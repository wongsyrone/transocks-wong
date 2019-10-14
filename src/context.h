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

// TODO: redesign states
enum transocks_client_state {
    client_new,
    client_relay_connected,
    client_socks5_finish_handshake,
    client_pumping_data,
    client_INVALID
};

enum transocks_listener_type {
    listener_tcp,
    listener_udp,
    listener_UNKNOWN
};

enum transocks_client_type {
    client_tcp,
    client_udp,
    client_UNKNOWN
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
    char *pumpMethodName;               // pump method name
    char *transparentMethodName;        // transparent method name
    struct sockaddr_storage *tcpBindAddr;  // listener addr
    struct sockaddr_storage *udpBindAddr; // udp listener addr
    struct sockaddr_storage *relayAddr; // SOCKS5 server addr
    socklen_t tcpBindAddrLen;              // tcp listener addr socklen
    socklen_t udpBindAddrLen;          // udp listener addr socklen
    socklen_t relayAddrLen;             // SOCKS5 server addr socklen
    struct event_base *eventBaseLoop;
    transocks_listener *tcpListener;
    transocks_listener *udpListener;
    struct event *sigtermEvent;
    struct event *sigintEvent;
    struct event *sighupEvent;
    struct event *sigusr1Event;
    struct list_head clientDlinkList;   // double link list of client
} transocks_global_env;

typedef struct transocks_client_t {
    struct list_head dLinkListEntry;
    struct transocks_global_env_t *globalEnv;
    struct sockaddr_storage *clientAddr;   // accepted client addr
    struct sockaddr_storage *destAddr;     // accepted client destination addr
    int clientFd;                          // accepted client fd
    int relayFd;
    socklen_t clientAddrLen;               // accepted client addr socklen
    socklen_t destAddrLen;                 // accepted client destination addr socklen
    struct bufferevent *clientBufferEvent; // client output -> relay input
    struct bufferevent *relayBufferEvent;  // relay output -> client input
    struct event *timeoutEvent;
    void *userArg;
    enum transocks_client_state clientState;
    enum transocks_client_type clientType;
    bool isClientShutdownRead;
    bool isClientShutdownWrite;
    bool isRelayShutdownRead;
    bool isRelayShutdownWrite;
} transocks_client;

typedef struct transocks_listener_t {
    enum transocks_listener_type listenerType;
    int listenerFd;              // listener socket fd
    struct event *listenerEvent; // listener EV_READ
} transocks_listener;

/* context structures util functions */

transocks_global_env *transocks_global_env_new(void);

void transocks_global_env_free(transocks_global_env *);

transocks_client *transocks_client_new(transocks_global_env *);

void transocks_client_free(transocks_client *);

int transocks_client_set_timeout(struct event_base *,
                                 struct event *,
                                 const struct timeval *,
                                 event_callback_fn,
                                 void *);
int transocks_client_remove_timeout(struct event *,
                                    void *);
void transocks_drop_all_clients(transocks_global_env *);

void transocks_dump_all_client_info(transocks_global_env *);

void transocks_client_dump_info(transocks_client *pclient);

#endif //TRANSOCKS_WONG_CONTEXT_H
