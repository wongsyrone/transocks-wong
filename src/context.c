//
// Created by wong on 10/25/18.
//

#include "context.h"
#include "listener-tcp.h"
#include "signal.h"
#include "pump.h"
#include "netutils.h"

static char *transocks_client_state_str[] = {
        [client_new] = "client_new",
        [client_relay_connected] = "client_relay_connected",
        [client_socks5_finish_handshake] = "client_socks5_finish_handshake",
        [client_pumping_data] = "client_pumping_data",
        [client_INVALID] = "client_INVALID"
};

/*
 * context structure utility function strategy
 * only init essential member for the current layer
 * only free member directly created and call inner layer free function
 */

transocks_global_env *transocks_global_env_new(void) {
    struct transocks_global_env_t *env =
            tr_calloc(1, sizeof(struct transocks_global_env_t));
    if (env == NULL) {
        LOGE("fail to allocate memory");
        goto fail;
    }
    env->pumpMethodName = NULL;
    env->transparentMethodName = NULL;
    env->tcpBindAddr = tr_calloc(1, sizeof(struct sockaddr_storage));

    env->eventBaseLoop = event_base_new();
    if (env->eventBaseLoop == NULL) {
        LOGE("fail to allocate event_base");
        goto fail;
    }

    INIT_LIST_HEAD(&(env->clientDlinkList));

    return env;

    fail:
    TRANSOCKS_FREE(tr_free, env->tcpBindAddr);
    TRANSOCKS_FREE(tr_free, env->relayAddr);
    TRANSOCKS_FREE(event_base_free, env->eventBaseLoop);
    TRANSOCKS_FREE(tr_free, env);
    return NULL;
}

void transocks_global_env_free(transocks_global_env *pEnv) {
    if (pEnv == NULL) return;

    TRANSOCKS_FREE(tr_free, pEnv->pumpMethodName);
    TRANSOCKS_FREE(tr_free, pEnv->transparentMethodName);
    TRANSOCKS_FREE(tr_free, pEnv->relayAddr);
    TRANSOCKS_FREE(tr_free, pEnv->tcpBindAddr);
    tcp_listener_destory(pEnv);
    signal_deinit(pEnv);

    TRANSOCKS_FREE(event_base_free, pEnv->eventBaseLoop);
    TRANSOCKS_FREE(tr_free, pEnv);
}

transocks_client *transocks_client_new(transocks_global_env *env) {
    transocks_client *client = tr_malloc(sizeof(transocks_client));
    if (client == NULL) {
        LOGE("fail to allocate memory");
        goto fail;
    }
    INIT_LIST_HEAD(&(client->dLinkListEntry));
    client->globalEnv = NULL;
    client->clientAddr = NULL;
    client->destAddr = NULL;
    client->clientBufferEvent = NULL;
    client->relayBufferEvent = NULL;
    client->timeoutEvent = NULL;
    client->userArg = NULL;
    client->isClientShutdownRead = false;
    client->isClientShutdownWrite = false;
    client->isRelayShutdownRead = false;
    client->isRelayShutdownWrite = false;
    client->clientState = client_new;


    client->globalEnv = env;

    return client;

    fail:

    TRANSOCKS_FREE(tr_free, client);
    return NULL;
}

void transocks_client_free(transocks_client *pClient) {
    if (pClient == NULL) return;

    transocks_pump_dump_info_debug(pClient, "free a conn");

    if (pClient->timeoutEvent != NULL) {
        evtimer_del(pClient->timeoutEvent);
    }
    TRANSOCKS_FREE(event_free, pClient->timeoutEvent);

    if (pClient->relayBufferEvent != NULL) {
        bufferevent_disable(pClient->relayBufferEvent, EV_READ | EV_WRITE);
    }
    if (pClient->clientBufferEvent != NULL) {
        bufferevent_disable(pClient->clientBufferEvent, EV_READ | EV_WRITE);
    }

    TRANSOCKS_FREE(bufferevent_free, pClient->relayBufferEvent);
    TRANSOCKS_FREE(bufferevent_free, pClient->clientBufferEvent);


    pClient->clientState = client_INVALID;

    transocks_socket_free(pClient->clientSocket);
    transocks_socket_address_free(pClient->destAddr);

    if (!list_empty(&pClient->dLinkListEntry)) {
        list_del(&(pClient->dLinkListEntry));
    }

    TRANSOCKS_FREE(tr_free, pClient);
}

