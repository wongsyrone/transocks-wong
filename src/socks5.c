//
// Created by wong on 10/28/18.
//

#include "socks5.h"
#include "pump.h"
#include "netutils.h"

static void socks5_timeout_cb(evutil_socket_t, short, void *);

static void socks_handshake_stage_errcb(struct bufferevent *, short, void *);

static void relay_onconnect_eventcb(struct bufferevent *, short, void *);

static void socks_on_server_connect_reply_readcb(struct bufferevent *, void *);

static void socks_send_connect_request(transocks_client *);

static void socks_on_server_selected_method_readcb(struct bufferevent *, void *);

static void socks_send_method_selection(transocks_client *);


static struct timeval socks5_timeout_tv = {
        .tv_sec = 60,
        .tv_usec = 0
};

static struct timeval udp_timeout_tv = {
        .tv_sec = 300,
        .tv_usec = 0
};

static char *socks5_error_str[] = {
        [SOCKS5_REP_SUCCEEDED] = "succeeded",
        [SOCKS5_REP_GENERAL] = "general SOCKS server failure",
        [SOCKS5_REP_CONN_DISALLOWED] = "connection not allowed by ruleset",
        [SOCKS5_REP_NETWORK_UNREACHABLE] = "Network unreachable",
        [SOCKS5_REP_HOST_UNREACHABLE] = "Host unreachable",
        [SOCKS5_REP_CONN_REFUSED] = "Connection refused",
        [SOCKS5_REP_TTL_EXPIRED] = "TTL expired",
        [SOCKS5_REP_CMD_NOT_SUPPORTED] = "Command not supported",
        [SOCKS5_REP_ADDRTYPE_NOT_SUPPORTED] = "Address type not supported",
        [SOCKS5_REP_UNASSIGNED] = "unassigned",
};

/*
 * We assume socks5 server are listening on the loopback interface
 * thus treats any EOF as error
 */
static void socks_handshake_stage_errcb(struct bufferevent *bev, short bevs, void *userArg) {
    TRANSOCKS_UNUSED(bev);
    transocks_client *pclient = (transocks_client *) userArg;
    evtimer_del(pclient->handshakeTimeoutEvent);
    if (TRANSOCKS_CHKBIT(bevs, BEV_EVENT_EOF)
        || TRANSOCKS_CHKBIT(bevs, BEV_EVENT_ERROR)) {
        int err = EVUTIL_SOCKET_ERROR();
        LOGE("socks error %d (%s)", err, evutil_socket_error_to_string(err));
        TRANSOCKS_FREE(transocks_client_free, pclient);
    }
}

static void socks_on_server_connect_reply_readcb(struct bufferevent *bev, void *userArg) {
    TRANSOCKS_UNUSED(bev);
    LOGD("enter");
    transocks_client *pClient = (transocks_client *) userArg;
    evtimer_del(pClient->handshakeTimeoutEvent);
    struct bufferevent *relay_bev = pClient->relayBufferEvent;
    struct evbuffer *input = bufferevent_get_input(relay_bev);
    size_t input_read_size = evbuffer_get_length(input);
    if (input_read_size < 10) {
        LOGE("no enough data");
        goto freeClient;
    }
    unsigned char *mem = evbuffer_pullup(input, input_read_size);
    struct socks_response_header *pSocksResponseHeader = (struct socks_response_header *) mem;
    if (pSocksResponseHeader->ver != SOCKS5_VERSION) {
        LOGE("not a socks5 server");
        goto freeClient;
    }
    if (pSocksResponseHeader->rep != SOCKS5_REP_SUCCEEDED) {
        LOGE("socks connect rep (%x): %s", pSocksResponseHeader->rep, socks5_error_str[pSocksResponseHeader->rep]);
        goto freeClient;
    }

    switch (pSocksResponseHeader->atyp) {
        case SOCKS5_ATYP_IPV4:
            evbuffer_drain(input, 10);
            break;
        case SOCKS5_ATYP_IPV6:
            evbuffer_drain(input, 22);
            break;
        default:
            LOGE("atyp domain should not happen");
            goto freeClient;
    }
    pClient->clientState = client_socks5_finish_handshake;
    LOGD("before pump, %d", (int) evbuffer_get_length(input));
    // disable bufferevent until pump enable it again
    bufferevent_disable(relay_bev, EV_READ | EV_WRITE);

    if (transocks_start_pump(pClient) != 0)
        goto freeClient;


    return;

    freeClient:
    TRANSOCKS_FREE(transocks_client_free, pClient);
}

