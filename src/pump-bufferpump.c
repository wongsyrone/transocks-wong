//
// Created by wong on 10/30/18.
//

#include "pump-bufferpump.h"

/*
 * read EOF: eventcb & (BEV_EVENT_READING | BEV_EVENT_EOF)
 * write EOF: eventcb & (BEV_EVENT_ERROR | BEV_EVENT_WRITING) && errno == ECONNRESET
 * strategy: alway enable EV_READ|EV_WRITE, check invariant at the beginning, if error occured
 *           check shutdown_how and determine what to do next, retry or destroy
 */

transocks_pump transocks_pump_bufferpump_ops;

static void transocks_client_readcb(struct bufferevent *bev, void *userArg);

static void transocks_client_writecb(struct bufferevent *bev, void *userArg);

static void transocks_relay_readcb(struct bufferevent *bev, void *userArg);

static void transocks_relay_writecb(struct bufferevent *bev, void *userArg);

static void transocks_client_eventcb(struct bufferevent *bev, short bevs, void *userArg);

static void transocks_relay_eventcb(struct bufferevent *bev, short bevs, void *userArg);

static void transocks_bufferpump_free(transocks_client *pclient);

static void transocks_bufferpump_dump_info(transocks_client *pclient);

static void transocks_bufferpump_dump_info(transocks_client *pclient) {
    transocks_client_dump_info(pclient);
}

static void transocks_bufferpump_free(transocks_client *pclient) {
    bufferevent_disable(pclient->clientBufferEvent, EV_READ | EV_WRITE);
    bufferevent_disable(pclient->relayBufferEvent, EV_READ | EV_WRITE);
    TRANSOCKS_FREE(transocks_client_free, pclient);
}

TRANSOCKS_ALWAYS_INLINE
static bool transocks_check_close(transocks_client *pclient) {
    return pclient->isClientShutdownRead
           && pclient->isClientShutdownWrite
           && pclient->isRelayShutdownRead
           && pclient->isRelayShutdownWrite;
}

static void transocks_client_readcb(struct bufferevent *bev, void *userArg) {
    TRANSOCKS_UNUSED(bev);
    transocks_client *pclient = (transocks_client *) userArg;
    struct evbuffer *clientinbuf = bufferevent_get_input(pclient->clientBufferEvent);
    struct evbuffer *relayoutbuf = bufferevent_get_output(pclient->relayBufferEvent);
    size_t srclen = evbuffer_get_length(clientinbuf);
    evbuffer_remove_buffer(clientinbuf, relayoutbuf, srclen);
}

static void transocks_relay_writecb(struct bufferevent *bev, void *userArg) {
    TRANSOCKS_UNUSED(bev);
    transocks_client *pclient = (transocks_client *) userArg;
    // check client close
    if (0 == evbuffer_get_length(bufferevent_get_output(pclient->relayBufferEvent))
        && pclient->isClientShutdownRead) {
        pclient->isRelayShutdownWrite = true;
        if (TRANSOCKS_SHUTDOWN(pclient->relayFd, SHUT_WR) < 0) {
            LOGE_ERRNO("relay shutdown write err");
        }
    }
    if (transocks_check_close(pclient)) {
        LOGD("passed close check");
        TRANSOCKS_FREE(transocks_bufferpump_free, pclient);
        return;
    }

}

static void transocks_relay_readcb(struct bufferevent *bev, void *userArg) {
    TRANSOCKS_UNUSED(bev);
    transocks_client *pclient = (transocks_client *) userArg;
    struct evbuffer *relayinbuf = bufferevent_get_input(pclient->relayBufferEvent);
    struct evbuffer *clientoutbuf = bufferevent_get_output(pclient->clientBufferEvent);
    size_t srclen = evbuffer_get_length(relayinbuf);
    evbuffer_remove_buffer(relayinbuf, clientoutbuf, srclen);
}

static void transocks_client_writecb(struct bufferevent *bev, void *userArg) {
    TRANSOCKS_UNUSED(bev);
    transocks_client *pclient = (transocks_client *) userArg;
    // check relay close
    if (0 == evbuffer_get_length(bufferevent_get_output(pclient->clientBufferEvent))
        && pclient->isRelayShutdownRead) {
        pclient->isClientShutdownWrite = true;
        if (TRANSOCKS_SHUTDOWN(pclient->clientFd, SHUT_WR) < 0) {
            LOGE_ERRNO("client shutdown write err");
        }
    }
    if (transocks_check_close(pclient)) {
        LOGD("passed close check");
        TRANSOCKS_FREE(transocks_bufferpump_free, pclient);
        return;
    }
}


