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
        return NULL;
    }
    env->bindAddr = calloc(1, sizeof(struct sockaddr_storage));
    env->relayAddr = calloc(1, sizeof(struct sockaddr_storage));
    if (env->bindAddr == NULL || env->relayAddr == NULL) {
        LOGE("fail to allocate memory");
        return NULL;
    }
    env->eventBaseLoop = event_base_new();
    if (env->eventBaseLoop == NULL) {
        LOGE("fail to allocate event_base");
        return NULL;
    }

    return env;
}

void transocks_global_env_free(transocks_global_env **ppenv) {
    transocks_global_env *pEnv = *ppenv;
    if (pEnv == NULL) return;
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
    pEnv = NULL;
}

transocks_client *transocks_client_new(transocks_global_env *env, enum transocks_pump_method pumpMethod) {
    transocks_client *client = calloc(1, sizeof(transocks_client));
    if (client == NULL) {
        LOGE("fail to allocate memory");
        return NULL;
    }
    client->client_state = client_new;
    client->clientaddr = calloc(1, sizeof(struct sockaddr_storage));
    client->destaddr = calloc(1, sizeof(struct sockaddr_storage));
    if (client->clientaddr == NULL || client->destaddr == NULL) {
        LOGE("fail to allocate memory");
        return NULL;
    }
    switch(pumpMethod) {
        case pumpmethod_UNSPECIFIED:
            break;
        case pumpmethod_bufferevent:
            client->pump_method = pumpmethod_bufferevent;
            client->u_pump_method.bufferpump = transocks_bufferpump_new(client);
            if (client->u_pump_method.bufferpump == NULL) {
                LOGE("fail to allocate memory");
                return NULL;
            }
            break;
        case pumpmethod_splice:
            client->pump_method = pumpmethod_splice;
            client->u_pump_method.splicepump = transocks_splicepump_new(client);
            if (client->u_pump_method.splicepump == NULL) {
                LOGE("fail to allocate memory");
                return NULL;
            }
            break;
        default:
            FATAL("unknown pumpmethod");
    }
    client->clientFd = -1;
    client->relayFd = -1;
    client->global_env = env;
    return client;
}

void transocks_client_free(transocks_client **ppclient) {
    transocks_client *pClient = *ppclient;
    if (pClient == NULL) return;
    switch(pClient->pump_method) {
        case pumpmethod_UNSPECIFIED:
            break;
        case pumpmethod_bufferevent:
            transocks_bufferpump_free(&(pClient->u_pump_method.bufferpump));
            free(pClient->u_pump_method.bufferpump);
            pClient->u_pump_method.bufferpump=NULL;
            break;
        case pumpmethod_splice:
            transocks_splicepump_free( &(pClient->u_pump_method.splicepump));
            free(pClient->u_pump_method.splicepump);
            pClient->u_pump_method.splicepump=NULL;
            break;
        default:
            FATAL("unknown pumpmethod");
            break;
    }
    free(pClient->clientaddr);
    pClient->clientaddr=NULL;
    free(pClient->destaddr);
    pClient->destaddr=NULL;
    pClient->client_state = client_MAX;
    if (pClient->clientFd > -1) {
        close(pClient->clientFd);
        pClient->clientFd = -1;
    }
    if (pClient->relayFd > -1) {
        close(pClient->relayFd);
        pClient->relayFd = -1;
    }
    free(pClient);
    pClient =NULL;
}

transocks_bufferpump *transocks_bufferpump_new(transocks_client *client) {
    transocks_bufferpump *bufferpump = calloc(1, sizeof(transocks_bufferpump));
    if (bufferpump == NULL) {
        LOGE("fail to allocate memory");
        return NULL;
    }
    bufferpump->client = client;
    return bufferpump;
}

void transocks_bufferpump_free(transocks_bufferpump **ppbufferpump) {
    transocks_bufferpump *pBufferpump = *ppbufferpump;
    if (pBufferpump == NULL) return;
    if (pBufferpump->client_bev != NULL) {
        bufferevent_free(pBufferpump->client_bev);
        pBufferpump->client_bev = NULL;
    }
    if (pBufferpump->relay_bev != NULL) {
        bufferevent_free(pBufferpump->relay_bev);
        pBufferpump->relay_bev = NULL;
    }
    pBufferpump->client = NULL;

    free(pBufferpump);
    pBufferpump =NULL;
}

transocks_splicepump *transocks_splicepump_new(transocks_client *client) {
    transocks_splicepump *splicepump = calloc(1, sizeof(transocks_splicepump));
    if (splicepump == NULL) {
        LOGE("fail to allocate memory");
        return NULL;
    }
    splicepump->client = client;
    splicepump->splice_pipe = transocks_splicepipe_new();
    if (splicepump->splice_pipe == NULL) {
        LOGE("fail to allocate memory");
        return NULL;
    }

    return splicepump;
}

void transocks_splicepump_free(transocks_splicepump **ppsplicepump) {
    transocks_splicepump *pSplicepump = *ppsplicepump;
    if (pSplicepump == NULL) return;
    if (pSplicepump->client_read_ev != NULL) {
        event_del(pSplicepump->client_read_ev);
        event_free(pSplicepump->client_read_ev);
        pSplicepump->client_read_ev=NULL;
    }
    if (pSplicepump->client_write_ev != NULL) {
        event_del(pSplicepump->client_write_ev);
        event_free(pSplicepump->client_write_ev);
        pSplicepump->client_write_ev=NULL;
    }
    if (pSplicepump->relay_read_ev != NULL) {
        event_del(pSplicepump->relay_read_ev);
        event_free(pSplicepump->relay_read_ev);
        pSplicepump->relay_read_ev=NULL;
    }
    if (pSplicepump->relay_write_ev != NULL) {
        event_del(pSplicepump->relay_write_ev);
        event_free(pSplicepump->relay_write_ev);
        pSplicepump->relay_write_ev=NULL;
    }
    if (pSplicepump->splice_pipe != NULL) {
        transocks_splicepipe_free( &(pSplicepump->splice_pipe));
        pSplicepump->splice_pipe = NULL;
    }
    pSplicepump->client = NULL;
    free(pSplicepump);
    pSplicepump = NULL;
}

transocks_splicepipe *transocks_splicepipe_new() {
    transocks_splicepipe *splicepipe = calloc(1, sizeof(transocks_splicepipe));
    if (splicepipe == NULL) {
        LOGE("fail to allocate memory");
        return NULL;
    }
    splicepipe->pipe_readfd = -1;
    splicepipe->pipe_writefd = -1;
    return splicepipe;
}

void transocks_splicepipe_free(transocks_splicepipe **ppsplicepipe) {
    transocks_splicepipe *pSplicepipe = *ppsplicepipe;
    if (pSplicepipe == NULL) return;
    if (pSplicepipe->pipe_readfd != -1) {
        close(pSplicepipe->pipe_readfd);
        pSplicepipe->pipe_readfd = -1;
    }
    if (pSplicepipe->pipe_writefd != -1) {
        close(pSplicepipe->pipe_writefd);
        pSplicepipe->pipe_writefd = -1;
    }
    pSplicepipe->pipe_size = -1;
    free(pSplicepipe);
    pSplicepipe =NULL;
}