static void socks_send_connect_request(transocks_client *pclient) {
    LOGD("enter");
    struct bufferevent *relay_bev = pclient->relayBufferEvent;
    struct socks_request_ipv4 *req_ip4 = NULL;
    struct socks_request_ipv6 *req_ip6 = NULL;
    struct sockaddr_in *sa_ip4 = NULL;
    struct sockaddr_in6 *sa_ip6 = NULL;
    if (pclient->destAddr->ss_family == AF_INET) {
        req_ip4 = tr_malloc(sizeof(struct socks_request_ipv4));
        if (req_ip4 == NULL) goto freeClient;
        sa_ip4 = (struct sockaddr_in *) (pclient->destAddr);
        req_ip4->ver = SOCKS5_VERSION;
        req_ip4->cmd = SOCKS5_CMD_CONNECT;
        req_ip4->rsv = 0x00;
        req_ip4->atyp = SOCKS5_ATYP_IPV4;
        req_ip4->port = sa_ip4->sin_port;
        assert(sizeof(struct socks_request_ipv4) == 6 + 4);
        memcpy(&(req_ip4->addr), &(sa_ip4->sin_addr), sizeof(struct in_addr));
        dump_data("socks_send_connect_request_v4", (char *) req_ip4, sizeof(struct socks_request_ipv4));
        bufferevent_write(relay_bev, (const void *) req_ip4, sizeof(struct socks_request_ipv4));
        TRANSOCKS_FREE(tr_free, req_ip4);
    } else if (pclient->destAddr->ss_family == AF_INET6) {
        req_ip6 = tr_malloc(sizeof(struct socks_request_ipv6));
        if (req_ip6 == NULL) goto freeClient;
        sa_ip6 = (struct sockaddr_in6 *) (pclient->destAddr);
        req_ip6->ver = SOCKS5_VERSION;
        req_ip6->cmd = SOCKS5_CMD_CONNECT;
        req_ip6->rsv = 0x00;
        req_ip6->atyp = SOCKS5_ATYP_IPV6;
        req_ip6->port = sa_ip6->sin6_port;
        assert(sizeof(struct socks_request_ipv6) == 6 + 16);
        memcpy(&(req_ip6->addr), &(sa_ip6->sin6_addr), sizeof(struct in6_addr));
        dump_data("socks_send_connect_request_v6", (char *) req_ip6, sizeof(struct socks_request_ipv6));
        bufferevent_write(relay_bev, (const void *) req_ip6, sizeof(struct socks_request_ipv6));
        TRANSOCKS_FREE(tr_free, req_ip6);
    } else {
        LOGE("unknown ss_family");
        goto freeClient;
    }

    // VER + REP + RSV + ATYP + 4-byte ipv4 ADDR + 2-byte port
    // (should not reply with domain, iptables has ip address only)
    bufferevent_setwatermark(relay_bev, EV_READ, 10, TRANSOCKS_BUFSIZE);
    bufferevent_setcb(relay_bev, socks_on_server_connect_reply_readcb, NULL, socks_handshake_stage_errcb, pclient);
    bufferevent_enable(relay_bev, EV_READ);

    transocks_client_set_timeout(pclient->globalEnv->eventBaseLoop, pclient->handshakeTimeoutEvent,
            &socks5_timeout_tv, socks5_timeout_cb, pclient);

    return;

    freeClient:
    TRANSOCKS_FREE(transocks_client_free, pclient);
}

static void socks_on_server_selected_method_readcb(struct bufferevent *bev, void *userArg) {
    LOGD("enter");
    transocks_client *pclient = (transocks_client *) userArg;
    evtimer_del(pclient->handshakeTimeoutEvent);
    struct evbuffer *input = bufferevent_get_input(bev);
    size_t input_read_size = evbuffer_get_length(input);
    if (input_read_size < 2) {
        LOGE("not enough data");
        goto freeClient;
    }
    unsigned char *mem = evbuffer_pullup(input, input_read_size);
    struct socks_method_select_response *pres = (struct socks_method_select_response *) mem;
    if (pres->ver != SOCKS5_VERSION) {
        LOGE("not a socks5 server");
        goto freeClient;
    }
    if (pres->method != SOCKS5_METHOD_NOAUTH) {
        LOGE("unsupported socks5 auth method %d", pres->method);
        goto freeClient;
    }
    if (evbuffer_drain(input, 2) != 0) {
        LOGE("fail to drain 2bytes server_selected_method");
        goto freeClient;
    }
    if (evbuffer_get_length(bufferevent_get_input(bev)) != 0) {
        LOGE("still has data in the relay bev input buffer");
        goto freeClient;
    }
    socks_send_connect_request(pclient);
    return;

    freeClient:
    TRANSOCKS_FREE(transocks_client_free, pclient);
}

