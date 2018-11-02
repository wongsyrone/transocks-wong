//
// Created by wong on 10/30/18.
//

#include "bufferpump.h"

// TODO

/*
 * read EOF: eventcb & (BEV_EVENT_READING | BEV_EVENT_EOF)
 * write EOF: eventcb & (BEV_EVENT_ERROR | BEV_EVENT_WRITING) && errno == ECONNRESET
 * strategy: alway enable EV_READ|EV_WRITE, check invariant at the beginning, if error occured
 *           check shutdown_how and determine what to do next, retry or destroy
 */

transocks_pump transocks_bufferpump_ops;

static void transocks_client_readcb(struct bufferevent *bev, void *userArg);

static void transocks_client_writecb(struct bufferevent *bev, void *userArg);

static void transocks_relay_readcb(struct bufferevent *bev, void *userArg);

static void transocks_relay_writecb(struct bufferevent *bev, void *userArg);

static void transocks_client_eventcb(struct bufferevent *bev, short bevs, void *userArg);

static void transocks_relay_eventcb(struct bufferevent *bev, short bevs, void *userArg);

static void transocks_bufferpump_free(transocks_client **ppclient);




static void transocks_bufferpump_free(transocks_client **ppclient) {
    bufferevent_disable((*ppclient)->client_bev, EV_READ | EV_WRITE);
    bufferevent_disable((*ppclient)->relay_bev, EV_READ | EV_WRITE);
    transocks_client_free(ppclient);
}

static inline bool transocks_check_close(transocks_client **ppclient) {
    transocks_client *pclient = *ppclient;
    return ((pclient->client_shutdown_how & EV_READ) == EV_READ
            && (pclient->client_shutdown_how & EV_WRITE) == EV_WRITE
            && (pclient->relay_shutdown_how & EV_READ) == EV_READ
            && (pclient->relay_shutdown_how & EV_WRITE) == EV_WRITE);
}

static void transocks_client_readcb(struct bufferevent *bev, void *userArg) {
    transocks_client **ppclient = (transocks_client **) userArg;
    transocks_client *pclient = *ppclient;
    bufferevent_disable(pclient->client_bev, EV_READ);
    struct evbuffer *clientinbuf = bufferevent_get_input(pclient->client_bev);
    struct evbuffer *relayoutbuf = bufferevent_get_output(pclient->relay_bev);
    size_t srclen = evbuffer_get_length(clientinbuf);
    evbuffer_remove_buffer(clientinbuf, relayoutbuf, srclen);

    bufferevent_enable(pclient->relay_bev, EV_WRITE);
}

static void transocks_relay_writecb(struct bufferevent *bev, void *userArg) {
    transocks_client **ppclient = (transocks_client **) userArg;
    transocks_client *pclient = *ppclient;
    bufferevent_disable(pclient->relay_bev, EV_WRITE);
    // check client close
    if (0 == evbuffer_get_length(bufferevent_get_output(pclient->relay_bev))
        && (pclient->client_shutdown_how & EV_READ) == EV_READ) {
        pclient->relay_shutdown_how |= EV_WRITE;
        if (shutdown(pclient->relayFd, SHUT_WR) < 0) {
            LOGE_ERRNO("relay shutdown write err");
        }
    }
    if (transocks_check_close(ppclient)) {
        LOGD("passed close check");
        transocks_bufferpump_free(ppclient);
        return;
    }
    // reschedule next loop
    bufferevent_enable(pclient->client_bev, EV_READ);

}

static void transocks_relay_readcb(struct bufferevent *bev, void *userArg) {
    transocks_client **ppclient = (transocks_client **) userArg;
    transocks_client *pclient = *ppclient;
    bufferevent_disable(pclient->relay_bev, EV_READ);
    struct evbuffer *relayinbuf = bufferevent_get_input(pclient->relay_bev);
    struct evbuffer *clientoutbuf = bufferevent_get_output(pclient->client_bev);
    size_t srclen = evbuffer_get_length(relayinbuf);
    evbuffer_remove_buffer(relayinbuf, clientoutbuf, srclen);

    bufferevent_enable(pclient->client_bev, EV_WRITE);
}

