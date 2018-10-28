//
// Created by wong on 10/25/18.
//

#ifndef TRANSOCKS_WONG_CONTEXT_H
#define TRANSOCKS_WONG_CONTEXT_H

#include <string.h>
#include <stdlib.h>

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>

#include "util.h"
#include "log.h"

// TODO: add all state
enum transocks_client_state {
    client_new,
    client_relay_connected,
    client_socks5_client_request_sent,
    client
    client_error,
    client_MAX
};

enum transocks_pump_method {
    pumpmethod_bufferevent,
    pumpmethod_splice,
    pumpmethod_UNSPECIFIED
};

// which side of shutdown()
typedef short transocks_shutdown_how_t; // EV_READ | EV_WRITE

/* forward declaration */

// global configuration and global environment
// free only exit the program
typedef struct transocks_global_env_t transocks_global_env;

// used when relaying data between client and the relay
// via buffer copying
typedef struct transocks_bufferpump_t transocks_bufferpump;

// used when relaying data between client and relay socket
// via splice syscall (zero copy)
typedef struct transocks_splicepipe_t transocks_splicepipe;
typedef struct transocks_splicepump_t transocks_splicepump;

// the client entity carrying essential metadata
typedef struct transocks_client_t transocks_client;

/* detailed declaration */

typedef struct transocks_global_env_t {
    struct sockaddr_storage *bindAddr;  // listener addr
    struct sockaddr_storage *relayAddr; // SOCKS5 server addr
    socklen_t bindAddrLen;              // listener addr socklen
    socklen_t relayAddrLen;             // SOCKS5 server addr socklen
    struct event_base *eventBaseLoop;
    struct evconnlistener *listener;
    struct event *sigterm_ev;
    struct event *sigint_ev;
} transocks_global_env;

typedef struct transocks_bufferpump_t {
    struct transocks_client_t *client;
    struct bufferevent *client_bev; // client output -> relay input
    struct bufferevent *relay_bev;  // relay output -> client input
} transocks_bufferpump;

typedef struct transocks_splicepipe_t {
    int pipe_readfd;   // pass pointer to read fd, pipe2() reads these two fds
    int pipe_writefd;
    size_t pipe_size;
} transocks_splicepipe;

typedef struct transocks_splicepump_t {
    struct transocks_client_t *client;
    struct event *client_read_ev;
    struct event *client_write_ev;
    struct event *relay_read_ev;
    struct event *relay_write_ev;
    struct transocks_splicepipe_t *splice_pipe;

} transocks_splicepump;

typedef struct transocks_client_t {
    struct transocks_global_env_t *global_env;
    struct sockaddr_storage *clientaddr;   // accepted client addr
    struct sockaddr_storage *destaddr;     // accepted client destination addr (from iptables)
    int clientFd;                          // accepted client fd
    int relayFd;
    socklen_t clientaddrlen;               // accepted client addr socklen
    socklen_t destaddrlen;                 // accepted client destination addr socklen
    enum transocks_client_state client_state;
    // TODO: determine how to handle these two pumping method
    enum transocks_pump_method pump_method;
    union {
        struct transocks_bufferpump_t *bufferpump;
        struct transocks_splicepump_t *splicepump;
    } u_pump_method;
    transocks_shutdown_how_t client_shutdown_how;
    transocks_shutdown_how_t relay_shutdown_how;
} transocks_client;


/* context structures util functions */

transocks_global_env *transocks_global_env_new(void);
void transocks_global_env_free(transocks_global_env **);
transocks_client *transocks_client_new(transocks_global_env *env, enum transocks_pump_method pumpMethod);
void transocks_client_free(transocks_client **);
transocks_bufferpump *transocks_bufferpump_new(transocks_client *client);
void transocks_bufferpump_free(transocks_bufferpump **);
transocks_splicepump *transocks_splicepump_new(transocks_client *client);
void transocks_splicepump_free(transocks_splicepump **);
transocks_splicepipe *transocks_splicepipe_new();
void transocks_splicepipe_free(transocks_splicepipe **);

#endif //TRANSOCKS_WONG_CONTEXT_H