static void socks_send_method_selection(transocks_client *pclient) {
    LOGD("enter");
    //wait for 2 bytes socks server selected method
    struct bufferevent *relay_bev = pclient->relayBufferEvent;
    struct socks_method_select_request req;
    req.ver = SOCKS5_VERSION;
    req.nmethods = 0x01;
    req.methods[0] = SOCKS5_METHOD_NOAUTH;
    bufferevent_setwatermark(relay_bev, EV_READ, 2, TRANSOCKS_BUFSIZE);
    bufferevent_write(relay_bev, (const void *) &req, sizeof(struct socks_method_select_request));
    bufferevent_setcb(relay_bev, socks_on_server_selected_method_readcb, NULL, socks_handshake_stage_errcb, pclient);
    bufferevent_enable(relay_bev, EV_READ);

    transocks_client_set_timeout(pclient->globalEnv->eventBaseLoop, pclient->handshakeTimeoutEvent,
                                 &socks5_timeout_tv, socks5_timeout_cb, pclient);
}

static void relay_onconnect_eventcb(struct bufferevent *bev, short bevs, void *userArg) {
    TRANSOCKS_UNUSED(bev);
    transocks_client *pclient = (transocks_client *) userArg;
    evtimer_del(pclient->handshakeTimeoutEvent);
    if (TRANSOCKS_CHKBIT(bevs, BEV_EVENT_CONNECTED)) {
        /* We're connected */
        pclient->clientState = client_relay_connected;
        socks_send_method_selection(pclient);
        return;
    }

    if (TRANSOCKS_CHKBIT(bevs, BEV_EVENT_ERROR)) {
        /* An error occured while connecting. */
        int err = EVUTIL_SOCKET_ERROR();
        LOGE("connect relay %d (%s)", err, evutil_socket_error_to_string(err));
        TRANSOCKS_FREE(transocks_client_free, pclient);
    }
}

static void socks5_timeout_cb(evutil_socket_t fd, short events, void *userArg) {
    TRANSOCKS_UNUSED(fd);
    TRANSOCKS_UNUSED(events);
    transocks_client *pclient = (transocks_client *) userArg;
    LOGE("timed out");
    TRANSOCKS_FREE(transocks_client_free, pclient);
}

void transocks_start_connect_relay(transocks_client *pClient) {
    LOGD("enter");
    transocks_global_env *env = pClient->globalEnv;
    int relay_fd = socket(env->relayAddr->ss_family, SOCK_STREAM, 0);
    if (relay_fd < 0) {
        LOGE_ERRNO("fail to create socket");
        goto freeClient;
    }
    if (apply_non_blocking(relay_fd, true) != 0) {
        LOGE("fail to set non-blocking");
        goto closeFd;
    }
    if (apply_tcp_keepalive(relay_fd) != 0) {
        LOGE("fail to set TCP keepalive");
        goto closeFd;
    }
    if (apply_tcp_nodelay(relay_fd) != 0) {
        LOGE("fail to set TCP nodelay");
        goto closeFd;
    }

    pClient->relayFd = relay_fd;

    // create relay bev
    struct bufferevent *relay_bev = bufferevent_socket_new(env->eventBaseLoop,
                                                           relay_fd, BEV_OPT_CLOSE_ON_FREE);

    if (relay_bev == NULL) {
        LOGE("bufferevent_socket_new");
        goto closeFd;
    }
    pClient->relayBufferEvent = relay_bev;

    bufferevent_setcb(pClient->relayBufferEvent, NULL, NULL, relay_onconnect_eventcb, pClient);
    bufferevent_enable(pClient->relayBufferEvent, EV_WRITE);

    transocks_client_set_timeout(pClient->globalEnv->eventBaseLoop, pClient->handshakeTimeoutEvent,
                                 &socks5_timeout_tv, socks5_timeout_cb, pClient);
    if (bufferevent_socket_connect(pClient->relayBufferEvent,
                                   (const struct sockaddr *) (env->relayAddr), env->relayAddrLen) != 0) {
        LOGE("fail to connect to relay");
        goto freeBev;
    }


    return;

    freeBev:
    TRANSOCKS_FREE(bufferevent_free, relay_bev);

    closeFd:
    TRANSOCKS_CLOSE(relay_fd);

    freeClient:
    TRANSOCKS_FREE(transocks_client_free, pClient);
}
