//
// Created by wong on 10/30/18.
//

#include "util.h"
#include "pump.h"

#include "bufferpump.h"
#include "splicepump.h"

extern transocks_pump transocks_bufferpump_ops;
extern transocks_pump transocks_splicepump_ops;

static transocks_pump *transocks_pumps[] = {
        &transocks_splicepump_ops,
        &transocks_bufferpump_ops,
};

static transocks_pump *pumpMethodImpl = NULL;

int transocks_pump_init(transocks_global_env *env) {
    transocks_pump **ppump;
    TRANSOCKS_FOREACH(ppump, transocks_pumps) {
        if (!strcmp(env->pumpMethodName, (*ppump)->name)) {
            pumpMethodImpl = *ppump;
            return 0;
        }
    }
    LOGE("cannot find pump impl %s", env->pumpMethodName);
    return -1;
}

int transocks_start_pump(transocks_client *pclient) {
    if (pumpMethodImpl == NULL || pumpMethodImpl->start_pump_fn == NULL)
        return -1;

    if (pumpMethodImpl->start_pump_fn(pclient) != 0) {
        LOGE("fail to start pump %s", pumpMethodImpl->name);
        return -1;
    }
    pclient->client_state = client_pumping_data;
    return 0;
}

void transocks_pump_free(transocks_client *pclient) {
    pumpMethodImpl->free_pump_fn(pclient);
}
