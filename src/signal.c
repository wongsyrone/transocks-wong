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
    env->sigterm_ev = evsignal_new(env->eventBaseLoop, SIGTERM, terminate_signal_cb, env);
    env->sigint_ev = evsignal_new(env->eventBaseLoop, SIGINT, terminate_signal_cb, env);
    env->sighup_ev = evsignal_new(env->eventBaseLoop, SIGHUP, dump_client_info_signal_cb, env);
    env->sigusr1_ev = evsignal_new(env->eventBaseLoop, SIGUSR1, drop_all_clients_signal_cb, env);

    if (env->sigterm_ev == NULL
        || env->sigint_ev == NULL
        || env->sighup_ev == NULL
        || env->sigusr1_ev == NULL) {
        LOGE("fail to allocate evsignal");
        return -1;
    }

    if (evsignal_add(env->sigterm_ev, NULL) != 0) {
        LOGE("fail to add SIGTERM");
        return -1;
    }
    if (evsignal_add(env->sigint_ev, NULL) != 0) {
        LOGE("fail to add SIGINT");
        return -1;
    }
    if (evsignal_add(env->sighup_ev, NULL) != 0) {
        LOGE("fail to add SIGHUP");
        return -1;
    }
    if (evsignal_add(env->sigusr1_ev, NULL) != 0) {
        LOGE("fail to add SIGUSR1");
        return -1;
    }

    return 0;
}

void signal_deinit(transocks_global_env *env) {
    if (env == NULL) return;
    if (env->sigint_ev != NULL) {
        evsignal_del(env->sigint_ev);
        TRANSOCKS_FREE(event_free, env->sigint_ev);
    }
    if (env->sigterm_ev != NULL) {
        evsignal_del(env->sigterm_ev);
        TRANSOCKS_FREE(event_free, env->sigterm_ev);
    }
    if (env->sighup_ev != NULL) {
        evsignal_del(env->sighup_ev);
        TRANSOCKS_FREE(event_free, env->sighup_ev);
    }
    if (env->sigusr1_ev != NULL) {
        evsignal_del(env->sigusr1_ev);
        TRANSOCKS_FREE(event_free, env->sigusr1_ev);
    }
}
