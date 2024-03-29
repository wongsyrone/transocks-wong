//
// Created by wong on 10/30/18.
//

#include "splicepump.h"

/*
 *   source socket => pipe write end => pipe read end => target socket
 *  read socket EOF   write pipe EOF    read pipe EOF   write socket EOF
 */

/*
 * CONDITION: data_size_in_pipe and socket_can_keep_read_or_write
 * read socket EOF: pipe not full && splice_from_socket() == 0
 * write pipe EOF: read socket EOF && pipe not full
 *                 if pipe is full, let other side read
 * read pipe EOF: splice_from_pipe() == 0 {aka pipe write end closed}
 * write socket EOF: read pipe EOF && pipe is empty
 *                   if pipe is not empty, self activate until all data been written to target socket
 * strategy: always enable EV_READ, let read callbacks control write callbacks, if error occured
 *           check shutdown_how and determine what to do next, handle pipe or handle socket
 */

#define TRANSOCKS_SPLICE_SHOULD_LOG_ERR(err) ((err) != EBADF && (err) != ENOTCONN)

transocks_pump transocks_splicepump_ops;

static transocks_splicepump *transocks_splicepump_new(transocks_client *pclient);

static transocks_splicepump *transocks_get_splicepump(transocks_client *pclient);

static void transocks_write_to_pipe(transocks_client *pclient, transocks_splicepump_data_direction direction);

static void transocks_read_from_pipe(transocks_client *pclient, transocks_splicepump_data_direction direction);

static void transocks_on_fatal_failure(transocks_client *pclient);

static void
transocks_check_normal_termination(transocks_client *pclient, transocks_splicepump_data_direction direction);

static transocks_splicepump_splice_result transocks_single_splice(int from_fd, int to_fd,
                                                                  size_t want_len,
                                                                  ssize_t *transfered_bytes);

static int getpipesize(int fd);

static void transocks_splicepump_dump_info(transocks_client *pclient);

static void transocks_splicepump_free(transocks_client *pclient);

static void transocks_splicepump_client_readcb(evutil_socket_t fd, short events, void *arg);

static void transocks_splicepump_relay_writecb(evutil_socket_t fd, short events, void *arg);

static void transocks_splicepump_relay_readcb(evutil_socket_t fd, short events, void *arg);

static void transocks_splicepump_client_writecb(evutil_socket_t fd, short events, void *arg);


static int getpipesize(int fd) {
    if (fd < 0) return -1;
    int ret = fcntl(fd, F_GETPIPE_SZ);
    if (ret == -1) {
        LOGE_ERRNO("fcntl F_GETPIPE_SZ");
        return -1;
    }
    return ret;
}

static void transocks_on_fatal_failure(transocks_client *pclient) {
    LOGD("fatal_failure transocks_splicepump_free");
    TRANSOCKS_FREE(transocks_splicepump_free, pclient);
}

static void
transocks_check_normal_termination(transocks_client *pclient, transocks_splicepump_data_direction direction) {
    bool flag = false;
    if (TRANSOCKS_IS_INVALID_FD(pclient->client_fd) || TRANSOCKS_IS_INVALID_FD(pclient->relay_fd)) {
        // not half close, we can do nothing
        flag = true;
    } else {
        // valid fd
        if (direction == inbound) {
            if (pclient->relay_shutdown_read && pclient->client_shutdown_write) {
                flag = true;
            }
        }
        if (direction == outbound) {
            if (pclient->client_shutdown_read && pclient->relay_shutdown_write) {
                flag = true;
            }
        }
    }

    if (flag) {
        LOGD("normal transocks_splicepump_free");
        TRANSOCKS_FREE(transocks_splicepump_free, pclient);
        return;
    }
}

static void transocks_splicepump_client_readcb(evutil_socket_t fd, short events, void *arg) {
    TRANSOCKS_UNUSED(fd);
    TRANSOCKS_UNUSED(events);
    transocks_client *pclient = (transocks_client *) arg;
    transocks_write_to_pipe(pclient, outbound);
}

static void transocks_splicepump_relay_writecb(evutil_socket_t fd, short events, void *arg) {
    TRANSOCKS_UNUSED(fd);
    TRANSOCKS_UNUSED(events);
    transocks_client *pclient = (transocks_client *) arg;
    transocks_splicepump *ppump = transocks_get_splicepump(pclient);
    transocks_read_from_pipe(pclient, outbound);
}

static void transocks_splicepump_relay_readcb(evutil_socket_t fd, short events, void *arg) {
    TRANSOCKS_UNUSED(fd);
    TRANSOCKS_UNUSED(events);
    transocks_client *pclient = (transocks_client *) arg;
    transocks_write_to_pipe(pclient, inbound);
}

