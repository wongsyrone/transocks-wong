//
// Created by wong on 10/30/18.
//

#include "bufferpump.h"

// TODO

transocks_pump transocks_bufferpump_ops;







static int transocks_bufferpump_start_pump(transocks_client **ppclient) {
    int err;
    struct bufferevent *client_bev = (*ppclient)->client_bev;
    struct bufferevent *relay_bev = (*ppclient)->relay_bev;
    bufferevent_setwatermark(client_bev, EV_READ | EV_WRITE, 0, TRANSOCKS_BUFSIZE);
    bufferevent_setwatermark(relay_bev, EV_READ | EV_WRITE, 0, TRANSOCKS_BUFSIZE);
    bufferevent_setcb(client_bev, transocks_client_readcb, transocks_client_writecb, transocks_client_eventcb, ppclient);
    bufferevent_setcb(relay_bev, transocks_relay_readcb, transocks_relay_writecb, transocks_relay_eventcb, ppclient);

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