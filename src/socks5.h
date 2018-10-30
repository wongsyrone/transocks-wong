//
// Created by wong on 10/24/18.
//

#ifndef TRANSOCKS_WONG_SOCKS5_H
#define TRANSOCKS_WONG_SOCKS5_H

#include <unistd.h>

#include "context.h"
#include "log.h"
#include "util.h"

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
#define SOCKS5_REP_FF_UNASSIGNED 0x09


#define  MAXHOSTNAMELEN    (255 + 1)      /* socks5: 255, +1 for len. */

union socks_addr_t {
    char domain[MAXHOSTNAMELEN];
    struct in_addr ipv4;
    struct {
        struct in6_addr ip;
        uint32_t scopeid;
    } ipv6;
} __attribute__((packed, aligned(1)));


struct socks_method_select_request {
    unsigned char ver;
    unsigned char nmethods;
    unsigned char methods[255];
} __attribute__((packed, aligned(1)));

struct socks_method_select_response {
    unsigned char ver;
    unsigned char method;
} __attribute__((packed, aligned(1)));

struct socks_request_header {
    unsigned char ver;
    unsigned char cmd;
    unsigned char rsv;
} __attribute__((packed, aligned(1)));

struct socks_response_header {
    unsigned char ver;
    unsigned char rep;
    unsigned char rsv;
} __attribute__((packed, aligned(1)));

struct socks_host_t {
    unsigned char atyp;
    union socks_addr_t addr;
    uint16_t port;
} __attribute__((packed, aligned(1)));

void transocks_start_connect_relay(transocks_client *client);

#endif //TRANSOCKS_WONG_SOCKS5_H
