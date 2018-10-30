//
// Created by wong on 10/30/18.
//

#include "bufferpump.h"

// TODO

transocks_pump transocks_bufferpump_ops;

static int transocks_bufferpump_init(transocks_client *client) {
    transocks_bufferpump_ops.user_arg = client;

    return 0;
}

static void transocks_bufferpump_free(transocks_client **ppclient) {
    transocks_client_free(ppclient);
}

static int transocks_bufferpump_start_pump() {
    return 0;
}

transocks_pump transocks_bufferpump_ops = {
        .name = PUMPMETHOD_BUFFER,
        .start_pump_fn = transocks_bufferpump_start_pump,
        .init_fn = transocks_bufferpump_init,
        .free_fn = transocks_bufferpump_free,
};