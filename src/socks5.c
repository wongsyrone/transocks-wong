//
// Created by wong on 10/28/18.
//

#include "socks5.h"

static void relay_connect_eventcb(struct bufferevent *bev, short bevs, void *userArg) {
    transocks_client *client = (transocks_client *)userArg;
    if (bevs & BEV_EVENT_CONNECTED) {
        /* We're connected Ordinarily we'd do
           something here, like start reading or writing. */
        client->client_state =
    } else if (bevs & BEV_EVENT_ERROR) {
        /* An error occured while connecting. */
        LOGE("connect relay error");
        transocks_client_free(&client);
    }
}

void transocks_start_connect_relay(transocks_client *client) {
    transocks_global_env *env = client->global_env;
    int relay_fd = socket(env->relayAddr->ss_family, SOCK_STREAM, 0);
    if (relay_fd < 0) {
        LOGE_ERRNO("fail to create socket");
        goto fail;
    }
    if (setnonblocking(relay_fd, true) != 0) {
        LOGE("fail to set non-blocking");
        goto fail;
    }
    if (apply_tcp_keepalive(relay_fd) != 0) {
        LOGE("fail to set TCP keepalive");
        goto fail;
    }

    client->relayFd = relay_fd;

    // create relay bev
    struct bufferevent *relay_bev = bufferevent_socket_new(env->eventBaseLoop,
            relay_fd, BEV_OPT_CLOSE_ON_FREE);

    if (relay_bev == NULL) {
        LOGE("bufferevent_socket_new");
        goto fail;
    }


    bufferevent_setcb(relay_bev, NULL, NULL, relay_connect_eventcb, client);
    if (bufferevent_socket_connect(relay_bev, (const struct sockaddr *)(env->relayAddr), env->relayAddrLen) != 0) {
        LOGE("fail to connect to relay");
        goto fail;
    }

    fail:
    if (relay_fd > -1) close(relay_fd);
    if (relay_bev!=NULL) bufferevent_free(relay_bev);
    transocks_client_free(&client);

}
