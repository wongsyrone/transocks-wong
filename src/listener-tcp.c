//
// Created by wong on 10/27/18.
//

#include "listener-tcp.h"
#include "netutils.h"

static void tcp_listener_cb(evutil_socket_t, short, void *);

static void tcp_listener_cb(evutil_socket_t fd, short events, void *userArg) {
    TRANSOCKS_UNUSED(fd);
    TRANSOCKS_UNUSED(events);
    transocks_global_env *env = (transocks_global_env *) userArg;
    int clientFd;
    struct sockaddr_storage acceptedSrcSockAddr;
    socklen_t acceptedSrcSockLen = sizeof(struct sockaddr_storage);
    clientFd = accept(env->tcpListener->listenerFd,
                      (struct sockaddr *) (&acceptedSrcSockAddr), &acceptedSrcSockLen);
    if (clientFd < 0) {
        if (TRANSOCKS_IS_RETRIABLE(errno)) {
            // wait for next event
            return;
        } else {
            LOGE_ERRNO("accept() err");
            return;
        }
    }
    if (acceptedSrcSockLen == 0) {
        goto freeFd;
    }
    if (apply_non_blocking(clientFd, true) != 0) {
        LOGE("fail to set nonblocking");
        goto freeFd;
    }

    if (apply_tcp_keepalive(clientFd) != 0) {
        LOGE("fail to set TCP keepalive");
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

    char srcaddrstr[TRANSOCKS_INET_ADDRPORTSTRLEN];
    char bindaddrstr[TRANSOCKS_INET_ADDRPORTSTRLEN];
    char destaddrstr[TRANSOCKS_INET_ADDRPORTSTRLEN];
    generate_sockaddr_port_str(bindaddrstr, TRANSOCKS_INET_ADDRPORTSTRLEN,
                               (const struct sockaddr *) env->tcpBindAddr, env->tcpBindAddrLen);

    pclient->clientAddrLen = acceptedSrcSockLen;
    memcpy((void *) (pclient->clientAddr), (void *) (&acceptedSrcSockAddr), acceptedSrcSockLen);
    generate_sockaddr_port_str(srcaddrstr, TRANSOCKS_INET_ADDRPORTSTRLEN,
                               (const struct sockaddr *) (&acceptedSrcSockAddr), acceptedSrcSockLen);


    if (get_orig_dst_tcp_redirect(clientFd, pclient->destAddr, &pclient->destAddrLen) != 0) {
        LOGE("fail to get origdestaddr, close fd");
        goto freeClient;
    }

    generate_sockaddr_port_str(destaddrstr, TRANSOCKS_INET_ADDRPORTSTRLEN,
                               (const struct sockaddr *) (pclient->destAddr), pclient->destAddrLen);


    LOGI("%s accept a conn %s -> %s", bindaddrstr, srcaddrstr, destaddrstr);
    pclient->clientFd = clientFd;
    pclient->globalEnv = env;

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

    pclient->clientBufferEvent = client_bev;

    list_add(&(pclient->dLinkListEntry), &(env->clientDlinkList));

    // start connecting SOCKS5 relay
    transocks_on_client_received(pclient);
    return;

    freeBev:
    TRANSOCKS_FREE(bufferevent_free, client_bev);

    freeClient:
    TRANSOCKS_FREE(transocks_client_free, pclient);

    freeFd:
    TRANSOCKS_CLOSE(clientFd);
}

int tcp_listener_init(transocks_global_env *env) {
    int on = 1;
    int err;
    int fd;

    // TODO

    err = bind(fd, (struct sockaddr *) (env->tcpBindAddr), env->tcpBindAddrLen);
    if (err != 0) {
        LOGE_ERRNO("fail to bind");
        goto closeFd;
    }

    err = listen(fd, SOMAXCONN);
    if (err != 0) {
        LOGE_ERRNO("fail to listen");
        goto closeFd;
    }

    env->tcpListener = tr_malloc(sizeof(struct transocks_listener_t));
    if (env->tcpListener == NULL) {
        LOGE("fail to allocate memory");
        goto freeListener;
    }
    env->tcpListener->listenerFd = fd;
    env->tcpListener->listenerEvent = event_new(env->eventBaseLoop, fd,
                                                EV_READ | EV_PERSIST, tcp_listener_cb, env);
    if (env->tcpListener->listenerEvent == NULL) {
        LOGE("fail to allocate memory");
        goto freeListener;
    }
    if (event_add(env->tcpListener->listenerEvent, NULL) != 0) {
        LOGE("fail to add listener_ev");
        goto freeListener;
    }

    return 0;

    freeListener:
    tcp_listener_destory(env);

    closeFd:
    TRANSOCKS_CLOSE(fd);

    return -1;
}

void tcp_listener_destory(transocks_global_env *env) {
    if (env == NULL) return;
    if (env->tcpListener != NULL) {
        if (env->tcpListener->listenerEvent != NULL) {
            event_del(env->tcpListener->listenerEvent);
            TRANSOCKS_FREE(event_free, env->tcpListener->listenerEvent);
        }
        TRANSOCKS_CLOSE(env->tcpListener->listenerFd);
        TRANSOCKS_FREE(tr_free, env->tcpListener);
    }
}