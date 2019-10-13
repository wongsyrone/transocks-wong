//
// Created by wong on 10/27/18.
//

#ifndef TRANSOCKS_WONG_TCP_LISTENER_H
#define TRANSOCKS_WONG_TCP_LISTENER_H

#include "mem-allocator.h"

#include <unistd.h>

#include "context.h"
#include "util.h"
#include "log.h"
#include "socks5.h"


int listener_init(transocks_global_env *);
void listener_deinit(transocks_global_env *);

#endif //TRANSOCKS_WONG_TCP_LISTENER_H
