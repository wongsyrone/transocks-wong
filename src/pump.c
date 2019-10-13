//
// Created by wong on 10/30/18.
//

#include "util.h"
#include "pump.h"

#include "pump-bufferpump.h"
#include "pump-splicepump.h"

extern transocks_pump transocks_pump_bufferpump_ops;
extern transocks_pump transocks_pump_splicepump_ops;

static transocks_pump *transocks_pumps[] = {
        &transocks_pump_splicepump_ops,
        &transocks_pump_bufferpump_ops,
};

static transocks_pump *pumpMethodImpl = NULL;

int transocks_pump_init(transocks_global_env *env) {
    transocks_pump **ppump;
    TRANSOCKS_FOREACH(ppump, transocks_pumps) {
        if (!strcmp(env->pumpMethodName, (*ppump)->name)) {
            pumpMethodImpl = *ppump;
            break;
        }
    }
    // sanity checks
    if (pumpMethodImpl == NULL) {
        LOGE("cannot find pump impl %s", env->pumpMethodName);
        return -1;
    }
    if (pumpMethodImpl->start_pump_fn == NULL) {
        LOGE("unregistered start_pump_fn handler");
        return -1;
    }
    if (pumpMethodImpl->free_pump_fn == NULL) {
        LOGE("unregistered free_pump_fn handler");
        return -1;
    }
    if (pumpMethodImpl->dump_info_fn == NULL) {
        LOGE("unregistered dump_info_fn handler");
        return -1;
    }

    return 0;
}

int transocks_start_pump(transocks_client *pclient) {
    if (pumpMethodImpl->start_pump_fn(pclient) != 0) {
        LOGE("fail to start pump %s", pumpMethodImpl->name);
        return -1;
    }
    pclient->clientState = client_pumping_data;
    return 0;
}

void transocks_pump_free(transocks_client *pclient) {
    pumpMethodImpl->free_pump_fn(pclient);
}

void transocks_pump_dump_info(transocks_client *pclient, const char *tagfmt, ...) {
    va_list args;
    va_start(args, tagfmt);
    fprintf(stdout, "\n----------\n");
    vfprintf(stdout, tagfmt, args);
    va_end(args);
    fprintf(stdout, ":\n");
    // pump specific dump info function
    pumpMethodImpl->dump_info_fn(pclient);
}
