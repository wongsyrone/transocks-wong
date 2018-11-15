//
// Created by wong on 10/24/18.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>

#include "util.h"
#include "log.h"
#include "context.h"
#include "signal.h"
#include "listener.h"
#include "pump.h"


static transocks_global_env *globalEnv = NULL;

int main(int argc, char **argv) {
    int opt;

    char *listenerAddrPort = NULL;
    char *socks5AddrPort = NULL;
    char *pumpMethod = NULL;

    struct sockaddr_storage listener_ss = {0};
    socklen_t listener_ss_size;
    struct sockaddr_storage socks5_ss = {0};
    socklen_t socks5_ss_size;

    static struct option long_options[] = {
            {"listener-addr-port", required_argument, NULL, GETOPT_VAL_LISTENERADDRPORT},
            {"socks5-addr-port",   required_argument, NULL, GETOPT_VAL_SOCKS5ADDRPORT},
            {"pump-method",        optional_argument, NULL, GETOPT_VAL_PUMPMETHOD},
            {"help",               no_argument,       NULL, GETOPT_VAL_HELP},
            {NULL, 0,                                 NULL, 0}
    };

    while ((opt = getopt_long(argc, argv, "", long_options, NULL)) != -1) {
        switch (opt) {
            case GETOPT_VAL_LISTENERADDRPORT:
                listenerAddrPort = optarg;
                break;
            case GETOPT_VAL_SOCKS5ADDRPORT:
                socks5AddrPort = optarg;
                break;
            case GETOPT_VAL_PUMPMETHOD:
                pumpMethod = optarg;
                break;
            case '?':
            case 'h':
            case GETOPT_VAL_HELP:
                PRINTHELP_EXIT();
            default:
                PRINTHELP_EXIT();
        }
    }

    if (listenerAddrPort == NULL
        || socks5AddrPort == NULL) {
        PRINTHELP_EXIT();
    }

    if (pumpMethod == NULL) {
        pumpMethod = PUMPMETHOD_BUFFER;
    }

    // ignore SIGPIPE
    signal(SIGPIPE, SIG_IGN);
    signal(SIGABRT, SIG_IGN);

    if (transocks_parse_sockaddr_port(listenerAddrPort, (struct sockaddr *) &listener_ss, &listener_ss_size) != 0) {
        FATAL_WITH_HELPMSG("invalid listener address and port: %s", listenerAddrPort);
    }
    if (transocks_parse_sockaddr_port(socks5AddrPort, (struct sockaddr *) &socks5_ss, &socks5_ss_size) != 0) {
        FATAL_WITH_HELPMSG("invalid socks5 address and port: %s", socks5AddrPort);
    }

    // check if port exists
    if (!validatePort(&listener_ss)) {
        FATAL_WITH_HELPMSG("fail to parse listener port: %s", listenerAddrPort);
    }
    if (!validatePort(&socks5_ss)) {
        FATAL_WITH_HELPMSG("fail to parse socks5 port: %s", socks5AddrPort);
    }


    globalEnv = transocks_global_env_new();
    if (globalEnv == NULL) {
        goto shutdown;
    }
    if (signal_init(globalEnv) != 0) {
        goto shutdown;
    }
    memcpy(globalEnv->bindAddr, &listener_ss, sizeof(struct sockaddr_storage));
    globalEnv->bindAddrLen = listener_ss_size;
    memcpy(globalEnv->relayAddr, &socks5_ss, sizeof(struct sockaddr_storage));
    globalEnv->relayAddrLen = socks5_ss_size;

    if (listener_init(globalEnv) != 0) {
        goto shutdown;
    }
    globalEnv->pumpMethodName = strdup(pumpMethod);
    if (globalEnv->pumpMethodName == NULL) {
        goto shutdown;
    }

    if (transocks_pump_init(globalEnv) != 0) {
        goto shutdown;
    }

    LOGI("transocks-wong started");
    LOGI("using pumpmethod: %s", globalEnv->pumpMethodName);

    // start event loop
    event_base_dispatch(globalEnv->eventBaseLoop);

    LOGI("exited event loop, shutting down..");

    shutdown:

    // clean up before exit
    transocks_drop_all_clients(globalEnv);
    
    TRANSOCKS_FREE(transocks_global_env_free, globalEnv);

    return 0;
}