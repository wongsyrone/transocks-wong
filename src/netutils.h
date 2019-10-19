//
// Created by wong on 10/13/19.
//

#ifndef TRANSOCKS_WONG_NETUTILS_H
#define TRANSOCKS_WONG_NETUTILS_H

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h> /* IP_RECVORIGDSTADDR */
#include <netinet/tcp.h>
#include <arpa/inet.h> /* inet_pton, inet_ntop */
#include <sys/types.h>
#include <sys/socket.h>  /* socket(), bind(),listen(), accept(),getsockopt() */
#include <sys/un.h>
#include <linux/if.h>                         /* For IFNAMSIZ. Silly Travis CI Ubuntu kernel */
#include <linux/netfilter_ipv4.h>             /* SO_ORIGINAL_DST */
#include <linux/netfilter_ipv6/ip6_tables.h>  /* IP6T_SO_ORIGINAL_DST */
#include <event2/util.h>
#include "mem-allocator.h"
#include "util.h"

#ifndef SO_ORIGINAL_DST
#warning using custom SO_ORIGINAL_DST
#define SO_ORIGINAL_DST  80
#endif

#ifndef IP6T_SO_ORIGINAL_DST
#warning using custom IP6T_SO_ORIGINAL_DST
#define IP6T_SO_ORIGINAL_DST  80
#endif

#ifndef IP_RECVORIGDSTADDR
#warning using custom IP_RECVORIGDSTADDR
#define IP_RECVORIGDSTADDR 20
#endif

#ifndef IPV6_RECVORIGDSTADDR
#warning using custom IPV6_RECVORIGDSTADDR
#define IPV6_RECVORIGDSTADDR 74
#endif

#ifndef IP_TRANSPARENT
#warning using custom IP_TRANSPARENT
#define IP_TRANSPARENT 19
#endif

#ifndef IPV6_TRANSPARENT
#warning using custom IPV6_TRANSPARENT
#define IPV6_TRANSPARENT 75
#endif

#define TRANSOCKS_MAX(a, b)    ( ((a) > (b)) ? (a) : (b)  )

#define TRANSOCKS_INVALID_SOCKET                (-1)

#define TRANSOCKS_INET_PORTSTRLEN               (5 + 1)
/* '[' + INET6_ADDRSTRLEN + ']' + ':' + "65535" + NUL */
#define TRANSOCKS_INET_ADDRPORTSTRLEN           (1 + INET6_ADDRSTRLEN + 1 + 1 + TRANSOCKS_INET_PORTSTRLEN + 1)

#define TRANSOCKS_ADDRPORTSTRLEN_MAX  ( TRANSOCKS_MAX( (sizeof(struct sockaddr_un) + 1),  TRANSOCKS_INET_ADDRPORTSTRLEN ) )

#define TRANSOCKS_BUFSIZE                       (4096)
#define TRANSOCKS_IS_RETRIABLE(err)             ((err) == EAGAIN || (err) == EWOULDBLOCK || (err) == EINTR)

#define TRANSOCKS_SHUTDOWN(fd, how)             TEMP_FAILURE_RETRY(shutdown(fd, how))

#define TRANSOCKS_CLOSE(fd)                   \
    do {                                      \
        if ((fd) >= 0) {                      \
            close(fd);                        \
            (fd) = TRANSOCKS_INVALID_SOCKET;  \
        }                                     \
    } while (0)

/* util macro for transocks_socket_address_t */
#define TRANSOCKS_SOCKET_ADDRESS_GET_SA_FAMILY(a) ((a)->sockaddr.sa.sa_family)
#define TRANSOCKS_SOCKET_ADDRESS_GET_SOCKPROTOCOL(a) ( (a)->protocol )
#define TRANSOCKS_SOCKET_ADDRESS_GET_SOCKTYPE(a) ( (a)->type )
#define TRANSOCKS_SOCKET_ADDRESS_GET_SOCKLEN(a) ( (a)->size )

#define TRANSOCKS_SOCKET_ADDRESS_GET_SA_FAMILY_PTR(a) ( &((a)->sockaddr.sa.sa_family) )
#define TRANSOCKS_SOCKET_ADDRESS_GET_SOCKPROTOCOL_PTR(a) ( &((a)->protocol) )
#define TRANSOCKS_SOCKET_ADDRESS_GET_SOCKTYPE_PTR(a) ( &((a)->type) )
#define TRANSOCKS_SOCKET_ADDRESS_GET_SOCKLEN_PTR(a) ( &((a)->size) )