static void transocks_client_eventcb(struct bufferevent *bev, short bevs, void *userArg) {
    TRANSOCKS_UNUSED(bev);
    transocks_client *pclient = (transocks_client *) userArg;
    if (TRANSOCKS_CHKBIT(bevs, BEV_EVENT_READING)
        && TRANSOCKS_CHKBIT(bevs, BEV_EVENT_EOF)) {
        // read eof
        pclient->isClientShutdownRead = true;
        bufferevent_disable(pclient->clientBufferEvent, EV_READ);
        if (TRANSOCKS_SHUTDOWN(pclient->clientFd, SHUT_RD) < 0) {
            LOGE_ERRNO("client shutdown read err");
        }
        // check if ouput cache is empty
        if (0 == evbuffer_get_length(bufferevent_get_output(pclient->relayBufferEvent))
            && pclient->isClientShutdownRead) {
            pclient->isRelayShutdownWrite = true;
            if (TRANSOCKS_SHUTDOWN(pclient->relayFd, SHUT_WR) < 0) {
                LOGE_ERRNO("relay shutdown write err");
            }
        }
        goto checkClose;
    }
    if (TRANSOCKS_CHKBIT(bevs, BEV_EVENT_WRITING)
        && ((TRANSOCKS_CHKBIT(bevs, BEV_EVENT_ERROR) && errno == ECONNRESET)
            || TRANSOCKS_CHKBIT(bevs, BEV_EVENT_EOF))) {
        // write eof
        pclient->isClientShutdownWrite = true;
        bufferevent_disable(pclient->clientBufferEvent, EV_WRITE);
        if (TRANSOCKS_SHUTDOWN(pclient->clientFd, SHUT_WR) < 0) {
            LOGE_ERRNO("client shutdown write err");
        }
        goto checkClose;
    }
    if (TRANSOCKS_CHKBIT(bevs, BEV_EVENT_ERROR)) {
        int err = EVUTIL_SOCKET_ERROR();
        LOGE("client error %d (%s)", err, evutil_socket_error_to_string(err));
        TRANSOCKS_FREE(transocks_bufferpump_free, pclient);
        return;
    }
    checkClose:
    if (transocks_check_close(pclient)) {
        LOGD("passed close check");
        TRANSOCKS_FREE(transocks_bufferpump_free, pclient);
        return;
    }
}

static void transocks_relay_eventcb(struct bufferevent *bev, short bevs, void *userArg) {
    TRANSOCKS_UNUSED(bev);
    transocks_client *pclient = (transocks_client *) userArg;
    if (TRANSOCKS_CHKBIT(bevs, BEV_EVENT_READING)
        && TRANSOCKS_CHKBIT(bevs, BEV_EVENT_EOF)) {
        // read eof
        pclient->isRelayShutdownRead = true;
        bufferevent_disable(pclient->relayBufferEvent, EV_READ);
        if (TRANSOCKS_SHUTDOWN(pclient->relayFd, SHUT_RD) < 0) {
            LOGE_ERRNO("relay shutdown read err");
        }
        // check if ouput cache is empty
        if (0 == evbuffer_get_length(bufferevent_get_output(pclient->clientBufferEvent))
            && pclient->isRelayShutdownRead) {
            pclient->isClientShutdownWrite = true;
            if (TRANSOCKS_SHUTDOWN(pclient->clientFd, SHUT_WR) < 0) {
                LOGE_ERRNO("client shutdown write err");
            }
        }
        goto checkClose;
    }
    if (TRANSOCKS_CHKBIT(bevs, BEV_EVENT_WRITING)
        && ((TRANSOCKS_CHKBIT(bevs, BEV_EVENT_ERROR) && errno == ECONNRESET)
            || TRANSOCKS_CHKBIT(bevs, BEV_EVENT_EOF))) {
        // write eof
        pclient->isRelayShutdownWrite = true;
        bufferevent_disable(pclient->relayBufferEvent, EV_WRITE);
        if (TRANSOCKS_SHUTDOWN(pclient->relayFd, SHUT_WR) < 0) {
            LOGE_ERRNO("relay shutdown write err");
        }
        goto checkClose;
    }
    if (TRANSOCKS_CHKBIT(bevs, BEV_EVENT_ERROR)) {
        int err = EVUTIL_SOCKET_ERROR();
        LOGE("relay error %d (%s)", err, evutil_socket_error_to_string(err));
        TRANSOCKS_FREE(transocks_bufferpump_free, pclient);
        return;
    }
    checkClose:
    if (transocks_check_close(pclient)) {
        LOGD("passed close check");
        TRANSOCKS_FREE(transocks_bufferpump_free, pclient);
        return;
    }
}


static int transocks_bufferpump_start_pump(transocks_client *pclient) {
    int err;
    struct bufferevent *client_bev = pclient->clientBufferEvent;
    struct bufferevent *relay_bev = pclient->relayBufferEvent;
    bufferevent_setwatermark(client_bev, EV_READ | EV_WRITE, 0, TRANSOCKS_BUFSIZE);
    bufferevent_setwatermark(relay_bev, EV_READ | EV_WRITE, 0, TRANSOCKS_BUFSIZE);
    bufferevent_setcb(client_bev, transocks_client_readcb, transocks_client_writecb,
                      transocks_client_eventcb, pclient);
    bufferevent_setcb(relay_bev, transocks_relay_readcb, transocks_relay_writecb,
                      transocks_relay_eventcb, pclient);

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

transocks_pump transocks_pump_bufferpump_ops = {
        .name = PUMPMETHOD_BUFFER,
        .start_pump_fn = transocks_bufferpump_start_pump,
        .free_pump_fn = transocks_bufferpump_free,
        .dump_info_fn = transocks_bufferpump_dump_info
};