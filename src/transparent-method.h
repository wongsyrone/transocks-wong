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
 * init listener -> begin listen
 * on_client_received -> setup client socket and client type -> get orig dest
 * start handshaking with SOCKS5 upstream ( timeout
 * streaming data between SOCKS5 upstream and client
 */
/* functions */

/* the last param is user-args */
typedef int (*transocks_transparent_method_listener_sock_setup_fn_t)(transocks_client *, int , int , int , void *);
typedef int (*transocks_transparent_method_client_sock_setup_fn_t)(transocks_client *, int , int , int , void *);
typedef int (*transocks_transparent_method_on_client_received_fn_t)(transocks_client *, void *);
typedef int (*transocks_transparent_method_get_orig_dest_addr_fn_t)(transocks_client *, struct sockaddr_storage *, socklen_t *, void *);
typedef int (*transocks_transparent_method_timeout_fn_t)(transocks_client *, void *);

typedef void (*transocks_transparent_method_fn3_t)(transocks_client *);

/* detailed type */
typedef struct transocks_transparent_method_t {
    char *name;
    void *user_arg;
    transocks_transparent_method_listener_sock_setup_fn_t listener_sock_setup_fn;
    transocks_transparent_method_on_client_received_fn_t on_client_received_fn;
    transocks_transparent_method_client_sock_setup_fn_t client_sock_setup;
    transocks_transparent_method_get_orig_dest_addr_fn_t get_orig_dest_addr;
    transocks_transparent_method_timeout_fn_t timeout_fn;
} transocks_transparent_method;


/* exported functions */
int transocks_transparent_method_init(transocks_global_env *env);

#endif //TRANSOCKS_WONG_TRANSPARENT_METHOD_H
