//
// Created by wong on 10/24/18.
//

#include "util.h"
#include "log.h"
#include "pump.h"
#include "transparent-method.h"
#include "netutils.h"


int create_pipe(int *readEndFd, int *writeEndFd) {
    int pipefds[2];
    int ret = pipe(pipefds);
    if (ret == -1) {
        LOGE_ERRNO("pipe");
        return -1;
    }
    if (setnonblocking(pipefds[0], true) != 0) {
        return -1;
    }
    if (setnonblocking(pipefds[1], true) != 0) {
        return -1;
    }
    *readEndFd = pipefds[0];
    *writeEndFd = pipefds[1];
    return 0;
}

void print_help() {
    fprintf(stdout, "transocks-wong help:\n");
    fprintf(stdout, "\t --listener-addr-port   what address and port we are listening\n");
    fprintf(stdout, "\t --socks5-addr-port     the SOCKS5 server address and port\n");
    fprintf(stdout, "\t --pump-method          " PUMPMETHOD_BUFFER "/" PUMPMETHOD_SPLICE "\n");
    fprintf(stdout, "\t --transparent-method   " TRANSPARENTMETHOD_REDIRECT "/" TRANSPARENTMETHOD_TPROXY "\n");
    fprintf(stdout, "\t\t " TRANSPARENTMETHOD_REDIRECT " only works for TCP connections" "\n");
    fprintf(stdout, "\t --help                 this message\n");
    fprintf(stdout, "\t Address and port must in this format:\n");
    fprintf(stdout, "\t\t - [IPv6Address]:Port\n");
    fprintf(stdout, "\t\t - IPv4Address:Port\n");
}
