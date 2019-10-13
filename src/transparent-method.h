//
// Created by wong on 10/13/19.
//

#ifndef TRANSOCKS_WONG_TRANSPARENT_METHOD_H
#define TRANSOCKS_WONG_TRANSPARENT_METHOD_H

#include "mem-allocator.h"
#include "context.h"
#include "util.h"
#include "log.h"

#define TRANSPARENTMETHOD_REDIRECT "redirect"
#define TRANSPARENTMETHOD_TPROXY "tproxy"

/* forward declaration */
typedef struct transocks_transparent_method_t transocks_transparent_method;
// TODO: determine what op we need and rename
/*
 * get orig dest
 * init listener
 */
/* functions */
typedef int (*transocks_transparent_method_fn1_t)(transocks_client *);

typedef void (*transocks_transparent_method_fn2_t)(transocks_client *);

typedef void (*transocks_transparent_method_fn3_t)(transocks_client *);

/* detailed type */
typedef struct transocks_transparent_method_t {
    char *name;
    void *user_arg;
    transocks_transparent_method_fn1_t transparent_method_fn1;
    transocks_transparent_method_fn2_t transparent_method_fn2;
    transocks_transparent_method_fn3_t transparent_method_fn3;
} transocks_transparent_method;


/* exported functions */
int transocks_transparent_method_init(transocks_global_env *env);

#endif //TRANSOCKS_WONG_TRANSPARENT_METHOD_H
