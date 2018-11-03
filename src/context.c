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
    if (env->bindAddr) TRANSOCKS_FREE(free, env->bindAddr);
    if (env->relayAddr) TRANSOCKS_FREE(free, env->relayAddr);
    if (env->eventBaseLoop) TRANSOCKS_FREE(event_base_free, env->eventBaseLoop);
    if (env) TRANSOCKS_FREE(free, env);
    return NULL;
}

void transocks_global_env_free(transocks_global_env *pEnv) {
    if (pEnv == NULL) return;
    if (pEnv->pumpMethodName != NULL) {
        TRANSOCKS_FREE(free, pEnv->pumpMethodName);
    }
    if (pEnv->relayAddr != NULL) {
        TRANSOCKS_FREE(free, pEnv->relayAddr);
    }

    if (pEnv->bindAddr != NULL) {
        TRANSOCKS_FREE(free, pEnv->bindAddr);
    }

    listener_deinit(pEnv);
    signal_deinit(pEnv);


    if (pEnv->eventBaseLoop != NULL) {
        TRANSOCKS_FREE(event_base_free, pEnv->eventBaseLoop);
    }
    TRANSOCKS_FREE(free, pEnv);
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
    if (client->clientaddr) TRANSOCKS_FREE(free, client->clientaddr);
    if (client->destaddr) TRANSOCKS_FREE(free, client->destaddr);
    if (client) TRANSOCKS_FREE(free, client);
    return NULL;
}

void transocks_client_free(transocks_client *pClient) {
    if (pClient == NULL) return;
    if (pClient->clientaddr!=NULL) {
        TRANSOCKS_FREE(free, pClient->clientaddr);
    }
    if (pClient->destaddr!=NULL) {
        TRANSOCKS_FREE(free, pClient->destaddr);
    }
    pClient->client_state = client_INVALID;
    if (pClient->relay_bev != NULL) {
        bufferevent_disable(pClient->relay_bev, EV_READ | EV_WRITE);
        TRANSOCKS_FREE(bufferevent_free, pClient->relay_bev);
    }
    if (pClient->client_bev != NULL) {
        bufferevent_disable(pClient->client_bev, EV_READ | EV_WRITE);
        TRANSOCKS_FREE(bufferevent_free, pClient->client_bev);
    }
    if (pClient->clientFd > -1) {
        TRANSOCKS_CLOSE(pClient->clientFd);
    }
    if (pClient->relayFd > -1) {
        TRANSOCKS_CLOSE(pClient->relayFd);
    }
    pClient->user_arg = NULL;
    TRANSOCKS_FREE(free, pClient);
}





