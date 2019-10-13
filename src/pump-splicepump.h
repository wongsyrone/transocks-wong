//
// Created by wong on 10/30/18.
//

#ifndef TRANSOCKS_WONG_PUMP_SPLICEPUMP_H
#define TRANSOCKS_WONG_PUMP_SPLICEPUMP_H

#include "mem-allocator.h"

#include "util.h"
#include "netutils.h"
#include "context.h"
#include "log.h"
#include "pump.h"

// used when relaying data between client and relay socket
// via splice syscall (zero copy)
typedef struct transocks_splicepipe_t transocks_splicepipe;
typedef struct transocks_splicepump_t transocks_splicepump;

typedef struct transocks_splicepipe_t {
    int pipe_readfd;   // pass pointer to read fd, pipe2() reads these two fds
    int pipe_writefd;
    size_t data_in_pipe;
    size_t capacity;
} transocks_splicepipe;

typedef struct transocks_splicepump_t {
    struct event *client_read_ev;
    struct event *client_write_ev;
    struct event *relay_read_ev;
    struct event *relay_write_ev;
    transocks_splicepipe *inbound_pipe;  // relay -> client
    transocks_splicepipe *outbound_pipe; // client -> relay
} transocks_splicepump;


#endif //TRANSOCKS_WONG_PUMP_SPLICEPUMP_H
