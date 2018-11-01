//
// Created by wong on 10/30/18.
//

#ifndef TRANSOCKS_WONG_SPLICEPUMP_H
#define TRANSOCKS_WONG_SPLICEPUMP_H

#include "util.h"
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
    size_t pipe_size;
    ssize_t data_in_pipe;
} transocks_splicepipe;

typedef struct transocks_splicepump_t {
    struct event *client_read_ev;
    struct event *client_write_ev;
    struct event *relay_read_ev;
    struct event *relay_write_ev;
    struct transocks_splicepipe_t inbound_pipe;  // relay -> client
    struct transocks_splicepipe_t outbound_pipe; // client -> relay
} transocks_splicepump;


#endif //TRANSOCKS_WONG_SPLICEPUMP_H
