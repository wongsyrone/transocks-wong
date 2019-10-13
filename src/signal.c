//
// Created by wong on 10/27/18.
//

#include "signal.h"

static void terminate_signal_cb(evutil_socket_t fd, short events, void *arg);

static void dump_client_info_signal_cb(evutil_socket_t fd, short events, void *arg);

static void drop_all_clients_signal_cb(evutil_socket_t fd, short events, void *arg);


static void terminate_signal_cb(evutil_socket_t fd, short events, void *arg) {
    TRANSOCKS_UNUSED(fd);
    TRANSOCKS_UNUSED(events);
    transocks_global_env *env = (transocks_global_env *) arg;
    if (event_base_loopbreak(env->eventBaseLoop) != 0)
        LOGE("fail to event_base_loopbreak");
}

static void dump_client_info_signal_cb(evutil_socket_t fd, short events, void *arg) {
    TRANSOCKS_UNUSED(fd);
    TRANSOCKS_UNUSED(events);
    transocks_global_env *env = (transocks_global_env *) arg;
    transocks_dump_all_client_info(env);
}

static void drop_all_clients_signal_cb(evutil_socket_t fd, short events, void *arg) {
    TRANSOCKS_UNUSED(fd);
    TRANSOCKS_UNUSED(events);
    transocks_global_env *env = (transocks_global_env *) arg;
    transocks_drop_all_clients(env);
}

int signal_init(transocks_global_env *env) {
    env->sigtermEvent = evsignal_new(env->eventBaseLoop, SIGTERM, terminate_signal_cb, env);
    env->sigintEvent = evsignal_new(env->eventBaseLoop, SIGINT, terminate_signal_cb, env);
    env->sighupEvent = evsignal_new(env->eventBaseLoop, SIGHUP, dump_client_info_signal_cb, env);
    env->sigusr1Event = evsignal_new(env->eventBaseLoop, SIGUSR1, drop_all_clients_signal_cb, env);

    if (env->sigtermEvent == NULL
        || env->sigintEvent == NULL
        || env->sighupEvent == NULL
        || env->sigusr1Event == NULL) {
        LOGE("fail to allocate evsignal");
        return -1;
    }

    if (evsignal_add(env->sigtermEvent, NULL) != 0) {
        LOGE("fail to add SIGTERM");
        return -1;
    }
    if (evsignal_add(env->sigintEvent, NULL) != 0) {
        LOGE("fail to add SIGINT");
        return -1;
    }
    if (evsignal_add(env->sighupEvent, NULL) != 0) {
        LOGE("fail to add SIGHUP");
        return -1;
    }
    if (evsignal_add(env->sigusr1Event, NULL) != 0) {
        LOGE("fail to add SIGUSR1");
        return -1;
    }

    return 0;
}

void signal_deinit(transocks_global_env *env) {
    if (env == NULL) return;
    if (env->sigintEvent != NULL) {
        evsignal_del(env->sigintEvent);
        TRANSOCKS_FREE(event_free, env->sigintEvent);
    }
    if (env->sigtermEvent != NULL) {
        evsignal_del(env->sigtermEvent);
        TRANSOCKS_FREE(event_free, env->sigtermEvent);
    }
    if (env->sighupEvent != NULL) {
        evsignal_del(env->sighupEvent);
        TRANSOCKS_FREE(event_free, env->sighupEvent);
    }
    if (env->sigusr1Event != NULL) {
        evsignal_del(env->sigusr1Event);
        TRANSOCKS_FREE(event_free, env->sigusr1Event);
    }
}
