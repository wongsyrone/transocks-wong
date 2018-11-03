//
// Created by wong on 10/30/18.
//

#ifndef TRANSOCKS_WONG_PUMP_H
#define TRANSOCKS_WONG_PUMP_H

#include "context.h"
#include "util.h"
#include "log.h"


#define PUMPMETHOD_SPLICE "splicepump"
#define PUMPMETHOD_BUFFER "bufferpump"

/* forward declaration */
typedef struct transocks_pump_t transocks_pump;

/* functions */
typedef int (*transocks_pump_start_fn_t)(transocks_client *);

/* detailed type */
typedef struct transocks_pump_t {
    char *name;
    void *user_arg;
    transocks_pump_start_fn_t start_pump_fn;
} transocks_pump;


/* exported functions */

int transocks_pump_init(transocks_global_env *);

int transocks_start_pump(transocks_client *);

#endif //TRANSOCKS_WONG_PUMP_H
