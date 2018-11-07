//
// Created by wong on 10/30/18.
//

#include "splicepump.h"

/*
 * CONDITION: data_size_in_pipe and socket_can_keep_read_or_write
 * read socket EOF: pipe not full && splice() == 0
 * write pipe EOF: pipe is full(should not happen frequently, as long as we read from pipe as quick as possible)
 * read pipe EOF: other side shutdown && pipe is empty(aka nothing to do)
 * write socket EOF: pipe is empty && (splice_to_socket == 0 || socket errno == ECONNRESET)
 * strategy: alway enable EV_READ|EV_WRITE, check invariant at the beginning, if error occured
 *           check shutdown_how and determine what to do next, handle pipe or handle socket
 */

#define TRANSOCKS_SPLICE_SHOULD_LOG_ERR(err) ((err) != EBADF && (err) != ENOTCONN)

transocks_pump transocks_splicepump_ops;

static transocks_splicepump *transocks_splicepump_new(transocks_client *pclient);

static int getpipesize(int fd);

static void transocks_splicepump_free(transocks_client *pclient);

static void transocks_splicepump_client_readcb(evutil_socket_t fd, short events, void *arg);

static void transocks_splicepump_relay_writecb(evutil_socket_t fd, short events, void *arg);

static void transocks_splicepump_relay_readcb(evutil_socket_t fd, short events, void *arg);

static void transocks_splicepump_client_writecb(evutil_socket_t fd, short events, void *arg);

static inline bool splicepump_check_close(transocks_client *pclient) {
    return pclient->client_shutdown_read
           && pclient->client_shutdown_write
           && pclient->relay_shutdown_read
           && pclient->relay_shutdown_write;
}

static int getpipesize(int fd) {
    if (fd < 0) return -1;
    int ret = fcntl(fd, F_GETPIPE_SZ);
    if (ret == -1) {
        LOGE_ERRNO("fcntl F_GETPIPE_SZ");
        return -1;
    }
    return ret;
}

