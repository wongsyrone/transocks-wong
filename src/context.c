//
// Created by wong on 10/25/18.
//

#include "context.h"
#include "listener.h"
#include "signal.h"
#include "pump.h"

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

    INIT_LIST_HEAD(&(env->clientDlinkList));

    return env;

    fail:
    TRANSOCKS_FREE(free, env->bindAddr);
    TRANSOCKS_FREE(free, env->relayAddr);
    TRANSOCKS_FREE(event_base_free, env->eventBaseLoop);
    TRANSOCKS_FREE(free, env);
    return NULL;
}

void transocks_global_env_free(transocks_global_env *pEnv) {
    if (pEnv == NULL) return;

    TRANSOCKS_FREE(free, pEnv->pumpMethodName);
    TRANSOCKS_FREE(free, pEnv->relayAddr);
    TRANSOCKS_FREE(free, pEnv->bindAddr);
    listener_deinit(pEnv);
    signal_deinit(pEnv);

    TRANSOCKS_FREE(event_base_free, pEnv->eventBaseLoop);
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
    if (client->clientaddr == NULL
        || client->destaddr == NULL) {
        LOGE("fail to allocate memory");
        goto fail;
    }
    client->clientFd = -1;
    client->relayFd = -1;
    client->global_env = env;
    client->client_shutdown_read = false;
    client->client_shutdown_write = false;
    client->relay_shutdown_read = false;
    client->relay_shutdown_write = false;
    return client;

    fail:
    TRANSOCKS_FREE(free, client->clientaddr);
    TRANSOCKS_FREE(free, client->destaddr);
    TRANSOCKS_FREE(free, client);
    return NULL;
}

void transocks_client_free(transocks_client *pClient) {
    if (pClient == NULL) return;

    print_client_info_debug(pClient, "free a conn");

    if (pClient->relay_bev != NULL) {
        bufferevent_disable(pClient->relay_bev, EV_READ | EV_WRITE);
    }
    if (pClient->client_bev != NULL) {
        bufferevent_disable(pClient->client_bev, EV_READ | EV_WRITE);
    }

    TRANSOCKS_FREE(bufferevent_free, pClient->relay_bev);
    TRANSOCKS_FREE(bufferevent_free, pClient->client_bev);

    /*
     * check and shutdown if haven't
     * and ignore any error when calling shutdown()
     */
    if (pClient->client_state != client_new && pClient->client_state != client_INVALID) {
        bool client_shutdown_read_inverse = !pClient->client_shutdown_read;
        bool client_shutdown_write_inverse = !pClient->client_shutdown_write;
        bool relay_shutdown_read_inverse = !pClient->relay_shutdown_read;
        bool relay_shutdown_write_inverse = !pClient->relay_shutdown_write;
        if (pClient->clientFd != -1) {
            if (client_shutdown_read_inverse && client_shutdown_write_inverse) {
                TRANSOCKS_SHUTDOWN(pClient->clientFd, SHUT_RDWR);
            } else if (client_shutdown_read_inverse) {
                TRANSOCKS_SHUTDOWN(pClient->clientFd, SHUT_RD);
            } else if (client_shutdown_write_inverse) {
                TRANSOCKS_SHUTDOWN(pClient->clientFd, SHUT_WR);
            }
        }
        if (pClient->relayFd != -1) {
            if (relay_shutdown_read_inverse && relay_shutdown_write_inverse) {
                TRANSOCKS_SHUTDOWN(pClient->relayFd, SHUT_RDWR);
            } else if (relay_shutdown_read_inverse) {
                TRANSOCKS_SHUTDOWN(pClient->relayFd, SHUT_RD);
            } else if (relay_shutdown_write_inverse) {
                TRANSOCKS_SHUTDOWN(pClient->relayFd, SHUT_WR);
            }
        }
    }

    TRANSOCKS_FREE(free, pClient->clientaddr);
    TRANSOCKS_FREE(free, pClient->destaddr);

    pClient->client_state = client_INVALID;

    TRANSOCKS_CLOSE(pClient->clientFd);
    TRANSOCKS_CLOSE(pClient->relayFd);

    if (!list_empty(&pClient->dlinklistentry)) {
        list_del(&(pClient->dlinklistentry));
    }

    TRANSOCKS_FREE(free, pClient);
}

void transocks_drop_all_clients(transocks_global_env *env) {
    transocks_client *pclient = NULL, *tmp = NULL;

    list_for_each_entry_safe(pclient, tmp, &(env->clientDlinkList), dlinklistentry) {
        print_client_info(pclient, "close connection");
        transocks_pump_free(pclient);
    }
}

void transocks_dump_all_client_info(transocks_global_env *env) {
    transocks_client *pclient = NULL;
    int i = 0;
    fprintf(stdout, "transocks-wong connection info:\n");
    list_for_each_entry(pclient, &(env->clientDlinkList), dlinklistentry) {
        print_client_info(pclient, "conn #%d", i);
        ++i;
    }
}

void print_client_info(transocks_client *pclient, const char *tagfmt, ...) {
    va_list args;
    va_start(args, tagfmt);
    char srcaddrstr[TRANSOCKS_INET_ADDRPORTSTRLEN] = {0};
    char destaddrstr[TRANSOCKS_INET_ADDRPORTSTRLEN] = {0};
    generate_sockaddr_port_str(srcaddrstr, TRANSOCKS_INET_ADDRPORTSTRLEN,
                               (const struct sockaddr *) (pclient->clientaddr), pclient->clientaddrlen);
    generate_sockaddr_port_str(destaddrstr, TRANSOCKS_INET_ADDRPORTSTRLEN,
                               (const struct sockaddr *) (pclient->destaddr), pclient->destaddrlen);
    vfprintf(stdout, tagfmt, args);
    va_end(args);
    fprintf(stdout, ": %s -> %s\n", srcaddrstr, destaddrstr);

}