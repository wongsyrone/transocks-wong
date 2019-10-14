//
// Created by wong on 10/27/18.
//

#ifndef TRANSOCKS_WONG_LISTENER_TCP_H
#define TRANSOCKS_WONG_LISTENER_TCP_H

#include "mem-allocator.h"

#include <unistd.h>

#include "context.h"
#include "util.h"
#include "log.h"
#include "socks5.h"


int tcp_listener_init(transocks_global_env *env);
void tcp_listener_destory(transocks_global_env *env);

#endif //TRANSOCKS_WONG_LISTENER_TCP_H
