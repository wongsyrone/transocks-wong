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
#include <event2/buffer.h>

#include "util.h"
#include "log.h"

enum transocks_client_state {
    client_new,
    client_relay_connected,
    client_socks5_finish_handshake,
    client_pumping_data,
    client_INVALID
};



// which side of shutdown()
typedef short transocks_shutdown_how_t; // EV_READ | EV_WRITE

/* forward declaration */

// global configuration and global environment
// free only exit the program
typedef struct transocks_global_env_t transocks_global_env;

// the client entity carrying essential metadata
typedef struct transocks_client_t transocks_client;

/* detailed declaration */

typedef struct transocks_global_env_t {
    char *pumpMethodName;               // pump method name
    struct sockaddr_storage *bindAddr;  // listener addr
    struct sockaddr_storage *relayAddr; // SOCKS5 server addr
    socklen_t bindAddrLen;              // listener addr socklen
    socklen_t relayAddrLen;             // SOCKS5 server addr socklen
    struct event_base *eventBaseLoop;
    struct evconnlistener *listener;
    struct event *sigterm_ev;
    struct event *sigint_ev;
} transocks_global_env;



typedef struct transocks_client_t {
    struct transocks_global_env_t *global_env;
    struct sockaddr_storage *clientaddr;   // accepted client addr
    struct sockaddr_storage *destaddr;     // accepted client destination addr (from iptables)
    int clientFd;                          // accepted client fd
    int relayFd;
    socklen_t clientaddrlen;               // accepted client addr socklen
    socklen_t destaddrlen;                 // accepted client destination addr socklen
    struct bufferevent *client_bev; // client output -> relay input
    struct bufferevent *relay_bev;  // relay output -> client input
    void *user_arg;
    enum transocks_client_state client_state;
    transocks_shutdown_how_t client_shutdown_how;
    transocks_shutdown_how_t relay_shutdown_how;
} transocks_client;


/* context structures util functions */

transocks_global_env *transocks_global_env_new(void);
void transocks_global_env_free(transocks_global_env **);
transocks_client *transocks_client_new(transocks_global_env *env);
void transocks_client_free(transocks_client **);


#endif //TRANSOCKS_WONG_CONTEXT_H