static void transocks_splicepump_client_readcb(evutil_socket_t fd, short events, void *arg) {
    TRANSOCKS_UNUSED(fd);
    TRANSOCKS_UNUSED(events);
    transocks_client *pclient = (transocks_client *) arg;
    transocks_splicepump *ppump = (transocks_splicepump *) (pclient->user_arg);
    ssize_t bytesRead;
    bytesRead = splice(pclient->clientFd, NULL, ppump->outbound_pipe->pipe_writefd, NULL,
                       ppump->outbound_pipe->capacity, SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
    if (bytesRead == -1) {
        if (TRANSOCKS_IS_RETRIABLE(errno)) {
            // return for next event
            return;
        } else {
            // error, should close
            if (TRANSOCKS_SPLICE_SHOULD_LOG_ERR(errno))
                LOGE_ERRNO("splice client read");
            TRANSOCKS_FREE(transocks_splicepump_free, pclient);
            return;
        }
    } else if (bytesRead == 0) {
        if (ppump->outbound_pipe->data_in_pipe < ppump->outbound_pipe->capacity) {
            // pipe not full, close read side
            pclient->client_shutdown_read = true;
            TRANSOCKS_SHUTDOWN(pclient->clientFd, SHUT_RD);
            TRANSOCKS_CLOSE(ppump->outbound_pipe->pipe_writefd);
            // not interested on the event anymore
            event_del(ppump->client_read_ev);
            // if pipe is already empty, close the other side as well
            if (ppump->outbound_pipe->data_in_pipe == 0) {
                pclient->relay_shutdown_write = true;
                TRANSOCKS_SHUTDOWN(pclient->relayFd, SHUT_WR);
                TRANSOCKS_CLOSE(ppump->outbound_pipe->pipe_readfd);
            }
        } else {
            // pipe full, let other side write
            event_active(ppump->relay_write_ev, EV_WRITE, 0);
            return;
        }
    } else {
        // get some data from client
        ppump->outbound_pipe->data_in_pipe += bytesRead;
    }

    if (ppump->outbound_pipe->data_in_pipe != 0) {
        // other side can write
        event_active(ppump->relay_write_ev, EV_WRITE, 0);
    }

    if (splicepump_check_close(pclient)) {
        TRANSOCKS_FREE(transocks_splicepump_free, pclient);
        return;
    }
}

static void transocks_splicepump_relay_writecb(evutil_socket_t fd, short events, void *arg) {
    TRANSOCKS_UNUSED(fd);
    TRANSOCKS_UNUSED(events);
    transocks_client *pclient = (transocks_client *) arg;
    transocks_splicepump *ppump = (transocks_splicepump *) (pclient->user_arg);
    ssize_t bytesRead;
    bytesRead = splice(ppump->outbound_pipe->pipe_readfd, NULL, pclient->relayFd, NULL,
                       (size_t) ppump->outbound_pipe->data_in_pipe, SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
    if (bytesRead == -1) {
        if (TRANSOCKS_IS_RETRIABLE(errno)) {
            // self active to retry
            event_active(ppump->relay_write_ev, EV_WRITE, 0);
            return;
        } else {
            // error, should close
            if (TRANSOCKS_SPLICE_SHOULD_LOG_ERR(errno))
                LOGE_ERRNO("splice relay write");
            TRANSOCKS_FREE(transocks_splicepump_free, pclient);
            return;
        }
    } else if (bytesRead == 0) {
        // no data in pipe, check other end
        if (pclient->client_shutdown_read) {
            // other end closed
            pclient->relay_shutdown_write = true;
            TRANSOCKS_SHUTDOWN(pclient->relayFd, SHUT_WR);
            TRANSOCKS_CLOSE(ppump->outbound_pipe->pipe_readfd);
        }
    } else {
        // get some data from pipe
        ppump->outbound_pipe->data_in_pipe -= bytesRead;
    }

    if (!pclient->client_shutdown_read) {
        // other side not closed
        event_active(ppump->client_read_ev, EV_READ, 0);
    }
    if (ppump->outbound_pipe->data_in_pipe > 0 && pclient->client_shutdown_read) {
        // still has data and other side closed
        event_active(ppump->relay_write_ev, EV_WRITE, 0);
    }

    if (splicepump_check_close(pclient)) {
        TRANSOCKS_FREE(transocks_splicepump_free, pclient);
        return;
    }
}

static void transocks_splicepump_relay_readcb(evutil_socket_t fd, short events, void *arg) {
    TRANSOCKS_UNUSED(fd);
    TRANSOCKS_UNUSED(events);
    transocks_client *pclient = (transocks_client *) arg;
    transocks_splicepump *ppump = (transocks_splicepump *) (pclient->user_arg);
    ssize_t bytesRead;
    bytesRead = splice(pclient->relayFd, NULL, ppump->inbound_pipe->pipe_writefd, NULL,
                       ppump->inbound_pipe->capacity, SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
    if (bytesRead == -1) {
        if (TRANSOCKS_IS_RETRIABLE(errno)) {
            // return for next event
            return;
        } else {
            // error, should close
            if (TRANSOCKS_SPLICE_SHOULD_LOG_ERR(errno))
                LOGE_ERRNO("splice relay read");
            TRANSOCKS_FREE(transocks_splicepump_free, pclient);
            return;
        }
    } else if (bytesRead == 0) {
        if (ppump->inbound_pipe->data_in_pipe < ppump->inbound_pipe->capacity) {
            // pipe not full, close read side
            pclient->relay_shutdown_read = true;
            TRANSOCKS_SHUTDOWN(pclient->relayFd, SHUT_RD);
            TRANSOCKS_CLOSE(ppump->inbound_pipe->pipe_writefd);
            // not interested on the event anymore
            event_del(ppump->relay_read_ev);
            // if pipe is already empty, close the other side as well
            if (ppump->inbound_pipe->data_in_pipe == 0) {
                pclient->client_shutdown_write = true;
                TRANSOCKS_SHUTDOWN(pclient->clientFd, SHUT_WR);
                TRANSOCKS_CLOSE(ppump->inbound_pipe->pipe_readfd);
            }
        } else {
            // pipe full, let other side write
            event_active(ppump->client_write_ev, EV_WRITE, 0);
            return;
        }
    } else {
        // get some data from relay
        ppump->inbound_pipe->data_in_pipe += bytesRead;
    }

    if (ppump->inbound_pipe->data_in_pipe != 0) {
        // other side can write
        event_active(ppump->client_write_ev, EV_WRITE, 0);
    }

    if (splicepump_check_close(pclient)) {
        TRANSOCKS_FREE(transocks_splicepump_free, pclient);
        return;
    }
}

static void transocks_splicepump_client_writecb(evutil_socket_t fd, short events, void *arg) {
    TRANSOCKS_UNUSED(fd);
    TRANSOCKS_UNUSED(events);
    transocks_client *pclient = (transocks_client *) arg;
    transocks_splicepump *ppump = (transocks_splicepump *) (pclient->user_arg);
    ssize_t bytesRead;
    bytesRead = splice(ppump->inbound_pipe->pipe_readfd, NULL, pclient->clientFd, NULL,
                       (size_t) ppump->inbound_pipe->data_in_pipe, SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
    if (bytesRead == -1) {
        if (TRANSOCKS_IS_RETRIABLE(errno)) {
            // self active to retry
            event_active(ppump->client_write_ev, EV_WRITE, 0);
            return;
        } else {
            // error
            if (TRANSOCKS_SPLICE_SHOULD_LOG_ERR(errno))
                LOGE_ERRNO("splice client write");
            TRANSOCKS_FREE(transocks_splicepump_free, pclient);
            return;
        }
    } else if (bytesRead == 0) {
        // no data in pipe, check other end
        if (pclient->relay_shutdown_read) {
            // other end closed
            pclient->client_shutdown_write = true;
            TRANSOCKS_SHUTDOWN(pclient->clientFd, SHUT_WR);
            TRANSOCKS_CLOSE(ppump->inbound_pipe->pipe_readfd);
        }
    } else {
        // get some data from pipe
        ppump->inbound_pipe->data_in_pipe -= bytesRead;
    }

    if (!pclient->relay_shutdown_read) {
        // other side not closed
        event_active(ppump->relay_read_ev, EV_READ, 0);
    }
    if (ppump->inbound_pipe->data_in_pipe > 0 && pclient->relay_shutdown_read) {
        // still has data and other side closed
        event_active(ppump->client_write_ev, EV_WRITE, 0);
    }

    if (splicepump_check_close(pclient)) {
        TRANSOCKS_FREE(transocks_splicepump_free, pclient);
        return;
    }
}

static transocks_splicepump *transocks_splicepump_new(transocks_client *pclient) {
    transocks_splicepump *pump = calloc(1, sizeof(transocks_splicepump));
    if (pump == NULL) {
        LOGE("mem");
        return NULL;
    }
    pump->inbound_pipe = calloc(1, sizeof(transocks_splicepipe));
    if (pump->inbound_pipe == NULL) {
        LOGE("mem");
        return NULL;
    }
    pump->outbound_pipe = calloc(1, sizeof(transocks_splicepipe));
    if (pump->outbound_pipe == NULL) {
        LOGE("mem");
        return NULL;
    }
    pump->inbound_pipe->pipe_writefd = -1;
    pump->inbound_pipe->pipe_readfd = -1;
    pump->inbound_pipe->data_in_pipe = 0;
    pump->outbound_pipe->pipe_writefd = -1;
    pump->outbound_pipe->pipe_readfd = -1;
    pump->outbound_pipe->data_in_pipe = 0;
    // attach pump to client context
    pclient->user_arg = pump;

    return pump;
}

static void transocks_splicepump_free(transocks_client *pclient) {
    if (pclient == NULL)
        return;
    transocks_splicepump *ppump = (transocks_splicepump *) (pclient->user_arg);
    if (ppump == NULL)
        return;
    LOGD("enter");
    event_del(ppump->client_read_ev);
    event_del(ppump->client_write_ev);
    event_del(ppump->relay_read_ev);
    event_del(ppump->relay_write_ev);
    TRANSOCKS_FREE(event_free, ppump->client_read_ev);
    TRANSOCKS_FREE(event_free, ppump->client_write_ev);
    TRANSOCKS_FREE(event_free, ppump->relay_read_ev);
    TRANSOCKS_FREE(event_free, ppump->relay_write_ev);

    TRANSOCKS_CLOSE(ppump->inbound_pipe->pipe_readfd);
    TRANSOCKS_CLOSE(ppump->inbound_pipe->pipe_writefd);
    TRANSOCKS_FREE(free, ppump->inbound_pipe);

    TRANSOCKS_CLOSE(ppump->outbound_pipe->pipe_readfd);
    TRANSOCKS_CLOSE(ppump->outbound_pipe->pipe_writefd);
    TRANSOCKS_FREE(free, ppump->outbound_pipe);

    TRANSOCKS_FREE(free, ppump);
    // call outer free func
    TRANSOCKS_FREE(transocks_client_free, pclient);
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
    pump->client_read_ev = event_new(penv->eventBaseLoop, pclient->clientFd, EV_READ | EV_PERSIST,
                                     transocks_splicepump_client_readcb, pclient);
    if (pump->client_read_ev == NULL) {
        goto fail;
    }
    pump->client_write_ev = event_new(penv->eventBaseLoop, pclient->clientFd, EV_WRITE | EV_PERSIST,
                                      transocks_splicepump_client_writecb, pclient);
    if (pump->client_write_ev == NULL) {
        goto fail;
    }
    pump->relay_read_ev = event_new(penv->eventBaseLoop, pclient->relayFd, EV_READ | EV_PERSIST,
                                    transocks_splicepump_relay_readcb, pclient);
    if (pump->relay_read_ev == NULL) {
        goto fail;
    }
    pump->relay_write_ev = event_new(penv->eventBaseLoop, pclient->relayFd, EV_WRITE | EV_PERSIST,
                                     transocks_splicepump_relay_writecb, pclient);
    if (pump->relay_write_ev == NULL) {
        goto fail;
    }
    // start both side read and let read callbacks control write events
    if (event_add(pump->client_read_ev, NULL) != 0) {
        goto fail;
    }
    if (event_add(pump->relay_read_ev, NULL) != 0) {
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
        .free_pump_fn = transocks_splicepump_free
};
