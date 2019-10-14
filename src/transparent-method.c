//
// Created by wong on 10/13/19.
//

#include "transparent-method.h"

extern transocks_transparent_method transocks_transparent_method_redirect_ops;
extern transocks_transparent_method transocks_transparent_method_tproxy_ops;

static transocks_transparent_method *transocks_transparent_methods[] = {
        &transocks_transparent_method_redirect_ops,
        &transocks_transparent_method_tproxy_ops,
};

static transocks_transparent_method *transparentMethodImpl = NULL;


int transocks_transparent_method_init(transocks_global_env *env) {
    transocks_transparent_method **ppMethod;
    TRANSOCKS_FOREACH(ppMethod, transocks_transparent_methods) {
        if (!strcmp(env->transparentMethodName, (*ppMethod)->name)) {
            transparentMethodImpl = *ppMethod;
            break;
        }
    }
    // sanity checks
    if (transparentMethodImpl == NULL) {
        LOGE("cannot find transparent method impl %s", env->transparentMethodName);
        return -1;
    }
    // TODO: determine methods

    if (transparentMethodImpl->tcp_listener_sock_setup_fn == NULL) {
        LOGE("unregistered tcp_listener_sock_setup_fn handler");
        return -1;
    }
    if (transparentMethodImpl->udp_listener_sock_setup_fn == NULL) {
        LOGE("unregistered udp_listener_sock_setup_fn handler");
        return -1;
    }
    if (transparentMethodImpl->tcp_on_client_received_fn == NULL) {
        LOGE("unregistered tcp_on_client_received_fn handler");
        return -1;
    }
    if (transparentMethodImpl->udp_on_client_received_fn == NULL) {
        LOGE("unregistered udp_on_client_received_fn handler");
        return -1;
    }
    if (transparentMethodImpl->tcp_client_sock_setup == NULL) {
        LOGE("unregistered tcp_client_sock_setup handler");
        return -1;
    }
    if (transparentMethodImpl->udp_client_sock_setup == NULL) {
        LOGE("unregistered udp_client_sock_setup handler");
        return -1;
    }
    if (transparentMethodImpl->tcp_get_orig_dest_addr == NULL) {
        LOGE("unregistered tcp_get_orig_dest_addr handler");
        return -1;
    }
    if (transparentMethodImpl->udp_get_orig_dest_addr == NULL) {
        LOGE("unregistered udp_get_orig_dest_addr handler");
        return -1;
    }
    if (transparentMethodImpl->tcp_timeout_fn == NULL) {
        LOGE("unregistered tcp_timeout_fn handler");
        return -1;
    }
    if (transparentMethodImpl->udp_timeout_fn == NULL) {
        LOGE("unregistered udp_timeout_fn handler");
        return -1;
    }
    return 0;
}

int transocks_transparent_method_tcp_listener_sock_setup() {

}
