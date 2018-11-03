//
// Created by wong on 10/25/18.
//

#include "context.h"
#include "listener.h"
#include "signal.h"

/*
 * context structure utility function strategy
 * only init essential member for the current layer
 * only free member directly created and call inner layer free function
 */

transocks_global_env *transocks_global_env_new(void) {
    struct transocks_global_env_t *env =
            calloc(1, sizeof(struct transocks_global_env_t));
    if (env == NULL) {
        LOGE("fail to allocate memory");
        goto fail;
    }
    env->bindAddr = calloc(1, sizeof(struct sockaddr_storage));
    env->relayAddr = calloc(1, sizeof(struct sockaddr_storage));
    if (env->bindAddr == NULL || env->relayAddr == NULL) {
        LOGE("fail to allocate memory");
        goto fail;
    }
    env->eventBaseLoop = event_base_new();
    if (env->eventBaseLoop == NULL) {
        LOGE("fail to allocate event_base");
        goto fail;
    }
    return env;
    fail:
    if (env->bindAddr) free(env->bindAddr);
    if (env->relayAddr) free(env->relayAddr);
    if (env->eventBaseLoop) event_base_free(env->eventBaseLoop);
    if (env) free(env);
    return NULL;
}

void transocks_global_env_free(transocks_global_env **ppenv) {
    if (ppenv == NULL)
        return;
    transocks_global_env *pEnv = *ppenv;
    if (pEnv == NULL) return;
    if (pEnv->pumpMethodName != NULL) {
        free(pEnv->pumpMethodName);
        pEnv->pumpMethodName = NULL;
    }
    if (pEnv->relayAddr != NULL) {
        free(pEnv->relayAddr);
        pEnv->relayAddr = NULL;
    }

    if (pEnv->bindAddr != NULL) {
        free(pEnv->bindAddr);
        pEnv->bindAddr = NULL;
    }

    listener_deinit(pEnv);
    signal_deinit(pEnv);


    if (pEnv->eventBaseLoop != NULL) {
        event_base_free(pEnv->eventBaseLoop);
        pEnv->eventBaseLoop = NULL;
    }

    free(pEnv);
    *ppenv = NULL;
}

transocks_client *transocks_client_new(transocks_global_env *env) {
    transocks_client *client = calloc(1, sizeof(transocks_client));
    if (client == NULL) {
        LOGE("fail to allocate memory");
        goto fail;
    }
    client->client_state = client_new;
    client->clientaddr = calloc(1, sizeof(struct sockaddr_storage));
    client->destaddr = calloc(1, sizeof(struct sockaddr_storage));
    if (client->clientaddr == NULL || client->destaddr == NULL) {
        LOGE("fail to allocate memory");
        goto fail;
    }
    client->clientFd = -1;
    client->relayFd = -1;
    client->global_env = env;
    return client;
    fail:
    if (client->clientaddr) free(client->clientaddr);
    if (client->destaddr) free(client->destadr);
    if (client) free(client);
    return NULL;
}

void transocks_client_free(transocks_client **ppclient) {
    if (ppclient == NULL)
        return;
    transocks_client *pClient = *ppclient;
    if (pClient == NULL) return;

    free(pClient->clientaddr);
    pClient->clientaddr = NULL;
    free(pClient->destaddr);
    pClient->destaddr = NULL;
    pClient->client_state = client_INVALID;
    if (pClient->relay_bev != NULL) {
        bufferevent_disable(pClient->relay_bev, EV_READ | EV_WRITE);
        bufferevent_free(pClient->relay_bev);
        pClient->relay_bev = NULL;
    }
    if (pClient->client_bev != NULL) {
        bufferevent_disable(pClient->client_bev, EV_READ | EV_WRITE);
        bufferevent_free(pClient->client_bev);
        pClient->client_bev = NULL;
    }
    if (pClient->clientFd > -1) {
        close(pClient->clientFd);
        pClient->clientFd = -1;
    }
    if (pClient->relayFd > -1) {
        close(pClient->relayFd);
        pClient->relayFd = -1;
    }
    pClient->user_arg = NULL;
    free(pClient);
    *ppclient = NULL;
}





