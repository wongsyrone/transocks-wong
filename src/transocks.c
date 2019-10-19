//
// Created by wong on 10/24/18.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>

#include "mem-allocator.h"
#include "util.h"
#include "log.h"
#include "context.h"
#include "signal.h"
#include "listener-tcp.h"
#include "listener-udp.h"
#include "pump.h"
#include "transparent-method.h"
#include "netutils.h"


static transocks_global_env *globalEnv = NULL;

int main(int argc, char **argv) {
    int opt;

    char *userTcpListenerAddrPort = NULL;
    char *userUdpListenerAddrPort = NULL;
    char *userSocks5AddrPort = NULL;
    char *userPumpMethod = NULL;
    char *userTransparentMethod = NULL;

    transocks_socket_address tcp_listener_addr;
    transocks_socket_address udp_listener_addr;
    transocks_socket_address socks5_addr;

    memset(&tcp_listener_addr, 0x00, sizeof(transocks_socket_address));
    memset(&udp_listener_addr, 0x00, sizeof(transocks_socket_address));
    memset(&socks5_addr, 0x00, sizeof(transocks_socket_address));

    static struct option long_options[] = {
            {
                    .name = "tcp-listener-addr-port",
                    .has_arg = required_argument,
                    .flag = NULL,
                    .val = GETOPT_VAL_TCPLISTENERADDRPORT
            },
            {
                    .name = "udp-listener-addr-port",
                    .has_arg = required_argument,
                    .flag = NULL,
                    .val = GETOPT_VAL_UDPLISTENERADDRPORT
            },
            {
                    .name = "socks5-addr-port",
                    .has_arg = required_argument,
                    .flag = NULL,
                    .val = GETOPT_VAL_SOCKS5ADDRPORT
            },
            {
                    .name = "pump-method",
                    .has_arg = optional_argument,
                    .flag = NULL,
                    .val = GETOPT_VAL_PUMPMETHOD
            },
            {
                    .name = "transparent-method",
                    .has_arg = required_argument,
                    .flag = NULL,
                    .val = GETOPT_VAL_TRANSPARENTMETHOD
            },
            {
                    .name = "help",
                    .has_arg = no_argument,
                    .flag = NULL,
                    .val = GETOPT_VAL_HELP
            },
            {
                    .name = NULL,
                    .has_arg = 0,
                    .flag = NULL,
                    .val = 0
            }
    };

    while ((opt = getopt_long(argc, argv, "", long_options, NULL)) != -1) {
        switch (opt) {
            case GETOPT_VAL_TCPLISTENERADDRPORT:
                userTcpListenerAddrPort = optarg;
                break;
            case GETOPT_VAL_UDPLISTENERADDRPORT:
                userUdpListenerAddrPort = optarg;
                break;
            case GETOPT_VAL_SOCKS5ADDRPORT:
                userSocks5AddrPort = optarg;
                break;
            case GETOPT_VAL_PUMPMETHOD:
                userPumpMethod = optarg;
                break;
            case GETOPT_VAL_TRANSPARENTMETHOD:
                userTransparentMethod = optarg;
                break;
            case '?':
            case 'h':
            case GETOPT_VAL_HELP:
                PRINTHELP_EXIT();
            default:
                PRINTHELP_EXIT();
        }
    }

    if (userTcpListenerAddrPort == NULL
        || userUdpListenerAddrPort == NULL
        || userSocks5AddrPort == NULL) {
        PRINTHELP_EXIT();
    }

    if (userPumpMethod == NULL) {
        userPumpMethod = PUMPMETHOD_BUFFER;
    }

    if (userTransparentMethod == NULL) {
        userTransparentMethod = TRANSPARENTMETHOD_REDIRECT;
    }

    // ignore SIGPIPE
    signal(SIGPIPE, SIG_IGN);

    if (transocks_parse_sockaddr_port(userTcpListenerAddrPort, &tcp_listener_addr) != 0) {
        FATAL_WITH_HELPMSG("invalid tcp listener address and port: %s", userTcpListenerAddrPort);
    }
    if (transocks_parse_sockaddr_port(userUdpListenerAddrPort, &udp_listener_addr) != 0) {
        FATAL_WITH_HELPMSG("invalid udp listener address and port: %s", userUdpListenerAddrPort);
    }
    if (transocks_parse_sockaddr_port(userSocks5AddrPort, &socks5_addr) != 0) {
        FATAL_WITH_HELPMSG("invalid socks5 address and port: %s", userSocks5AddrPort);
    }

    // check if port exists
    if (!validate_addr_port(&tcp_listener_addr)) {
        FATAL_WITH_HELPMSG("fail to parse tcp listener address port: %s", userTcpListenerAddrPort);
    }
    if (!validate_addr_port(&udp_listener_addr)) {
        FATAL_WITH_HELPMSG("fail to parse udp listener address port: %s", userUdpListenerAddrPort);
    }
    if (!validate_addr_port(&socks5_addr)) {
        FATAL_WITH_HELPMSG("fail to parse socks5 address port: %s", userSocks5AddrPort);
    }


    globalEnv = transocks_global_env_new();
    if (globalEnv == NULL) {
        goto bareExit;
    }
    if (signal_init(globalEnv) != 0) {
        goto shutdown;
    }
    // TODO
    memcpy(globalEnv->tcpBindAddr, &tcp_listener_addr, sizeof(transocks_socket_address));
    memcpy(globalEnv->udpBindAddr, &udp_listener_addr, sizeof(transocks_socket_address));
    memcpy(globalEnv->relayAddr, &socks5_addr, sizeof(transocks_socket_address));

    globalEnv->pumpMethodName = tr_strdup(userPumpMethod);
    if (globalEnv->pumpMethodName == NULL) {
        goto shutdown;
    }

    if (transocks_pump_init(globalEnv) != 0) {
        goto shutdown;
    }

    globalEnv->transparentMethodName = tr_strdup(userTransparentMethod);
    if (globalEnv->transparentMethodName == NULL) {
        goto shutdown;
    }

    if (transocks_transparent_method_init(globalEnv) != 0) {
        goto shutdown;
    }
    // TODO: a generic listener for both TCP and UDP
    if (tcp_listener_init(globalEnv) != 0) {
        goto shutdown;
    }
    if (udp_listener_init(globalEnv) != 0) {
        goto shutdown;
    }

    LOGI("transocks-wong started");
    LOGI("using memory allocator: "
                 TR_USED_MEM_ALLOCATOR);
    LOGI("using pump method: %s", globalEnv->pumpMethodName);
    LOGI("using transparent method: %s", globalEnv->transparentMethodName);

    // start event loop
    event_base_dispatch(globalEnv->eventBaseLoop);

    LOGI("exited event loop, shutting down..");

    shutdown:

    // exit gracefully
    transocks_drop_all_clients(globalEnv);
    // report intentional event loop break
    if (event_base_got_exit(globalEnv->eventBaseLoop)
        || event_base_got_break(globalEnv->eventBaseLoop)) {
        LOGE("exited event loop intentionally");
    }
    // we are done, bye
    TRANSOCKS_FREE(transocks_global_env_free, globalEnv);

    bareExit:
    return 0;
}