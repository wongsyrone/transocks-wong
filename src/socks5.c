//
// Created by wong on 10/28/18.
//

#include "socks5.h"
#include "pump.h"

static void socks_handshake_stage_errcb(struct bufferevent *, short, void *);

static void relay_onconnect_eventcb(struct bufferevent *, short, void *);

static void socks_on_server_connect_reply_readcb(struct bufferevent *, void *);

static void socks_send_connect_request(transocks_client **);

static void socks_on_server_selected_method_readcb(struct bufferevent *, void *);

static void socks_send_method_selection(transocks_client **);


/*
 * We assume socks5 server are listening on the loopback interface
 * thus treats any EOF as error
 */
static void socks_handshake_stage_errcb(struct bufferevent *bev, short bevs, void *userArg) {
    transocks_client **ppclient = (transocks_client **) userArg;
    if ((bevs & BEV_EVENT_EOF) == BEV_EVENT_EOF
        || (bevs & BEV_EVENT_ERROR) == BEV_EVENT_ERROR) {
        int err = EVUTIL_SOCKET_ERROR();
        LOGE("socks error %d (%s)", err, evutil_socket_error_to_string(err));
        transocks_client_free(ppclient);
    }
}

static void socks_on_server_connect_reply_readcb(struct bufferevent *bev, void *userArg) {
    transocks_client **ppclient = (transocks_client **) userArg;
    struct evbuffer *input = bufferevent_get_input(bev);
    size_t input_read_size = evbuffer_get_length(input);
    if (input_read_size < 10) {
        LOGE("no enough data");
        goto freeClient;
    }
    unsigned char *mem = evbuffer_pullup(input, input_read_size);
    struct socks_response_header *preshead = (struct socks_response_header *) mem;
    if (preshead->ver != SOCKS5_VERSION) {
        LOGE("not a socks5 server");
        goto freeClient;
    }
    if (preshead->rep != SOCKS5_REP_SUCCEEDED) {
        LOGE("socks connect rep %x", preshead->rep);
        goto freeClient;
    }

    switch (preshead->atyp) {
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
    (*ppclient)->client_state = client_socks5_finish_handshake;

    if (transocks_start_pump(ppclient) != 0)
        goto freeClient;


    return;

    freeClient:
    transocks_client_free(ppclient);
}

static void socks_send_connect_request(transocks_client **ppclient) {
    transocks_client *pclient = *ppclient;
    struct bufferevent *relay_bev = pclient->relay_bev;
    struct socks_request_ipv4 req_ip4;
    struct socks_request_ipv6 req_ip6;
    struct sockaddr_in *sa_ip4;
    struct sockaddr_in6 *sa_ip6;


    switch (pclient->destaddr->ss_family) {
        case AF_INET:
            sa_ip4 = (struct sockaddr_in *) (pclient->destaddr);
            req_ip4.ver = SOCKS5_VERSION;
            req_ip4.cmd = SOCKS5_CMD_CONNECT;
            req_ip4.rsv = 0x00;
            req_ip4.atyp = SOCKS5_ATYP_IPV4;
            req_ip4.port = sa_ip4->sin_port;
            memcpy(&(req_ip4.addr), &(sa_ip4->sin_addr), sizeof(struct in_addr));
            assert(sizeof(req_ip4) == 6 + 4);
            bufferevent_write(relay_bev, (const void *) &req_ip4, sizeof(req_ip4));
            break;
        case AF_INET6:
            sa_ip6 = (struct sockaddr_in6 *) (pclient->destaddr);
            req_ip6.ver = SOCKS5_VERSION;
            req_ip6.cmd = SOCKS5_CMD_CONNECT;
            req_ip6.rsv = 0x00;
            req_ip6.atyp = SOCKS5_ATYP_IPV6;
            req_ip6.port = sa_ip6->sin6_port;
            memcpy(&(req_ip6.addr), &(sa_ip6->sin6_addr), sizeof(struct in6_addr));
            assert(sizeof(req_ip6) == 6 + 16);
            bufferevent_write(relay_bev, (const void *) &req_ip6, sizeof(req_ip6));
            break;
        default:
            LOGE("unknown ss_family");
            goto freeClient;
    }
    // VER + REP + RSV + ATYP + 4-byte ipv4 ADDR + 2-byte port
    // (should not reply with domain, iptables has ip address only)
    bufferevent_setwatermark(relay_bev, EV_READ, 10, TRANSOCKS_BUFSIZE);
    bufferevent_setcb(relay_bev, socks_on_server_connect_reply_readcb, NULL, socks_handshake_stage_errcb, ppclient);
    bufferevent_disable(relay_bev, EV_WRITE);
    bufferevent_enable(relay_bev, EV_READ);

    return;

    freeClient:
    transocks_client_free(ppclient);
}

static void socks_on_server_selected_method_readcb(struct bufferevent *bev, void *userArg) {
    transocks_client **ppclient = (transocks_client **) userArg;
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

    socks_send_connect_request(ppclient);
    return;

    freeClient:
    transocks_client_free(ppclient);
}

static void socks_send_method_selection(transocks_client **ppclient) {
    //wait for 2 bytes socks server selected method
    transocks_client *pclient = *ppclient;
    struct bufferevent *relay_bev = pclient->relay_bev;
    struct socks_method_select_request req;
    req.ver = SOCKS5_VERSION;
    req.nmethods = 0x01;
    req.methods[0] = SOCKS5_METHOD_NOAUTH;
    bufferevent_setwatermark(relay_bev, EV_READ, 2, TRANSOCKS_BUFSIZE);
    bufferevent_write(relay_bev, (const void *) &req, sizeof(struct socks_method_select_request));
    bufferevent_setcb(relay_bev, socks_on_server_selected_method_readcb, NULL, socks_handshake_stage_errcb, ppclient);
    bufferevent_disable(relay_bev, EV_WRITE);
    bufferevent_enable(relay_bev, EV_READ);
}

static void relay_onconnect_eventcb(struct bufferevent *bev, short bevs, void *userArg) {
    transocks_client **ppclient = (transocks_client **) userArg;
    transocks_client *pclient = *ppclient;
    if (bevs & BEV_EVENT_CONNECTED) {
        /* We're connected */
        pclient->client_state = client_relay_connected;
        socks_send_method_selection(ppclient);
        return;
    } else if (bevs & BEV_EVENT_ERROR) {
        /* An error occured while connecting. */
        int err = EVUTIL_SOCKET_ERROR();
        LOGE("connect relay %d (%s)", err, evutil_socket_error_to_string(err));
        transocks_client_free(ppclient);
    }
}

void transocks_start_connect_relay(transocks_client **ppclient) {
    transocks_global_env *env = (*ppclient)->global_env;
    int relay_fd = socket(env->relayAddr->ss_family, SOCK_STREAM, 0);
    if (relay_fd < 0) {
        LOGE_ERRNO("fail to create socket");
        goto freeClient;
    }
    if (setnonblocking(relay_fd, true) != 0) {
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

    (*ppclient)->relayFd = relay_fd;

    // create relay bev
    struct bufferevent *relay_bev = bufferevent_socket_new(env->eventBaseLoop,
                                                           relay_fd, BEV_OPT_CLOSE_ON_FREE);

    if (relay_bev == NULL) {
        LOGE("bufferevent_socket_new");
        goto closeFd;
    }
    (*ppclient)->relay_bev = relay_bev;

    bufferevent_setcb(relay_bev, NULL, NULL, relay_onconnect_eventcb, ppclient);
    if (bufferevent_socket_connect(relay_bev, (const struct sockaddr *) (env->relayAddr), env->relayAddrLen) != 0) {
        LOGE("fail to connect to relay");
        goto freeBev;
    }


    return;

    freeBev:
    bufferevent_free(relay_bev);

    closeFd:
    close(relay_fd);

    freeClient:
    transocks_client_free(ppclient);
}
