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
//    if (transparentMethodImpl->start_pump_fn == NULL) {
//        LOGE("unregistered start_pump_fn handler");
//        return -1;
//    }
//    if (transparentMethodImpl->free_pump_fn == NULL) {
//        LOGE("unregistered free_pump_fn handler");
//        return -1;
//    }
//    if (transparentMethodImpl->dump_info_fn == NULL) {
//        LOGE("unregistered dump_info_fn handler");
//        return -1;
//    }

    return 0;
}
