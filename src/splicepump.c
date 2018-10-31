//
// Created by wong on 10/30/18.
//

#include "splicepump.h"

// TODO

transocks_pump transocks_splicepump_ops;



static int transocks_splicepump_start_pump(transocks_client **ppclient) {

    return 0;
}

static void transocks_splicepump_free() {

}

transocks_pump transocks_splicepump_ops = {
        .name = PUMPMETHOD_SPLICE,
        .start_pump_fn = transocks_splicepump_start_pump,
};