static void transocks_client_writecb(struct bufferevent *bev, void *userArg) {
    transocks_client **ppclient = (transocks_client **) userArg;
    transocks_client *pclient = *ppclient;
    bufferevent_disable(pclient->client_bev, EV_WRITE);
    // check relay close
    if (0 == evbuffer_get_length(bufferevent_get_output(pclient->client_bev))
        && (pclient->relay_shutdown_how & EV_READ) == EV_READ) {
        pclient->client_shutdown_how |= EV_WRITE;
        if (shutdown(pclient->clientFd, SHUT_WR) < 0) {
            LOGE_ERRNO("client shutdown write err");
        }
    }
    if (transocks_check_close(ppclient)) {
        LOGD("passed close check");
        transocks_bufferpump_free(ppclient);
        return;
    }
    // reschedule next loop
    bufferevent_enable(pclient->relay_bev, EV_READ);
}


static void transocks_client_eventcb(struct bufferevent *bev, short bevs, void *userArg) {
    transocks_client **ppclient = (transocks_client **) userArg;
    transocks_client *pclient = *ppclient;
    if (bevs & BEV_EVENT_ERROR) {
        int err = EVUTIL_SOCKET_ERROR();
        LOGE("client error %d (%s)", err, evutil_socket_error_to_string(err));
        transocks_bufferpump_free(ppclient);
        return;
    }
    if ((bevs & BEV_EVENT_READING) == BEV_EVENT_READING && (bevs & BEV_EVENT_EOF) == BEV_EVENT_EOF) {
        pclient->client_shutdown_how |= EV_READ;
        bufferevent_disable(pclient->client_bev, EV_READ);
        if (shutdown(pclient->clientFd, SHUT_RD) < 0) {
            LOGE_ERRNO("client shutdown read err");
        }
    }
}

static void transocks_relay_eventcb(struct bufferevent *bev, short bevs, void *userArg) {
    transocks_client **ppclient = (transocks_client **) userArg;
    transocks_client *pclient = *ppclient;
    if (bevs & BEV_EVENT_ERROR) {
        int err = EVUTIL_SOCKET_ERROR();
        LOGE("relay error %d (%s)", err, evutil_socket_error_to_string(err));
        transocks_bufferpump_free(ppclient);
        return;
    }
    if ((bevs & BEV_EVENT_READING) == BEV_EVENT_READING && (bevs & BEV_EVENT_EOF) == BEV_EVENT_EOF) {
        pclient->relay_shutdown_how |= EV_READ;
        bufferevent_disable(pclient->relay_bev, EV_READ);
        if (shutdown(pclient->relayFd, SHUT_RD) < 0) {
            LOGE_ERRNO("relay shutdown read err");
        }
    }
}


static int transocks_bufferpump_start_pump(transocks_client **ppclient) {
    int err;
    struct bufferevent *client_bev = (*ppclient)->client_bev;
    struct bufferevent *relay_bev = (*ppclient)->relay_bev;
    bufferevent_setwatermark(client_bev, EV_READ | EV_WRITE, 0, TRANSOCKS_BUFSIZE);
    bufferevent_setwatermark(relay_bev, EV_READ | EV_WRITE, 0, TRANSOCKS_BUFSIZE);
    bufferevent_setcb(client_bev, transocks_client_readcb, transocks_client_writecb,
                      transocks_client_eventcb, ppclient);
    bufferevent_setcb(relay_bev, transocks_relay_readcb, transocks_relay_writecb,
                      transocks_relay_eventcb, ppclient);

    err = bufferevent_enable(client_bev, EV_READ | EV_WRITE);
    if (err) {
        LOGE("client bufferevent_enable");
        return -1;
    }
    err = bufferevent_enable(relay_bev, EV_READ | EV_WRITE);
    if (err) {
        LOGE("relay bufferevent_enable");
        return -1;
    }

    return 0;
}

transocks_pump transocks_bufferpump_ops = {
        .name = PUMPMETHOD_BUFFER,
        .start_pump_fn = transocks_bufferpump_start_pump,
};