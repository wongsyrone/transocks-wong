//
// Created by wong on 10/14/19.
//

#include "listener-udp.h"
#include "netutils.h"

static void udp_listener_cb(evutil_socket_t, short, void *);

static void udp_listener_cb(evutil_socket_t fd, short events, void *userArg) {
    TRANSOCKS_UNUSED(fd);
    TRANSOCKS_UNUSED(events);
    transocks_global_env *env = (transocks_global_env *) userArg;
    int clientFd;
    struct sockaddr_storage acceptedSrcSockAddr;
    socklen_t acceptedSrcSockLen = sizeof(struct sockaddr_storage);


    transocks_client *pclient = transocks_client_new(env);

    if (pclient == NULL) {
        LOGE("fail to allocate memory");
        goto freeClient;
    }

    char srcaddrstr[TRANSOCKS_INET_ADDRPORTSTRLEN];
    char bindaddrstr[TRANSOCKS_INET_ADDRPORTSTRLEN];
    char destaddrstr[TRANSOCKS_INET_ADDRPORTSTRLEN];
    generate_sockaddr_readable_str(bindaddrstr, TRANSOCKS_INET_ADDRPORTSTRLEN,
                                   (const struct sockaddr *) env->tcpBindAddr, env->tcpBindAddrLen);

    memcpy((void *) (pclient->clientAddr), (void *) (&acceptedSrcSockAddr), acceptedSrcSockLen);
    generate_sockaddr_readable_str(srcaddrstr, TRANSOCKS_INET_ADDRPORTSTRLEN,
                                   (const struct sockaddr *) (&acceptedSrcSockAddr), acceptedSrcSockLen);


    if (get_orig_dst_tcp_redirect(clientFd, pclient->destAddr, &pclient->destAddrLen) != 0) {
        LOGE("fail to get origdestaddr, close fd");
        goto freeClient;
    }

    generate_sockaddr_readable_str(destaddrstr, TRANSOCKS_INET_ADDRPORTSTRLEN,
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

int udp_listener_init(transocks_global_env *env) {
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

    env->udpListener = tr_malloc(sizeof(struct transocks_listener_t));
    if (env->udpListener == NULL) {
        LOGE("fail to allocate memory");
        goto freeListener;
    }
    env->udpListener->listenerFd = fd;
    env->udpListener->listenerEvent = event_new(env->eventBaseLoop, fd,
                                                EV_READ | EV_PERSIST, udp_listener_cb , env);
    if (env->udpListener->listenerEvent == NULL) {
        LOGE("fail to allocate memory");
        goto freeListener;
    }
    if (event_add(env->udpListener->listenerEvent, NULL) != 0) {
        LOGE("fail to add listener_ev");
        goto freeListener;
    }

    return 0;

    freeListener:
    udp_listener_destory(env);

    closeFd:
    TRANSOCKS_CLOSE(fd);

    return -1;
}

void udp_listener_destory(transocks_global_env *env) {
    if (env == NULL) return;
    if (env->udpListener != NULL) {
        if (env->udpListener->listenerEvent != NULL) {
            event_del(env->udpListener->listenerEvent);
            TRANSOCKS_FREE(event_free, env->udpListener->listenerEvent);
        }
        TRANSOCKS_CLOSE(env->udpListener->listenerFd);
        TRANSOCKS_FREE(tr_free, env->udpListener);
    }
}