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

int transocks_pump_init(char *name, transocks_client *client) {
    transocks_pump **ppump;
    int ret;
    TRANSOCKS_FOREACH(ppump, transocks_pumps) {
        if (!strcmp(name, (*ppump)->name)) {
            if ( (*ppump)->init_fn != NULL ) {
                ret = (*ppump)->init_fn(client);
                if (ret != 0) return ret;

            }
        }

    }
    LOGE("cannot find pump impl");
    return -1;
}
int transocks_start_pump(char *name) {
    transocks_pump **ppump;
    int ret;
    TRANSOCKS_FOREACH(ppump, transocks_pumps) {
        if (!strcmp(name, (*ppump)->name)) {
            if ( (*ppump)->start_pump_fn != NULL ) {
                ret = (*ppump)->start_pump_fn();
                return ret;
            }
        }
    }
    LOGE("cannot find pump impl");
    return -1;
}

void transocks_pump_free(char *name, transocks_client **client) {
    transocks_pump **ppump;
    TRANSOCKS_FOREACH(ppump, transocks_pumps) {
        if (!strcmp(name, (*ppump)->name)) {
            if ( (*ppump)->free_fn != NULL ) {
                (*ppump)->free_fn(client);
                return;
            }
        }
    }
    LOGE("cannot find pump impl");
}