transocks_socket *transocks_socket_new(void) {
    transocks_socket *ret = tr_calloc(1, sizeof(transocks_socket));
    if (ret == NULL) {
        LOGE("fail to allocate memory");
        goto fail;
    }
    ret->sockfd = TRANSOCKS_INVALID_SOCKET;
    ret->address = transocks_socket_address_new();
    if (ret->address == NULL) {
        goto fail;
    }
    return ret;

    fail:
    transocks_socket_free(ret);
    return NULL;
}

void transocks_socket_free(transocks_socket *psocket) {
    if (psocket == NULL) return;
    if (psocket->sockfd >= 0) {
        TRANSOCKS_SHUTDOWN(psocket->sockfd, SHUT_RDWR);
    }
    TRANSOCKS_CLOSE(psocket->sockfd);

    transocks_socket_address_free(psocket->address);

    TRANSOCKS_FREE(tr_free, psocket);
}

transocks_socket_address *transocks_socket_address_new(void) {
    transocks_socket_address *ret = tr_calloc(1, sizeof(transocks_socket_address));
    if (ret == NULL) {
        LOGE("fail to allocate memory");
        goto fail;
    }
    return ret;

    fail:
    transocks_socket_address_free(ret);
    return NULL;
}

void transocks_socket_address_free(transocks_socket_address *paddress) {
    if (paddress == NULL) return;
    TRANSOCKS_FREE(tr_free, paddress);
}

int transocks_client_set_timeout(struct event_base *eventBase,
                                 struct event *event,
                                 const struct timeval *timeout,
                                 event_callback_fn ev_fn,
                                 void *arg) {
    int ret;
    if (event == NULL) {
        // first time
        event = evtimer_new(eventBase, ev_fn, arg);
        if (event == NULL) {
            LOGE("mem");
            return -1;
        }
        return evtimer_add(event, timeout);
    } else {
        // already allocated, replace existing timeout
        evtimer_del(event);
        ret = evtimer_assign(event, eventBase, ev_fn, arg);
        if (ret != 0) return ret;
        ret = evtimer_add(event, timeout);
        return ret;
    }
}

int transocks_client_remove_timeout(struct event *event,
                                 void *arg) {
    int ret;
    if (event == NULL) {
        // not allocated, no need to remove timeout
        return 1;
    } else {
        // already allocated, replace existing timeout
        evtimer_del(event);
        return 0;
    }
}

void transocks_drop_all_clients(transocks_global_env *env) {
    transocks_client *pclient = NULL, *tmp = NULL;

    list_for_each_entry_safe(pclient, tmp, &(env->clientDlinkList), dLinkListEntry) {
        transocks_pump_dump_info(pclient, "close connection");
        transocks_pump_free(pclient);
    }
}

void transocks_dump_all_client_info(transocks_global_env *env) {
    transocks_client *pclient = NULL;
    int i = 0;
    fprintf(stdout, "transocks-wong connection info:\n");
    list_for_each_entry(pclient, &(env->clientDlinkList), dLinkListEntry) {
        transocks_pump_dump_info(pclient, "conn #%d", i);
        ++i;
    }
}

void transocks_client_dump_info(transocks_client *pclient) {
    char srcaddrstr[TRANSOCKS_ADDRPORTSTRLEN_MAX];
    char destaddrstr[TRANSOCKS_ADDRPORTSTRLEN_MAX];
    generate_sockaddr_readable_str(srcaddrstr, TRANSOCKS_ADDRPORTSTRLEN_MAX,
                                   pclient->clientAddr);
    generate_sockaddr_readable_str(destaddrstr, TRANSOCKS_ADDRPORTSTRLEN_MAX,
                                   pclient->destAddr);
    fprintf(stdout, "\n\t%s -> %s", srcaddrstr, destaddrstr);
    fprintf(stdout, "\n\tfd: client %d relay %d",
            pclient->clientFd, pclient->relayFd);
    fprintf(stdout, "\n\tclient shut R %d W %d",
            pclient->isClientShutdownRead, pclient->isClientShutdownWrite);
    fprintf(stdout, "\n\trelay shut R %d W %d",
            pclient->isRelayShutdownRead, pclient->isRelayShutdownWrite);
    fprintf(stdout, "\n\tclient state: %s", transocks_client_state_str[pclient->clientState]);
    fprintf(stdout, "\n----------\n");
}