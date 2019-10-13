//
// Created by wong on 10/25/18.
//

#include "context.h"
#include "tcp-listener.h"
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
    env->bindAddr = tr_calloc(1, sizeof(struct sockaddr_storage));
    env->relayAddr = tr_calloc(1, sizeof(struct sockaddr_storage));
    if (env->bindAddr == NULL || env->relayAddr == NULL) {
        LOGE("fail to allocate memory");
        goto fail;
    }
    env->eventBaseLoop = event_base_new();
    if (env->eventBaseLoop == NULL) {
        LOGE("fail to allocate event_base");
        goto fail;
    }

    INIT_LIST_HEAD(&(env->clientDlinkList));

    return env;

    fail:
    TRANSOCKS_FREE(tr_free, env->bindAddr);
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
    TRANSOCKS_FREE(tr_free, pEnv->bindAddr);
    listener_deinit(pEnv);
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
    client->clientFd = -1;
    client->relayFd = -1;
    client->clientBufferEvent = NULL;
    client->relayBufferEvent = NULL;
    client->handshakeTimeoutEvent = NULL;
    client->udpTimeoutEvent = NULL;
    client->userArg = NULL;
    client->isClientShutdownRead = false;
    client->isClientShutdownWrite = false;
    client->isRelayShutdownRead = false;
    client->isRelayShutdownWrite = false;
    client->clientState = client_new;

    client->clientAddr = tr_malloc(sizeof(struct sockaddr_storage));
    client->destAddr = tr_malloc(sizeof(struct sockaddr_storage));
    if (client->clientAddr == NULL
        || client->destAddr == NULL) {
        LOGE("fail to allocate memory");
        goto fail;
    }

    client->globalEnv = env;

    return client;

    fail:
    TRANSOCKS_FREE(tr_free, client->clientAddr);
    TRANSOCKS_FREE(tr_free, client->destAddr);
    TRANSOCKS_FREE(tr_free, client);
    return NULL;
}

void transocks_client_free(transocks_client *pClient) {
    if (pClient == NULL) return;

    transocks_pump_dump_info_debug(pClient, "free a conn");

    if (pClient->handshakeTimeoutEvent != NULL) {
        evtimer_del(pClient->handshakeTimeoutEvent);
    }
    TRANSOCKS_FREE(event_free, pClient->handshakeTimeoutEvent);

    if (pClient->udpTimeoutEvent != NULL) {
        evtimer_del(pClient->udpTimeoutEvent);
    }
    TRANSOCKS_FREE(event_free, pClient->udpTimeoutEvent);

    if (pClient->relayBufferEvent != NULL) {
        bufferevent_disable(pClient->relayBufferEvent, EV_READ | EV_WRITE);
    }
    if (pClient->clientBufferEvent != NULL) {
        bufferevent_disable(pClient->clientBufferEvent, EV_READ | EV_WRITE);
    }

    TRANSOCKS_FREE(bufferevent_free, pClient->relayBufferEvent);
    TRANSOCKS_FREE(bufferevent_free, pClient->clientBufferEvent);

    if (pClient->clientState != client_new && pClient->clientState != client_INVALID) {
        if (pClient->clientFd >= 0) {
            TRANSOCKS_SHUTDOWN(pClient->clientFd, SHUT_RDWR);
        }
        if (pClient->relayFd >= 0) {
            TRANSOCKS_SHUTDOWN(pClient->relayFd, SHUT_RDWR);
        }
    }

    TRANSOCKS_FREE(tr_free, pClient->clientAddr);
    TRANSOCKS_FREE(tr_free, pClient->destAddr);

    pClient->clientState = client_INVALID;

    TRANSOCKS_CLOSE(pClient->clientFd);
    TRANSOCKS_CLOSE(pClient->relayFd);

    if (!list_empty(&pClient->dLinkListEntry)) {
        list_del(&(pClient->dLinkListEntry));
    }

    TRANSOCKS_FREE(tr_free, pClient);
}

int transocks_client_set_timeout(transocks_client *pclient, const struct timeval *timeout,
                                 event_callback_fn ev_fn, void *arg) {
    int ret;
    if (pclient->handshakeTimeoutEvent == NULL) {
        // first time
        pclient->handshakeTimeoutEvent = evtimer_new(pclient->globalEnv->eventBaseLoop, ev_fn, arg);
        if (pclient->handshakeTimeoutEvent == NULL) {
            LOGE("mem");
            return -1;
        }
        return evtimer_add(pclient->handshakeTimeoutEvent, timeout);
    } else {
        // already allocated, replace existing timeout
        evtimer_del(pclient->handshakeTimeoutEvent);
        ret = evtimer_assign(pclient->handshakeTimeoutEvent, pclient->globalEnv->eventBaseLoop, ev_fn, arg);
        if (ret != 0) return ret;
        ret = evtimer_add(pclient->handshakeTimeoutEvent, timeout);
        return ret;
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
    char srcaddrstr[TRANSOCKS_INET_ADDRPORTSTRLEN];
    char destaddrstr[TRANSOCKS_INET_ADDRPORTSTRLEN];
    generate_sockaddr_port_str(srcaddrstr, TRANSOCKS_INET_ADDRPORTSTRLEN,
                               (const struct sockaddr *) (pclient->clientAddr), pclient->clientAddrLen);
    generate_sockaddr_port_str(destaddrstr, TRANSOCKS_INET_ADDRPORTSTRLEN,
                               (const struct sockaddr *) (pclient->destAddr), pclient->destAddrLen);
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