static void transocks_splicepump_client_writecb(evutil_socket_t fd, short events, void *arg) {
    TRANSOCKS_UNUSED(fd);
    TRANSOCKS_UNUSED(events);
    transocks_client *pclient = (transocks_client *) arg;
    transocks_read_from_pipe(pclient, inbound);
}

static transocks_splicepump_splice_result transocks_single_splice(int from_fd, int to_fd,
                                                                  size_t want_len,
                                                                  ssize_t *transfered_bytes) {
    ssize_t n = splice(from_fd, NULL,
                       to_fd, NULL,
                       want_len,
                       SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
    if (n == -1) {
        if (TRANSOCKS_IS_RETRIABLE(errno)) {
            return can_retry;
        } else {
            if (TRANSOCKS_SPLICE_SHOULD_LOG_ERR(errno)) {
                LOGE_ERRNO("splice from: %d, to: %d, want_len: %zu, ret %zd", from_fd, to_fd, want_len, n);
            }
            return fatal_failure;
        }
    } else if (n == 0) {
        return read_eof;
    } else {
        // data transfer
        *transfered_bytes = n;
        return normal_transfer;
    }
}

static void transocks_write_to_pipe(transocks_client *pclient, transocks_splicepump_data_direction direction) {
    transocks_splicepump *ppump = transocks_get_splicepump(pclient);
    transocks_splicepump_splice_result splice_ret;
    ssize_t transfer_bytes;
    int from_fd = -1, to_fd = -1;
    int ultimate_target_fd = -1;
    size_t want_len;
    transocks_splicepipe *pipe = NULL;
    bool *read_eof_flag;
    struct event *this_libevent_ev = NULL;
    struct event *ultimate_target_libevent_ev = NULL;

    if (direction == inbound) {
        from_fd = pclient->relay_fd;
        ultimate_target_fd = pclient->client_fd;
        pipe = ppump->inbound_pipe;
        read_eof_flag = &(pclient->relay_shutdown_read);
        this_libevent_ev = ppump->relay_read_ev;
        ultimate_target_libevent_ev = ppump->client_write_ev;
    }
    if (direction == outbound) {
        from_fd = pclient->client_fd;
        ultimate_target_fd = pclient->relay_fd;
        pipe = ppump->outbound_pipe;
        read_eof_flag = &(pclient->client_shutdown_read);
        this_libevent_ev = ppump->client_read_ev;
        ultimate_target_libevent_ev = ppump->relay_write_ev;
    }
    to_fd = pipe->pipe_writefd;
    want_len = pipe->capacity;
    if (TRANSOCKS_IS_INVALID_FD(from_fd) || TRANSOCKS_IS_INVALID_FD(to_fd)) {
        LOGD("fatal failure: invalid fd");
        transocks_on_fatal_failure(pclient);
        return;
    }
    splice_ret = transocks_single_splice(from_fd,
                                         to_fd,
                                         want_len,
                                         &transfer_bytes);
    switch (splice_ret) {
        case normal_transfer:
            pipe->data_in_pipe += (size_t) transfer_bytes;
            break;
        case can_retry:
            // pipe has data, should clear as soon as possible
            if (!TRANSOCKS_IS_INVALID_FD(ultimate_target_fd) && pipe->data_in_pipe > 0) {
                // activate events
                TRANSOCKS_EVENT_ACTIVE(ultimate_target_libevent_ev, EV_READ);
            }

            break;
        case fatal_failure:
            transocks_on_fatal_failure(pclient);
            return;
        case read_eof:
            *read_eof_flag = true;
            TRANSOCKS_SHUTDOWN(from_fd, SHUT_RD);
            TRANSOCKS_CLOSE(to_fd); // make sure pipe will get EOF
            // no more data from from_fd
            event_del(this_libevent_ev);
            break;
        default:
            break;
    }

    transocks_check_normal_termination(pclient, direction);
}

static void transocks_read_from_pipe(transocks_client *pclient, transocks_splicepump_data_direction direction) {
    transocks_splicepump *ppump = transocks_get_splicepump(pclient);
    transocks_splicepump_splice_result splice_ret;
    ssize_t transfer_bytes;
    int from_fd = -1, to_fd = -1;
    int origin_source_fd = -1;
    size_t want_len;
    transocks_splicepipe *pipe = NULL;
    bool *read_eof_flag;
    struct event *this_libevent_ev;

    if (direction == inbound) {
        origin_source_fd = pclient->relay_fd;
        to_fd = pclient->client_fd;
        pipe = ppump->inbound_pipe;
        read_eof_flag = &(pclient->client_shutdown_write);
    }
    if (direction == outbound) {
        origin_source_fd = pclient->client_fd;
        to_fd = pclient->relay_fd;
        pipe = ppump->outbound_pipe;
        read_eof_flag = &(pclient->relay_shutdown_write);
    }
    from_fd = pipe->pipe_readfd;
    want_len = pipe->data_in_pipe;
    if (TRANSOCKS_IS_INVALID_FD(from_fd) || TRANSOCKS_IS_INVALID_FD(to_fd)) {
        LOGD("fatal failure: invalid fd");
        transocks_on_fatal_failure(pclient);
        return;
    }
    splice_ret = transocks_single_splice(from_fd,
                                         to_fd,
                                         want_len,
                                         &transfer_bytes);
    switch (splice_ret) {
        case normal_transfer:
            pipe->data_in_pipe -= (size_t) transfer_bytes;
            break;
        case can_retry:
            LOGE("read_from_pipe splice: unexpected can_retry");
            break;
        case fatal_failure:
            transocks_on_fatal_failure(pclient);
            return;
        case read_eof:
            // no data from pipe anymore, thus cannot write to other side
            *read_eof_flag = true;
            TRANSOCKS_SHUTDOWN(to_fd, SHUT_WR);
            TRANSOCKS_CLOSE(from_fd);
            break;
        default:
            break;
    }

    transocks_check_normal_termination(pclient, direction);
}

static transocks_splicepump *transocks_splicepump_new(transocks_client *pclient) {
    transocks_splicepump *pump = tr_malloc(sizeof(transocks_splicepump));
    if (pump == NULL) {
        LOGE("mem");
        return NULL;
    }
    pump->client_read_ev = NULL;
    pump->client_write_ev = NULL;
    pump->relay_read_ev = NULL;
    pump->relay_write_ev = NULL;
    pump->inbound_pipe = NULL;
    pump->outbound_pipe = NULL;

    pump->inbound_pipe = tr_malloc(sizeof(transocks_splicepipe));
    if (pump->inbound_pipe == NULL) {
        LOGE("mem");
        return NULL;
    }
    pump->inbound_pipe->pipe_writefd = -1;
    pump->inbound_pipe->pipe_readfd = -1;
    pump->inbound_pipe->data_in_pipe = 0;

    pump->outbound_pipe = tr_malloc(sizeof(transocks_splicepipe));
    if (pump->outbound_pipe == NULL) {
        LOGE("mem");
        return NULL;
    }
    pump->outbound_pipe->pipe_writefd = -1;
    pump->outbound_pipe->pipe_readfd = -1;
    pump->outbound_pipe->data_in_pipe = 0;

    // attach pump to client context
    pclient->user_arg = pump;

    return pump;
}

static transocks_splicepump *transocks_get_splicepump(transocks_client *pclient) {
    transocks_splicepump *ppump = (transocks_splicepump *) (pclient->user_arg);
    return ppump;
}

static void transocks_splicepump_free(transocks_client *pclient) {
    if (pclient == NULL)
        return;
    transocks_splicepump *ppump = transocks_get_splicepump(pclient);
    if (ppump == NULL)
        return;
    LOGD("enter");

    TRANSOCKS_CLOSE(ppump->inbound_pipe->pipe_writefd);
    TRANSOCKS_CLOSE(ppump->inbound_pipe->pipe_readfd);
    TRANSOCKS_FREE(tr_free, ppump->inbound_pipe);

    TRANSOCKS_CLOSE(ppump->outbound_pipe->pipe_writefd);
    TRANSOCKS_CLOSE(ppump->outbound_pipe->pipe_readfd);
    TRANSOCKS_FREE(tr_free, ppump->outbound_pipe);

    if (ppump->client_read_ev != NULL) {
        event_del(ppump->client_read_ev);
    }
    if (ppump->client_write_ev != NULL) {
        event_del(ppump->client_write_ev);
    }
    if (ppump->relay_read_ev != NULL) {
        event_del(ppump->relay_read_ev);
    }
    if (ppump->relay_write_ev != NULL) {
        event_del(ppump->relay_write_ev);
    }
    TRANSOCKS_FREE(event_free, ppump->client_read_ev);
    TRANSOCKS_FREE(event_free, ppump->client_write_ev);
    TRANSOCKS_FREE(event_free, ppump->relay_read_ev);
    TRANSOCKS_FREE(event_free, ppump->relay_write_ev);

    TRANSOCKS_FREE(tr_free, ppump);
    // call outer free func
    TRANSOCKS_FREE(transocks_client_free, pclient);
}

static void transocks_splicepump_dump_info(transocks_client *pclient) {
    if (pclient == NULL)
        return;
    transocks_splicepump *ppump = transocks_get_splicepump(pclient);
    if (ppump == NULL)
        return;
    fprintf(stdout, "\n\tpipe:");
    fprintf(stdout, "\n\t  IN: R %d W %d DATALEN %ld CAPACITY %ld",
            ppump->inbound_pipe->pipe_readfd, ppump->inbound_pipe->pipe_writefd,
            ppump->inbound_pipe->data_in_pipe, ppump->inbound_pipe->capacity);
    fprintf(stdout, "\n\t  OUT: R %d W %d DATALEN %ld CAPACITY %ld",
            ppump->outbound_pipe->pipe_readfd, ppump->outbound_pipe->pipe_writefd,
            ppump->outbound_pipe->data_in_pipe, ppump->outbound_pipe->capacity);
    // call outer func
    transocks_client_dump_info(pclient);
}

static int transocks_splicepump_start_pump(transocks_client *pclient) {
    transocks_global_env *penv = pclient->global_env;
    int pipesz;
    int error = bufferevent_disable(pclient->client_bev, EV_READ | EV_WRITE);
    if (error) {
        LOGE("client bev bufferevent_disable");
        goto fail;
    }
    error = bufferevent_disable(pclient->relay_bev, EV_READ | EV_WRITE);
    if (error) {
        LOGE("relay bev bufferevent_disable");
        goto fail;
    }
    // drop fd, we take the responsibility
    bufferevent_setfd(pclient->client_bev, -1);
    bufferevent_setfd(pclient->relay_bev, -1);

    transocks_splicepump *pump = transocks_splicepump_new(pclient);
    if (pump == NULL) {
        LOGE("fail to allocate memory");
        goto fail;
    }

    // create pipe
    if (createpipe(&(pump->inbound_pipe->pipe_readfd), &(pump->inbound_pipe->pipe_writefd)) != 0) {
        goto fail;
    }
    if (createpipe(&(pump->outbound_pipe->pipe_readfd), &(pump->outbound_pipe->pipe_writefd)) != 0) {
        goto fail;
    }
    // set pipe size
    if ((pipesz = getpipesize(pump->inbound_pipe->pipe_readfd)) == -1) {
        goto fail;
    }
    pump->inbound_pipe->capacity = (size_t) pipesz;

    if ((pipesz = getpipesize(pump->outbound_pipe->pipe_readfd)) == -1) {
        goto fail;
    }
    pump->outbound_pipe->capacity = (size_t) pipesz;
    // create event
    pump->client_read_ev = event_new(penv->eventBaseLoop, pclient->client_fd, EV_READ | EV_PERSIST,
                                     transocks_splicepump_client_readcb, pclient);
    if (pump->client_read_ev == NULL) {
        goto fail;
    }
    pump->client_write_ev = event_new(penv->eventBaseLoop, pump->inbound_pipe->pipe_readfd, EV_READ | EV_PERSIST,
                                      transocks_splicepump_client_writecb, pclient);
    if (pump->client_write_ev == NULL) {
        goto fail;
    }
    pump->relay_read_ev = event_new(penv->eventBaseLoop, pclient->relay_fd, EV_READ | EV_PERSIST,
                                    transocks_splicepump_relay_readcb, pclient);
    if (pump->relay_read_ev == NULL) {
        goto fail;
    }
    pump->relay_write_ev = event_new(penv->eventBaseLoop, pump->outbound_pipe->pipe_readfd, EV_READ | EV_PERSIST,
                                     transocks_splicepump_relay_writecb, pclient);
    if (pump->relay_write_ev == NULL) {
        goto fail;
    }
    // start both side read
    if (event_add(pump->client_read_ev, NULL) != 0) {
        goto fail;
    }
    if (event_add(pump->client_write_ev, NULL) != 0) {
        goto fail;
    }
    if (event_add(pump->relay_read_ev, NULL) != 0) {
        goto fail;
    }
    if (event_add(pump->relay_write_ev, NULL) != 0) {
        goto fail;
    }
    return 0;

    fail:
    TRANSOCKS_FREE(transocks_splicepump_free, pclient);
    return -1;
}


transocks_pump transocks_splicepump_ops = {
        .name = PUMPMETHOD_SPLICE,
        .start_pump_fn = transocks_splicepump_start_pump,
        .free_pump_fn = transocks_splicepump_free,
        .dump_info_fn = transocks_splicepump_dump_info
};
