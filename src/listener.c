//
// Created by wong on 10/27/18.
//

#include "listener.h"

static void listener_errcb(struct evconnlistener *, void *);

static void listener_cb(struct evconnlistener *, evutil_socket_t,
                        struct sockaddr *, int, void *);


static void listener_errcb(struct evconnlistener *listener, void *userArg) {
    TRANSOCKS_UNUSED(listener);
    transocks_global_env *env = (transocks_global_env *) userArg;
    int err = EVUTIL_SOCKET_ERROR();
    LOGE("listener accept() %d (%s)", err, evutil_socket_error_to_string(err));
    if (event_base_loopbreak(env->eventBaseLoop) != 0)
        LOGE("fail to event_base_loopbreak");
}

static void listener_cb(struct evconnlistener *listener, evutil_socket_t clientFd,
                        struct sockaddr *srcAddr, int srcSockLen, void *userArg) {
    TRANSOCKS_UNUSED(listener);
    transocks_global_env *env = (transocks_global_env *) userArg;
    socklen_t typedSrcSockLen = (socklen_t) srcSockLen;


    if (setnonblocking(clientFd, true) != 0) {
        LOGE("fail to set nonblocking");
        goto freeFd;
    }

    if (apply_tcp_nodelay(clientFd) != 0) {
        LOGE("fail to set TCP nodelay");
        goto freeFd;
    }

    transocks_client *pclient = transocks_client_new(env);

    if (pclient == NULL) {
        LOGE("fail to allocate memory");
        goto freeClient;
    }

    char srcaddrstr[TRANSOCKS_INET_ADDRPORTSTRLEN] = {0};
    char bindaddrstr[TRANSOCKS_INET_ADDRPORTSTRLEN] = {0};
    char destaddrstr[TRANSOCKS_INET_ADDRPORTSTRLEN] = {0};
    generate_sockaddr_port_str(bindaddrstr, TRANSOCKS_INET_ADDRPORTSTRLEN,
                               (const struct sockaddr *) env->bindAddr, env->bindAddrLen);

    pclient->clientaddrlen = typedSrcSockLen;
    memcpy((void *) (pclient->clientaddr), (void *) srcAddr, typedSrcSockLen);
    generate_sockaddr_port_str(srcaddrstr, TRANSOCKS_INET_ADDRPORTSTRLEN,
                               (const struct sockaddr *) srcAddr, typedSrcSockLen);


    if (getorigdst(clientFd, pclient->destaddr, &pclient->destaddrlen) != 0) {
        LOGE("fail to get origdestaddr, close fd");
        goto freeClient;
    }

    generate_sockaddr_port_str(destaddrstr, TRANSOCKS_INET_ADDRPORTSTRLEN,
                               (const struct sockaddr *) (pclient->destaddr), pclient->destaddrlen);


    LOGI("%s accept a conn %s -> %s", bindaddrstr, srcaddrstr, destaddrstr);
    pclient->clientFd = clientFd;
    pclient->global_env = env;

    struct bufferevent *client_bev = bufferevent_socket_new(env->eventBaseLoop,
                                                            -1, BEV_OPT_CLOSE_ON_FREE);

    if (client_bev == NULL) {
        LOGE("bufferevent_socket_new");
        goto freeBev;
    }

    // should not read any data from client now, until handshake is finished
    if (bufferevent_disable(client_bev, EV_READ) != 0) {
        LOGE("bufferevent_disable read");
        goto freeBev;
    }
    if (bufferevent_setfd(client_bev, clientFd) != 0) {
        LOGE("bufferevent_setfd client");
        goto freeBev;
    }

    pclient->client_bev = client_bev;

    list_add(&(pclient->dlinklistentry), &(env->clientDlinkList));

    // start connecting SOCKS5 relay
    transocks_start_connect_relay(pclient);
    return;

    freeBev:
    TRANSOCKS_FREE(bufferevent_free, client_bev);

    freeClient:
    TRANSOCKS_FREE(transocks_client_free, pclient);

    freeFd:
    TRANSOCKS_CLOSE(clientFd);
}

int listener_init(transocks_global_env *env) {
    int on = 1;
    int err;
    int fd = socket(env->bindAddr->ss_family, SOCK_STREAM, 0);
    if (fd < 0) {
        LOGE_ERRNO("fail to create socket");
        return -1;
    }
    err = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    if (err != 0) {
        LOGE_ERRNO("fail to set SO_REUSEADDR");
        goto closeFd;
    }
    err = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
    if (err != 0) {
        LOGE_ERRNO("fail to set SO_REUSEPORT");
        goto closeFd;
    }
    if (setnonblocking(fd, true) != 0) {
        LOGE("fail to set non-blocking");
        goto closeFd;
    }
    // ensure we accept both ipv4 and ipv6
    if (env->bindAddr->ss_family == AF_INET6) {
        if (apply_ipv6only(fd, 0) != 0) {
            LOGE("fail to disable IPV6_V6ONLY");
            goto closeFd;
        }
    }
    // bind
    err = bind(fd, (struct sockaddr *) env->bindAddr, env->bindAddrLen);
    if (err != 0) {
        LOGE_ERRNO("fail to bind");
        goto closeFd;
    }
    env->listener = evconnlistener_new(env->eventBaseLoop, listener_cb, env,
                                       LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, SOMAXCONN, fd);
    if (env->listener == NULL) {
        LOGE("fail to create listener");
        goto closeFd;
    }
    evconnlistener_set_error_cb(env->listener, listener_errcb);

    return 0;

    closeFd:
    TRANSOCKS_CLOSE(fd);

    return -1;
}

void listener_deinit(transocks_global_env *env) {
    if (env == NULL) return;
    if (env->listener != NULL) {
        evconnlistener_disable(env->listener);
        TRANSOCKS_FREE(evconnlistener_free, env->listener);
    }
}