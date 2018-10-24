//
// Created by wong on 10/24/18.
//
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/util.h>

#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h> /* inet_pton, inet_ntop */
#include <sys/types.h>
#include <sys/socket.h>  /* socket(), bind(),listen(), accept(),getsockopt() */
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6/ip6_tables.h>

#include "util.h"

#ifndef SO_ORIGINAL_DST
#warning using custom SO_ORIGINAL_DST
#define SO_ORIGINAL_DST  80
#endif

#ifndef IP6T_SO_ORIGINAL_DST
#warning using custom IP6T_SO_ORIGINAL_DST
#define IP6T_SO_ORIGINAL_DST  80
#endif

static struct event_base *loop = NULL;
static const struct timeval exit_tv = {
        .tv_sec = 1,
        .tv_usec = 0
};

void signal_cb(int sig) {
    switch (sig) {
        case SIGTERM:
        case SIGINT:
            event_base_loopexit(loop, &exit_tv);
    }
}

void print_help() {
    fprintf(stdout, "transocks-wong help:\n");
    fprintf(stdout, "\t --listener-addr   what address we are listening\n");
    fprintf(stdout, "\t --listener-port   what port we are listening\n");
    fprintf(stdout, "\t --socks5-addr     the SOCKS5 server address\n");
    fprintf(stdout, "\t --socks5-port     the SOCKS5 server port\n");

}

int main(int argc, char **argv) {
    printf("Hello, world\n");
    int opt;

    char *listenerAddr;
    char *listenerPort;
    char *socks5Addr;
    char *socks5Port;

    static struct option long_options[] = {
            {"listener-addr", required_argument, NULL, GETOPT_VAL_LISTENERADDR},
            {"listener-port", required_argument, NULL, GETOPT_VAL_LISTENERPORT},
            {"socks5-addr",   required_argument, NULL, GETOPT_VAL_SOCKS5ADDR},
            {"socks5-port",   required_argument, NULL, GETOPT_VAL_SOCKS5PORT},
            {"help",          no_argument,       NULL, GETOPT_VAL_HELP},
            {NULL, 0,                            NULL, 0}
    };

    while ((opt = getopt_long(argc, argv, "", long_options, NULL)) != -1) {
        switch (opt) {
            case GETOPT_VAL_LISTENERADDR:
                listenerAddr = optarg;
                break;
            case GETOPT_VAL_LISTENERPORT:
                listenerPort = optarg;
                break;
            case GETOPT_VAL_SOCKS5ADDR:
                socks5Addr = optarg;
                break;
            case GETOPT_VAL_SOCKS5PORT:
                socks5Port = optarg;
                break;
            case '?':
            case 'h':
            case GETOPT_VAL_HELP:
                print_help();
                exit(EXIT_SUCCESS);

        }
    }
    if (opterr) {
        print_help();
        exit(EXIT_FAILURE);
    }

    if (listenerAddr == NULL
        || listenerPort == NULL
        || socks5Addr == NULL
        || socks5Port == NULL) {
        print_help();
        exit(EXIT_FAILURE);
    }

    struct event_config *eventConfig;
    int listener;

    // ignore SIGPIPE
    signal(SIGPIPE, SIG_IGN);
    signal(SIGABRT, SIG_IGN);
    signal(SIGINT, signal_cb);
    signal(SIGTERM, signal_cb);


    loop = event_base_new();
    if (!loop) {
        // TODO:err
        return 1;
    }

    // start event loop
    event_base_dispatch(loop);

    // clean up before exit
    event_base_free(loop);

    return 0;
}