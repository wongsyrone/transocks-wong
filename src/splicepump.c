//
// Created by wong on 10/30/18.
//

#include "splicepump.h"

// TODO

const transocks_pump transocks_splicepump_ops;

static transocks_splicepump splicePump;


static int transocks_splicepump_start_pump() {
    return 0;
}
static int transocks_splicepump_init(transocks_client *client) {
    transocks_splicepump_ops.user_arg = client;


    return 0;
}

static void transocks_splicepump_free(transocks_client **ppclient) {
    transocks_client *pclient = *ppclient;
    if (pclient == NULL) return;

}

const transocks_pump transocks_splicepump_ops = {
        .name = PUMPMETHOD_SPLICE,
        .init_fn = transocks_splicepump_init,
        .free_fn = transocks_splicepump_free,
        .start_pump_fn = transocks_splicepump_start_pump,
};
