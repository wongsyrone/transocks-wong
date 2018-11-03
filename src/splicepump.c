//
// Created by wong on 10/30/18.
//

/* for splice() and F_GETPIPE_SZ */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

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

transocks_pump transocks_splicepump_ops;

static transocks_splicepump *transocks_splicepump_new(transocks_client **ppclient);

static int getpipesize(int fd);

static void transocks_splicepump_free(transocks_client **ppclient);

static void transocks_splicepump_client_readcb(evutil_socket_t fd, short events, void *arg);

static void transocks_splicepump_relay_writecb(evutil_socket_t fd, short events, void *arg);

static void transocks_splicepump_relay_readcb(evutil_socket_t fd, short events, void *arg);

static void transocks_splicepump_client_writecb(evutil_socket_t fd, short events, void *arg);

static inline bool splicepump_check_close(transocks_client **ppclient) {
    transocks_client *pclient = *ppclient;
    return ((pclient->client_shutdown_how & EV_READ) == EV_READ
            && (pclient->client_shutdown_how & EV_WRITE) == EV_WRITE
            && (pclient->relay_shutdown_how & EV_READ) == EV_READ
            && (pclient->relay_shutdown_how & EV_WRITE) == EV_WRITE);
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
    transocks_client **ppclient = (transocks_client **) arg;
    transocks_splicepump *ppump = (transocks_splicepump *) ((*ppclient)->user_arg);
    bool client_can_read = true;
    bool is_pipe_full = ppump->outbound_pipe.data_in_pipe >= ppump->outbound_pipe.capacity;
    if (is_pipe_full) {
        LOGD("pipe full")
        return;
    }
    ssize_t bytesRead;
    bytesRead = splice((*ppclient)->clientFd, NULL, ppump->outbound_pipe.pipe_writefd, NULL,
                       ppump->outbound_pipe.capacity, SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
    if (bytesRead == -1) {
        if (TRANSOCKS_IS_RETRIABLE(errno)) {
            // return for next event
            return;
        } else {
            // error, should close
            client_can_read = false;
            LOGE_ERRNO("splice client read");
            goto decide;
        }
    } else if (bytesRead == 0) {
        // client read EOF or pipe is full
        client_can_read = false;
        goto decide;
    } else {
        // get some data from client
        ppump->outbound_pipe.data_in_pipe += bytesRead;
        // other side can write
        event_active(ppump->relay_write_ev, EV_WRITE, 0);
    }

    decide:
    if (!client_can_read) {
        (*ppclient)->client_shutdown_how |= EV_READ;
        if (shutdown((*ppclient)->clientFd, SHUT_RD) < 0) {
            LOGE_ERRNO("client shutdown read");
        }
        event_del(ppump->client_read_ev);
    }
    if (splicepump_check_close(ppclient)) {
        transocks_splicepump_free(ppclient);
        return;
    }
}

static void transocks_splicepump_relay_writecb(evutil_socket_t fd, short events, void *arg) {
    transocks_client **ppclient = (transocks_client **) arg;
    transocks_splicepump *ppump = (transocks_splicepump *) ((*ppclient)->user_arg);
    bool is_pipe_empty = ppump->outbound_pipe.data_in_pipe == 0;
    if (is_pipe_empty) {
        LOGD("pipe empty");
        return;
    }
    bool is_client_closed = false;
    bool relay_can_write = true;
    ssize_t bytesRead;
    bytesRead = splice(ppump->outbound_pipe.pipe_readfd, NULL, (*ppclient)->relayFd, NULL,
                       (size_t) ppump->outbound_pipe.data_in_pipe, SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
    if (bytesRead == -1) {
        if (TRANSOCKS_IS_RETRIABLE(errno)) {
            // self active to retry
            event_active(ppump->relay_write_ev, EV_WRITE, 0);
            return;
        } else {
            // error
            relay_can_write = false;
            LOGE_ERRNO("splice relay write");
            goto decide;
        }
    } else if (bytesRead == 0) {
        // no data in pipe, check other end
        if (((*ppclient)->client_shutdown_how & EV_READ) == EV_READ) {
            // other end closed
            is_client_closed = true;
            relay_can_write = false;
            goto decide;
        }
    } else {
        // get some data from pipe
        ppump->outbound_pipe.data_in_pipe -= bytesRead;
    }

    decide:
    if (!relay_can_write) {
        (*ppclient)->relay_shutdown_how |= EV_WRITE;
        if (shutdown((*ppclient)->relayFd, SHUT_WR) < 0) {
            LOGE_ERRNO("relay shutdown write");
        }
        event_del(ppump->relay_write_ev);
    }
    if (splicepump_check_close(ppclient)) {
        transocks_splicepump_free(ppclient);
        return;
    }
}

static void transocks_splicepump_relay_readcb(evutil_socket_t fd, short events, void *arg) {
    transocks_client **ppclient = (transocks_client **) arg;
    transocks_splicepump *ppump = (transocks_splicepump *) ((*ppclient)->user_arg);
    bool relay_can_read = true;
    bool is_pipe_full = ppump->inbound_pipe.data_in_pipe >= ppump->inbound_pipe.capacity;
    if (is_pipe_full) {
        LOGD("pipe full")
        return;
    }
    ssize_t bytesRead;
    bytesRead = splice((*ppclient)->relayFd, NULL, ppump->inbound_pipe.pipe_writefd, NULL,
                       ppump->inbound_pipe.capacity, SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
    if (bytesRead == -1) {
        if (TRANSOCKS_IS_RETRIABLE(errno)) {
            // return for next event
            return;
        } else {
            // error, should close
            relay_can_read = false;
            LOGE_ERRNO("splice relay read");
            goto decide;
        }
    } else if (bytesRead == 0) {
        // relay read EOF
        relay_can_read = false;
        goto decide;
    } else {
        // get some data from client
        ppump->inbound_pipe.data_in_pipe += bytesRead;
        // other side can write
        event_active(ppump->client_write_ev, EV_WRITE, 0);
    }

    decide:
    if (!relay_can_read) {
        (*ppclient)->relay_shutdown_how |= EV_READ;
        if (shutdown((*ppclient)->relayFd, SHUT_RD) < 0) {
            LOGE_ERRNO("relay shutdown read");
        }
        event_del(ppump->relay_read_ev);
    }
    if (splicepump_check_close(ppclient)) {
        transocks_splicepump_free(ppclient);
        return;
    }
}

static void transocks_splicepump_client_writecb(evutil_socket_t fd, short events, void *arg) {
    transocks_client **ppclient = (transocks_client **) arg;
    transocks_splicepump *ppump = (transocks_splicepump *) ((*ppclient)->user_arg);
    bool is_pipe_empty = ppump->inbound_pipe.data_in_pipe == 0;
    if (is_pipe_empty) {
        LOGD("pipe empty");
        return;
    }
    bool is_relay_closed = false;
    bool client_can_write = true;
    ssize_t bytesRead;
    bytesRead = splice(ppump->inbound_pipe.pipe_readfd, NULL, (*ppclient)->clientFd, NULL,
                       (size_t) ppump->inbound_pipe.data_in_pipe, SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
    if (bytesRead == -1) {
        if (TRANSOCKS_IS_RETRIABLE(errno)) {
            // self active to retry
            event_active(ppump->client_write_ev, EV_WRITE, 0);
            return;
        } else {
            // error
            client_can_write = false;
            LOGE_ERRNO("splice client write");
            goto decide;
        }
    } else if (bytesRead == 0) {
        // no data in pipe, check other end
        if (((*ppclient)->relay_shutdown_how & EV_READ) == EV_READ) {
            // other end closed
            is_relay_closed = true;
            client_can_write = false;
            goto decide;
        }
    } else {
        // get some data from pipe
        ppump->inbound_pipe.data_in_pipe -= bytesRead;
    }

    decide:
    if (!client_can_write) {
        (*ppclient)->client_shutdown_how |= EV_WRITE;
        if (shutdown((*ppclient)->clientFd, SHUT_WR) < 0) {
            LOGE_ERRNO("client shutdown write");
        }
        event_del(ppump->client_write_ev);
    }
    if (splicepump_check_close(ppclient)) {
        transocks_splicepump_free(ppclient);
        return;
    }
}

static transocks_splicepump *transocks_splicepump_new(transocks_client **ppclient) {
    transocks_splicepump *pump = calloc(1, sizeof(transocks_splicepump));
    if (!pump) {
        LOGE("mem");
        return NULL;
    }

    pump->inbound_pipe.pipe_writefd = -1;
    pump->inbound_pipe.pipe_readfd = -1;
    pump->inbound_pipe.data_in_pipe = 0;
    pump->outbound_pipe.pipe_writefd = -1;
    pump->outbound_pipe.pipe_readfd = -1;
    pump->outbound_pipe.data_in_pipe = 0;
    // attach pump to client context
    (*ppclient)->user_arg = pump;

    return pump;
}

static void transocks_splicepump_free(transocks_client **ppclient) {
    if (ppclient == NULL)
        return;
    transocks_client *pclient = *ppclient;
    if (pclient == NULL)
        return;
    transocks_splicepump *ppump = (transocks_splicepump *) (pclient->user_arg);
    if (ppump == NULL) return;
    if (ppump->client_read_ev != NULL) {
        event_free(ppump->client_read_ev);
        ppump->client_read_ev = NULL;
    }
    if (ppump->client_write_ev != NULL) {
        event_free(ppump->client_write_ev);
        ppump->client_write_ev = NULL;
    }
    if (ppump->relay_read_ev != NULL) {
        event_free(ppump->relay_read_ev);
        ppump->relay_read_ev = NULL;
    }
    if (ppump->relay_write_ev != NULL) {
        event_free(ppump->relay_write_ev);
        ppump->relay_write_ev = NULL;
    }
    if (ppump->inbound_pipe.pipe_readfd > 0) {
        close(ppump->inbound_pipe.pipe_readfd);
        ppump->inbound_pipe.pipe_readfd = -1;
    }
    if (ppump->inbound_pipe.pipe_writefd > 0) {
        close(ppump->inbound_pipe.pipe_writefd);
        ppump->inbound_pipe.pipe_writefd = -1;
    }
    if (ppump->outbound_pipe.pipe_readfd > 0) {
        close(ppump->outbound_pipe.pipe_readfd);
        ppump->outbound_pipe.pipe_readfd = -1;
    }
    if (ppump->outbound_pipe.pipe_writefd > 0) {
        close(ppump->outbound_pipe.pipe_writefd);
        ppump->outbound_pipe.pipe_writefd = -1;
    }
    transocks_client_free(ppclient);
}

static int transocks_splicepump_start_pump(transocks_client **ppclient) {
    transocks_client *pclient = *ppclient;
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
    transocks_splicepump *pump = transocks_splicepump_new(ppclient);
    if (pump == NULL) {
        LOGE("fail to allocate memory");
        goto fail;
    }

    // create pipe
    if (createpipe(&pump->inbound_pipe.pipe_readfd, &pump->inbound_pipe.pipe_writefd) != 0) {
        goto fail;
    }
    if (createpipe(&pump->outbound_pipe.pipe_readfd, &pump->outbound_pipe.pipe_writefd) != 0) {
        goto fail;
    }
    // set pipe size
    if ((pipesz = getpipesize(pump->inbound_pipe.pipe_readfd)) == -1) {
        goto fail;
    }
    pump->inbound_pipe.capacity = (size_t) pipesz;

    if ((pipesz = getpipesize(pump->outbound_pipe.pipe_readfd)) == -1) {
        goto fail;
    }
    pump->outbound_pipe.capacity = (size_t) pipesz;
    // create event
    pump->client_read_ev = event_new(penv->eventBaseLoop, pclient->clientFd, EV_READ | EV_PERSIST,
                                     transocks_splicepump_client_readcb, ppclient);
    if (pump->client_read_ev == NULL) {
        goto fail;
    }
    pump->client_write_ev = event_new(penv->eventBaseLoop, pclient->clientFd, EV_WRITE | EV_PERSIST,
                                      transocks_splicepump_client_writecb, ppclient);
    if (pump->client_write_ev == NULL) {
        goto fail;
    }
    pump->relay_read_ev = event_new(penv->eventBaseLoop, pclient->relayFd, EV_READ | EV_PERSIST,
                                    transocks_splicepump_relay_readcb, ppclient);
    if (pump->relay_read_ev == NULL) {
        goto fail;
    }
    pump->relay_write_ev = event_new(penv->eventBaseLoop, pclient->relayFd, EV_WRITE | EV_PERSIST,
                                     transocks_splicepump_relay_writecb, ppclient);
    if (pump->relay_write_ev == NULL) {
        goto fail;
    }
    // start both side
    if (event_add(pump->client_read_ev, NULL) != 0) {
        goto fail;
    }
    if (event_add(pump->relay_read_ev, NULL) != 0) {
        goto fail;
    }
    if (event_add(pump->client_write_ev, NULL) != 0) {
        goto fail;
    }
    if (event_add(pump->relay_write_ev, NULL) != 0) {
        goto fail;
    }
    return 0;

    fail:
    transocks_splicepump_free(ppclient);
    return -1;
}


transocks_pump transocks_splicepump_ops = {
        .name = PUMPMETHOD_SPLICE,
        .start_pump_fn = transocks_splicepump_start_pump,
};
