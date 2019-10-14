//
// Created by wong on 10/24/18.
//

#ifndef TRANSOCKS_WONG_SOCKS5_H
#define TRANSOCKS_WONG_SOCKS5_H

#include "mem-allocator.h"

#include <unistd.h>

#include "context.h"
#include "log.h"
#include "util.h"

//Memory alignment settings(Part 1)
#pragma pack(push) //Push current alignment to stack.
#pragma pack(1) //Set alignment to 1 byte boundary.

#define SOCKS5_VERSION 0x05
#define SOCKS5_METHOD_NOAUTH 0x00
#define SOCKS5_METHOD_UNACCEPTABLE 0xff

#define SOCKS5_CMD_CONNECT 0x01
#define SOCKS5_CMD_BIND 0x02
#define SOCKS5_CMD_UDP_ASSOCIATE 0x03

#define SOCKS5_ATYP_IPV4 0x01
#define SOCKS5_ATYP_DOMAIN 0x03
#define SOCKS5_ATYP_IPV6 0x04

#define SOCKS5_REP_SUCCEEDED 0x00
#define SOCKS5_REP_GENERAL 0x01
#define SOCKS5_REP_CONN_DISALLOWED 0x02
#define SOCKS5_REP_NETWORK_UNREACHABLE 0x03
#define SOCKS5_REP_HOST_UNREACHABLE 0x04
#define SOCKS5_REP_CONN_REFUSED 0x05
#define SOCKS5_REP_TTL_EXPIRED 0x06
#define SOCKS5_REP_CMD_NOT_SUPPORTED 0x07
#define SOCKS5_REP_ADDRTYPE_NOT_SUPPORTED 0x08
#define SOCKS5_REP_UNASSIGNED 0x09

struct socks_method_select_request {
    unsigned char ver;
    unsigned char nmethods;
    unsigned char methods[1];
};

struct socks_method_select_response {
    unsigned char ver;
    unsigned char method;
};

struct socks_request_ipv4 {
    unsigned char ver;
    unsigned char cmd;
    unsigned char rsv;
    unsigned char atyp;
    struct in_addr addr;
    uint16_t port;
};

struct socks_request_ipv6 {
    unsigned char ver;
    unsigned char cmd;
    unsigned char rsv;
    unsigned char atyp;
    struct in6_addr addr;
    uint16_t port;
};

struct socks_response_ipv4 {
    unsigned char ver;
    unsigned char rep;
    unsigned char rsv;
    unsigned char atyp;
    struct in_addr addr;
    uint16_t port;
};

struct socks_response_ipv6 {
    unsigned char ver;
    unsigned char rep;
    unsigned char rsv;
    unsigned char atyp;
    struct in6_addr addr;
    uint16_t port;
};

struct socks_response_header {
    unsigned char ver;
    unsigned char rep;
    unsigned char rsv;
    unsigned char atyp;
};


//Memory alignment settings(Part 2)
#pragma pack(pop) //Restore original alignment from stack.


void transocks_on_client_received(transocks_client *);

#endif //TRANSOCKS_WONG_SOCKS5_H
