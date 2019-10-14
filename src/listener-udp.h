//
// Created by wong on 10/14/19.
//

#ifndef TRANSOCKS_WONG_LISTENER_UDP_H
#define TRANSOCKS_WONG_LISTENER_UDP_H
#include "mem-allocator.h"

#include <unistd.h>

#include "context.h"
#include "util.h"
#include "log.h"
#include "socks5.h"


int udp_listener_init(transocks_global_env *env);
void udp_listener_destory(transocks_global_env *env);

#endif //TRANSOCKS_WONG_LISTENER_UDP_H