#define TRANSOCKS_SOCKET_ADDRESS_GET_SOCKADDR(a) ( (a)->sockaddr.sa )
#define TRANSOCKS_SOCKET_ADDRESS_GET_SOCKADDR_STORAGE(a) ( (a)->sockaddr.storage )
#define TRANSOCKS_SOCKET_ADDRESS_GET_SOCKADDR_IN(a) ( (a)->sockaddr.in )
#define TRANSOCKS_SOCKET_ADDRESS_GET_SOCKADDR_IN6(a) ( (a)->sockaddr.in6 )
#define TRANSOCKS_SOCKET_ADDRESS_GET_SOCKADDR_UN(a) ( (a)->sockaddr.un )

#define TRANSOCKS_SOCKET_ADDRESS_GET_SOCKADDR_PTR(a) ( &((a)->sockaddr.sa) )
#define TRANSOCKS_SOCKET_ADDRESS_GET_SOCKADDR_STORAGE_PTR(a) ( &((a)->sockaddr.storage) )
#define TRANSOCKS_SOCKET_ADDRESS_GET_SOCKADDR_IN_PTR(a) ( &((a)->sockaddr.in) )
#define TRANSOCKS_SOCKET_ADDRESS_GET_SOCKADDR_IN6_PTR(a) ( &((a)->sockaddr.in6) )
#define TRANSOCKS_SOCKET_ADDRESS_GET_SOCKADDR_UN_PTR(a) ( &((a)->sockaddr.un) )

#define CMSG_FOREACH(cmsg, mh)                                          \
        for ((cmsg) = CMSG_FIRSTHDR(mh); (cmsg); (cmsg) = CMSG_NXTHDR((mh), (cmsg)))

typedef struct transocks_socket_address_t  transocks_socket_address;

union transocks_sockaddr_union {
    /* The minimal, abstract version */
    struct sockaddr sa;

    /* The libc provided version that allocates "enough room" for every protocol */
    struct sockaddr_storage storage;

    /* Protoctol-specific implementations */
    struct sockaddr_in in;
    struct sockaddr_in6 in6;
    struct sockaddr_un un;

    /* Ensure there is enough space after the AF_UNIX sun_path for one more NUL byte, just to be sure that the path
     * component is always followed by at least one NUL byte. */
    uint8_t un_buffer[sizeof(struct sockaddr_un) + 1];
};

typedef struct transocks_socket_address_t {
    union transocks_sockaddr_union sockaddr;

    /* We store the size here explicitly due to the weird
     * sockaddr_un semantics for abstract sockets */
    socklen_t size;

    /* Socket type, i.e. SOCK_STREAM, SOCK_DGRAM, ... */
    int type;

    /* Socket protocol, IPPROTO_xxx, usually 0, except for netlink */
    int protocol;
} transocks_socket_address;

int sockaddr_un_unlink(const struct sockaddr_un *sa);

static inline int transocks_socket_address_unlink(const transocks_socket_address *a) {
    return TRANSOCKS_SOCKET_ADDRESS_GET_SA_FAMILY(a) == AF_UNIX ? sockaddr_un_unlink(&a->sockaddr.un) : 0;
}

void generate_sockaddr_readable_str(char *outstrbuf, size_t strbufsize, const transocks_socket_address *address);

int apply_tcp_keepalive(int);

int apply_ipv6only(int, int);

int apply_tcp_nodelay(int);

int apply_non_blocking(int, bool);

int get_orig_dst_tcp_redirect(int fd, transocks_socket_address *destAddress);

int get_orig_dst_tcp_tproxy(int fd, transocks_socket_address *destAddress);

int get_orig_dst_udp_tproxy(struct msghdr *msg, transocks_socket_address *destAddress);

bool validate_addr_port(transocks_socket_address *address);

int transocks_parse_sockaddr_port(const char *str, transocks_socket_address *address);

int new_stream_socket(sa_family_t family) ;

int new_dgram_socket(sa_family_t family) ;

int new_stream_udsock(void) ;

int new_dgram_udsock(void) ;

int new_tcp4_listenersock(void);

int new_tcp6_listenersock(void);

int new_tcp4_listenersock_tproxy(void);

int new_tcp6_listenersock_tproxy(void);

int new_udp4_response_sock_tproxy(void);

int new_udp6_response_sock_tproxy(void);

int new_udp4_listenersock_tproxy(void);

int new_udp6_listenersock_tproxy(void);

int sockaddr_un_set_path(struct sockaddr_un *ret, const char *path);

void print_accepted_client_info(const transocks_socket_address *bindAddr,
                                const transocks_socket_address *srcAddr,
                                const transocks_socket_address *destAddr);

#endif //TRANSOCKS_WONG_NETUTILS_H
