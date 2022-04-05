//
// Created by wong on 10/30/18.
//

#ifndef TRANSOCKS_WONG_SPLICEPUMP_H
#define TRANSOCKS_WONG_SPLICEPUMP_H

#include "mem-allocator.h"

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

typedef enum transocks_splicepump_data_direction_e {
    inbound,
    outbound,
} transocks_splicepump_data_direction;

typedef enum transocks_splicepump_splice_result_e {
    normal_transfer,
    can_retry,
    fatal_failure,
    read_eof,
} transocks_splicepump_splice_result;

/*
 * According to libevent documentation, event_active() is rarely used, we
 * use it to let producer controls consumer(from pipe's perspective), thus
 * the event being used should NOT be passed to event_add() as producer
 * triggers the consumer and producer controls when the data comes to the end
 * We use the simple trick to activate event that doesn't.
 */
#define TRANSOCKS_EVENT_ACTIVE(ev, events)            \
    do {                                              \
        if (!event_pending((ev), (events), NULL)) {   \
            event_active((ev), (events), 0);          \
        }                                             \
    } while (0)


#endif //TRANSOCKS_WONG_SPLICEPUMP_H
