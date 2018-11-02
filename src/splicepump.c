//
// Created by wong on 10/30/18.
//

/* for splice() and F_GETPIPE_SZ */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "splicepump.h"

// TODO

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
    ssize_t bytesRead;
    bytesRead = splice((*ppclient)->clientFd, NULL, ppump->outbound_pipe.pipe_writefd, NULL,
                       ppump->outbound_pipe.pipe_size, SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
    if (bytesRead == -1) {
        if (is_retriable(errno)) {
            // retriable
        } else {
            // error, should close
        }
    } else if (bytesRead == 0) {
        // EOF
        (*ppclient)->client_shutdown_how |= EV_READ;
        if (shutdown((*ppclient)->clientFd, SHUT_RD) < 0) {
            LOGE_ERRNO("client shutdown read");
        }
    } else {
        // get some data from client
        ppump->outbound_pipe.data_in_pipe += bytesRead;
    }
}

static void transocks_splicepump_relay_writecb(evutil_socket_t fd, short events, void *arg) {
    transocks_client **ppclient = (transocks_client **) arg;
    transocks_splicepump *ppump = (transocks_splicepump *) ((*ppclient)->user_arg);
    ssize_t bytesRead;
    bytesRead = splice(ppump->outbound_pipe.pipe_readfd, NULL, (*ppclient)->relayFd, NULL,
                       (size_t) ppump->outbound_pipe.data_in_pipe, SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
    if (bytesRead == -1) {
        if (is_retriable(errno)) {
            // retriable
        } else {
            // error, should close
        }
    } else if (bytesRead == 0) {
        // EOF
    } else {
        // get some data from client
        ppump->outbound_pipe.data_in_pipe = bytesRead;
    }
}

static void transocks_splicepump_relay_readcb(evutil_socket_t fd, short events, void *arg) {
    transocks_client **ppclient = (transocks_client **) arg;
    transocks_splicepump *ppump = (transocks_splicepump *) ((*ppclient)->user_arg);
}

static void transocks_splicepump_client_writecb(evutil_socket_t fd, short events, void *arg) {
    transocks_client **ppclient = (transocks_client **) arg;
    transocks_splicepump *ppump = (transocks_splicepump *) ((*ppclient)->user_arg);
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
    transocks_splicepump *ppump = (transocks_splicepump *) ((*ppclient)->user_arg);
    event_del(ppump->client_read_ev);
    event_free(ppump->client_read_ev);
    event_del(ppump->client_write_ev);
    event_free(ppump->client_write_ev);
    event_del(ppump->relay_read_ev);
    event_free(ppump->relay_read_ev);
    event_del(ppump->relay_write_ev);
    event_free(ppump->relay_write_ev);
    close(ppump->inbound_pipe.pipe_readfd);
    close(ppump->inbound_pipe.pipe_writefd);
    close(ppump->outbound_pipe.pipe_readfd);
    close(ppump->outbound_pipe.pipe_writefd);
}

static int transocks_splicepump_start_pump(transocks_client **ppclient) {
    transocks_client *pclient = *ppclient;
    transocks_global_env *penv = pclient->global_env;
    int pipesz;
    int error = bufferevent_disable(pclient->client_bev, EV_READ | EV_WRITE);
    if (error) {
        LOGE("client bev bufferevent_disable");
        goto freeClient;
    }
    error = bufferevent_disable(pclient->relay_bev, EV_READ | EV_WRITE);
    if (error) {
        LOGE("relay bev bufferevent_disable");
        goto freeClient;
    }
    transocks_splicepump *pump = transocks_splicepump_new(ppclient);
    if (pump == NULL) {
        LOGE("fail to allocate memory");
        goto freeClient;
    }

    // create pipe
    if (createpipe(&pump->inbound_pipe.pipe_readfd, &pump->inbound_pipe.pipe_writefd) != 0) {
        goto freeSplicepump;
    }
    if (createpipe(&pump->outbound_pipe.pipe_readfd, &pump->outbound_pipe.pipe_writefd) != 0) {
        goto freeSplicepump;
    }
    // set pipe size
    if ((pipesz = getpipesize(pump->inbound_pipe.pipe_readfd)) == -1) {
        goto freeSplicepump;
    }
    pump->inbound_pipe.pipe_size = (size_t) pipesz;

    if ((pipesz = getpipesize(pump->outbound_pipe.pipe_readfd)) == -1) {
        goto freeSplicepump;
    }
    pump->outbound_pipe.pipe_size = (size_t) pipesz;
    // create event
    pump->client_read_ev = event_new(penv->eventBaseLoop, pclient->clientFd, EV_READ | EV_PERSIST,
                                     transocks_splicepump_client_readcb, ppclient);
    if (pump->client_read_ev == NULL) {
        goto freeSplicepump;
    }
    pump->client_write_ev = event_new(penv->eventBaseLoop, pclient->clientFd, EV_WRITE | EV_PERSIST,
                                      transocks_splicepump_client_writecb, ppclient);
    if (pump->client_write_ev == NULL) {
        goto freeSplicepump;
    }
    pump->relay_read_ev = event_new(penv->eventBaseLoop, pclient->relayFd, EV_READ | EV_PERSIST,
                                    transocks_splicepump_relay_readcb, ppclient);
    if (pump->relay_read_ev == NULL) {
        goto freeSplicepump;
    }
    pump->relay_write_ev = event_new(penv->eventBaseLoop, pclient->relayFd, EV_WRITE | EV_PERSIST,
                                     transocks_splicepump_relay_writecb, ppclient);
    if (pump->relay_write_ev == NULL) {
        goto freeSplicepump;
    }
    // start both side read
    if (event_add(pump->client_read_ev, NULL) != 0) {
        goto freeSplicepump;
    }
    if (event_add(pump->relay_read_ev, NULL) != 0) {
        goto freeSplicepump;
    }
    return 0;

    freeSplicepump:
    transocks_splicepump_free(ppclient);
    freeClient:
    transocks_client_free(ppclient);
    return -1;
}


transocks_pump transocks_splicepump_ops = {
        .name = PUMPMETHOD_SPLICE,
        .start_pump_fn = transocks_splicepump_start_pump,
};
