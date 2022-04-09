//
// Created by wong on 10/25/18.
//

#ifndef TRANSOCKS_WONG_CONTEXT_H
#define TRANSOCKS_WONG_CONTEXT_H

#include "mem-allocator.h"

#include <string.h>
#include <stdlib.h>

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <event2/buffer.h>

#include "util.h"
#include "log.h"
#include "list.h"

enum transocks_client_state {
    client_new,
    client_relay_connected,
    client_socks5_finish_handshake,
    client_pumping_data,
    client_INVALID
};

/* forward declaration */

// global configuration and global environment
// free only exit the program
typedef struct transocks_global_env_t transocks_global_env;

// the client entity carrying essential metadata
typedef struct transocks_client_t transocks_client;

// the connection listener
typedef struct transocks_listener_t transocks_listener;

/* detailed declaration */

typedef struct transocks_global_env_t {
    char *pump_method_name;               // pump method name
    struct sockaddr_storage *bind_addr;   // listener addr
    struct sockaddr_storage *relay_addr;  // SOCKS5 server addr
    socklen_t bind_addr_len;              // listener addr socklen
    socklen_t relay_addr_len;             // SOCKS5 server addr socklen
    struct event_base *eventBaseLoop;
    transocks_listener *listener;
    struct event *sigterm_ev;
    struct event *sigint_ev;
    struct event *sighup_ev;
    struct event *sigusr1_ev;
    struct list_head current_clients_dlinklist;   // double link list of clients
} transocks_global_env;


typedef struct transocks_client_t {
    struct list_head single_client_dlinklist_entry;
    struct transocks_global_env_t *global_env;
    struct sockaddr_storage *client_addr;   // accepted client addr
    struct sockaddr_storage *dest_addr;     // accepted client destination addr (from iptables)
    int client_fd;                          // accepted client fd
    int relay_fd;
    socklen_t client_addr_len;               // accepted client addr socklen
    socklen_t dest_addr_len;                 // accepted client destination addr socklen
    struct bufferevent *client_bev; // client output -> relay input
    struct bufferevent *relay_bev;  // relay output -> client input
    struct event *timeout_ev;
    void *user_arg;
    enum transocks_client_state client_state;
    bool client_shutdown_read;
    bool client_shutdown_write;
    bool relay_shutdown_read;
    bool relay_shutdown_write;
} transocks_client;

typedef struct transocks_listener_t {
    int listener_fd;            // listener socket fd
    struct event *listener_ev;  // listener EV_READ
} transocks_listener;

/* context structures util functions */

transocks_global_env *transocks_global_env_new(void);

void transocks_global_env_free(transocks_global_env *);

transocks_client *transocks_client_new(transocks_global_env *);

void transocks_client_free(transocks_client *);

int transocks_client_set_timeout(transocks_client *, const struct timeval *, event_callback_fn, void *);

void transocks_drop_all_clients(transocks_global_env *);

void transocks_dump_all_client_info(transocks_global_env *);

void transocks_client_dump_info(transocks_client *pclient);

#endif //TRANSOCKS_WONG_CONTEXT_H
