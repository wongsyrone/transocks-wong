//
// Created by wong on 10/13/19.
//

#include "listener-tcp.h"
#include "transparent-method.h"
#include "pump.h"
#include "log.h"
#include "util.h"
#include "netutils.h"


int sockaddr_un_unlink(const struct sockaddr_un *sa) {
    const char *p, *nul;

    assert(sa);

    if (sa->sun_family != AF_UNIX)
        return -EPROTOTYPE;

    if (sa->sun_path[0] == 0) /* Nothing to do for abstract sockets */
        return 0;

    /* The path in .sun_path is not necessarily NUL terminated. Let's fix that. */
    nul = memchr(sa->sun_path, 0, sizeof(sa->sun_path));
    if (nul)
        p = sa->sun_path;
    else
        p = memdupa_suffix0(sa->sun_path, sizeof(sa->sun_path));

    if (unlink(p) < 0)
        return -errno;

    return 1;
}

void generate_sockaddr_readable_str(char *outstrbuf, size_t strbufsize, const transocks_socket_address *address) {
    char ipstr[INET6_ADDRSTRLEN];
    uint16_t port;
    sa_family_t family = TRANSOCKS_SOCKET_ADDRESS_GET_SA_FAMILY(address);
    socklen_t socklen = TRANSOCKS_SOCKET_ADDRESS_GET_SOCKLEN(address);
    if (family == AF_INET) {
        const struct sockaddr_in *in = TRANSOCKS_SOCKET_ADDRESS_GET_SOCKADDR_IN_PTR(address);
        if (inet_ntop(AF_INET, &(in->sin_addr), ipstr, INET_ADDRSTRLEN) == NULL) {
            LOGE_ERRNO("inet_ntop");
        }
        port = ntohs(in->sin_port);
        snprintf(outstrbuf, strbufsize, "%s:%d", ipstr, port);
    } else if (family == AF_INET6) {
        const struct sockaddr_in6 *in6 = TRANSOCKS_SOCKET_ADDRESS_GET_SOCKADDR_IN6_PTR(address);
        if (inet_ntop(AF_INET6, &(in6->sin6_addr), ipstr, INET6_ADDRSTRLEN) == NULL) {
            LOGE_ERRNO("inet_ntop");
        }
        port = ntohs(in6->sin6_port);
        snprintf(outstrbuf, strbufsize, "[%s]:%d", ipstr, port);

    } else if (family == AF_UNIX) {
        const struct sockaddr_un *un = TRANSOCKS_SOCKET_ADDRESS_GET_SOCKADDR_UN_PTR(address);
        snprintf(outstrbuf, strbufsize, "%s", un->sun_path);
    } else {
        LOGE("unknown sa_family %d, socklen %d", family, socklen);
    }
}

int apply_tcp_keepalive(int fd) {
    if (fd < 0) return -1;
    int keepAlive = 1;
    int keepIdle = 40;
    int keepInterval = 20;
    int keepCount = 5;
#ifdef SO_KEEPALIVE
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void *) &keepAlive, sizeof(keepAlive)) < 0) {
        LOGE_ERRNO("fail to set SO_KEEPALIVE");
        return -1;
    }
#endif
#ifdef TCP_KEEPIDLE
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, (void *) &keepIdle, sizeof(keepIdle)) < 0) {
        LOGE_ERRNO("fail to set TCP_KEEPIDLE");
        return -1;
    }
#endif
#ifdef TCP_KEEPINTVL
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, (void *) &keepInterval, sizeof(keepInterval)) < 0) {
        LOGE_ERRNO("fail to set TCP_KEEPINTVL");
        return -1;
    }
#endif
#ifdef TCP_KEEPCNT
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, (void *) &keepCount, sizeof(keepCount)) < 0) {
        LOGE_ERRNO("fail to set TCP_KEEPCNT");
        return -1;
    }
#endif
    return 0;
}

int apply_ipv6only(int fd, int on) {
    if (fd < 0) return -1;
    if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (void *) &on, sizeof(int)) < 0) {
        LOGE_ERRNO("fail to set IPV6_V6ONLY");
        return -1;
    }
    return 0;
}

int apply_tcp_nodelay(int fd) {
    if (fd < 0) return -1;
    int on = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void *) &on, sizeof(on)) < 0) {
        LOGE_ERRNO("fail to set TCP_NODELAY");
        return -1;
    }
    return 0;
}

int apply_reuse_port(int fd, int on) {
    if (fd < 0) return -1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (void *) &on, sizeof(on)) < 0) {
        LOGE_ERRNO("fail to set SO_REUSEPORT");
        return -1;
    }
    return 0;
}

int apply_reuse_addr(int fd, int on) {
    if (fd < 0) return -1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void *) &on, sizeof(on)) < 0) {
        LOGE_ERRNO("fail to set SO_REUSEADDR");
        return -1;
    }
    return 0;
}

int apply_ip_transparent(int fd, int on) {
    if (fd < 0) return -1;
    if (setsockopt(fd, SOL_IP, IP_TRANSPARENT, (void *) &on, sizeof(on)) < 0) {
        LOGE_ERRNO("fail to set IP_TRANSPARENT");
        return -1;
    }
    return 0;
}

int apply_ip6_transparent(int fd, int on) {
    if (fd < 0) return -1;
    if (setsockopt(fd, SOL_IPV6, IPV6_TRANSPARENT, (void *) &on, sizeof(on)) < 0) {
        LOGE_ERRNO("fail to set IPV6_TRANSPARENT");
        return -1;
    }
    return 0;
}

int apply_ip_recvorigdstaddr(int fd, int on) {
    if (fd < 0) return -1;
    if (setsockopt(fd, SOL_IP, IP_RECVORIGDSTADDR, (void *) &on, sizeof(on)) < 0) {
        LOGE_ERRNO("fail to set IP_RECVORIGDSTADDR");
        return -1;
    }
    return 0;
}

int apply_ip6_recvorigdstaddr(int fd, int on) {
    if (fd < 0) return -1;
    if (setsockopt(fd, SOL_IPV6, IPV6_RECVORIGDSTADDR, (void *) &on, sizeof(on)) < 0) {
        LOGE_ERRNO("fail to set IPV6_RECVORIGDSTADDR");
        return -1;
    }
    return 0;
}

int apply_non_blocking(int fd, bool wantNonBlocking) {
    if (fd < 0) return -1;
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1) {
        LOGE_ERRNO("fcntl F_GETFL");
        return -1;
    }
    if (wantNonBlocking == TRANSOCKS_CHKBIT(flags, O_NONBLOCK)) {
        return 0;
    }
    if (wantNonBlocking) {
        TRANSOCKS_SETBIT(flags, O_NONBLOCK);
    } else {
        TRANSOCKS_CLRBIT(flags, O_NONBLOCK);
    }
    if (fcntl(fd, F_SETFL, flags) != 0) {
        LOGE_ERRNO("fcntl F_SETFL %x", flags);
        return -1;
    }
    return 0;
}

int get_orig_dst_tcp_redirect(int fd, transocks_socket_address *destAddress) {
    socklen_t v6_len = sizeof(struct sockaddr_in6);
    socklen_t v4_len = sizeof(struct sockaddr_in);
    struct sockaddr_storage *storagePtr = TRANSOCKS_SOCKET_ADDRESS_GET_SOCKADDR_STORAGE_PTR(destAddress);
    socklen_t *sockLenPtr = TRANSOCKS_SOCKET_ADDRESS_GET_SOCKLEN_PTR(destAddress);
    int err;
    err = getsockopt(fd, SOL_IPV6, IP6T_SO_ORIGINAL_DST, storagePtr, &v6_len);
    if (err) {
        LOGD_ERRNO("IP6T_SO_ORIGINAL_DST");
        err = getsockopt(fd, SOL_IP, SO_ORIGINAL_DST, storagePtr, &v4_len);
        if (err) {
            LOGE_ERRNO("SO_ORIGINAL_DST");
            return -1;
        }
        *sockLenPtr = v4_len;
        return 0;
    }
    *sockLenPtr = v6_len;
    return 0;
}

int get_orig_dst_tcp_tproxy(int fd, transocks_socket_address *destAddress) {
    socklen_t len = sizeof(struct sockaddr_storage);
    struct sockaddr *addrPtr = TRANSOCKS_SOCKET_ADDRESS_GET_SOCKADDR_PTR(destAddress);
    socklen_t *sockLenPtr = TRANSOCKS_SOCKET_ADDRESS_GET_SOCKLEN_PTR(destAddress);
    int ret = getsockname(fd, addrPtr, &len);
    if (ret) {
        LOGE_ERRNO("getsockname");
        exit(EXIT_FAILURE);
    }
    *sockLenPtr = len;
    return ret;
}

int get_orig_dst_udp_tproxy(struct msghdr *msg, transocks_socket_address *destAddress) {
    socklen_t v6_len = sizeof(struct sockaddr_in6);
    socklen_t v4_len = sizeof(struct sockaddr_in);
    struct sockaddr_storage *storagePtr = TRANSOCKS_SOCKET_ADDRESS_GET_SOCKADDR_STORAGE_PTR(destAddress);
    socklen_t *sockLenPtr = TRANSOCKS_SOCKET_ADDRESS_GET_SOCKLEN_PTR(destAddress);
    struct cmsghdr *cmsg;
    CMSG_FOREACH(cmsg, msg) {
        if (cmsg->cmsg_level == SOL_IP && cmsg->cmsg_type == IP_RECVORIGDSTADDR) {
            memcpy(storagePtr, CMSG_DATA(cmsg), v4_len);
            storagePtr->ss_family = AF_INET;
            *sockLenPtr = v4_len;
            return 0;
        } else if (cmsg->cmsg_level == SOL_IPV6 && cmsg->cmsg_type == IPV6_RECVORIGDSTADDR) {
            memcpy(storagePtr, CMSG_DATA(cmsg), v6_len);
            storagePtr->ss_family = AF_INET6;
            *sockLenPtr = v6_len;
            return 0;
        }
    }
    return 1;
}

bool validate_addr_port(transocks_socket_address *address) {
    struct sockaddr_in6 *in6 = NULL;
    struct sockaddr_in *in = NULL;
    struct sockaddr_un *un = NULL;
    switch (TRANSOCKS_SOCKET_ADDRESS_GET_SA_FAMILY(address)) {
        case AF_INET:
            in = TRANSOCKS_SOCKET_ADDRESS_GET_SOCKADDR_IN_PTR(address);
            if (in->sin_port == 0) {
                return false;
            }
            break;
        case AF_INET6:
            in6 = TRANSOCKS_SOCKET_ADDRESS_GET_SOCKADDR_IN6_PTR(address);
            if (in6->sin6_port == 0) {
                return false;
            }
            if (IN6_IS_ADDR_V4MAPPED(&(in6->sin6_addr))) {
                LOGE("Please input IPv4 address directly");
                return false;
            }
            break;
        case AF_UNIX:
            un = TRANSOCKS_SOCKET_ADDRESS_GET_SOCKADDR_UN_PTR(address);

            break;
        default:
            LOGE("unknown sockaddr_storage ss_family\n");
            return false;
    }
    return true;
}

int transocks_parse_sockaddr_port(const char *str, transocks_socket_address *address) {
    int v6len = sizeof(struct sockaddr_in6);
    int v4len = sizeof(struct sockaddr_in);

    struct sockaddr *sa = TRANSOCKS_SOCKET_ADDRESS_GET_SOCKADDR_PTR(address);
    int err;
    err = evutil_parse_sockaddr_port(str, sa, &v6len);
    if (err != 0) {
        // try again as v4
        err = evutil_parse_sockaddr_port(str, sa, &v4len);
        if (err != 0) {
            return -1;
        }
        TRANSOCKS_SOCKET_ADDRESS_GET_SOCKLEN(address) = (socklen_t) v4len;
        return 0;
    }
    TRANSOCKS_SOCKET_ADDRESS_GET_SOCKLEN(address) = (socklen_t) v6len;
    return 0;
}

int transocks_parse_sockaddr_un(const char *str, transocks_socket_address *address) {

}

int new_stream_socket(sa_family_t family) {
    int sockfd = socket(family, SOCK_STREAM, 0);
    if (sockfd < 0) {
        LOGE_ERRNO("AF_INET, SOCK_STREAM");
        exit(errno);
    }
    return sockfd;
}

int new_dgram_socket(sa_family_t family) {
    int sockfd = socket(family, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        LOGE_ERRNO("AF_INET, SOCK_DGRAM");
        exit(errno);
    }
    return sockfd;
}

int new_stream_udsock(void) {
    int sockfd = new_stream_socket(AF_UNIX);
    apply_non_blocking(sockfd, true);
    return sockfd;
}

int new_dgram_udsock(void) {
    int sockfd = new_dgram_socket(AF_UNIX);
    apply_non_blocking(sockfd, true);
    return sockfd;
}

int new_tcp4_listenersock(void) {
    int sockfd = new_stream_socket(AF_INET);
    apply_non_blocking(sockfd, true);
    apply_reuse_addr(sockfd, 1);
    apply_reuse_port(sockfd, 1);
    return sockfd;
}

int new_tcp6_listenersock(void) {
    int sockfd = new_stream_socket(AF_INET6);
    apply_non_blocking(sockfd, true);
    // ensure we accept both ipv4 and ipv6
    apply_ipv6only(sockfd, 0);
    apply_reuse_addr(sockfd, 1);
    apply_reuse_port(sockfd, 1);
    return sockfd;
}

int new_tcp4_listenersock_tproxy(void) {
    int sockfd = new_tcp4_listenersock();
    apply_non_blocking(sockfd, true);
    apply_ip_transparent(sockfd, 1);
    return sockfd;
}

int new_tcp6_listenersock_tproxy(void) {
    int sockfd = new_tcp6_listenersock();
    apply_non_blocking(sockfd, true);
    apply_ip6_transparent(sockfd, 1);
    return sockfd;
}

int new_udp4_response_sock_tproxy(void) {
    int sockfd = new_dgram_socket(AF_INET);
    apply_non_blocking(sockfd, true);
    apply_reuse_addr(sockfd, 1);
    apply_ip_transparent(sockfd, 1);
    return sockfd;
}

int new_udp6_response_sock_tproxy(void) {
    int sockfd = new_dgram_socket(AF_INET6);
    apply_non_blocking(sockfd, true);
    // ensure we accept both ipv4 and ipv6
    apply_ipv6only(sockfd, 0);
    apply_reuse_addr(sockfd, 1);
    apply_ip6_transparent(sockfd, 1);
    return sockfd;
}

int new_udp4_listenersock_tproxy(void) {
    int sockfd = new_udp4_response_sock_tproxy();
    apply_non_blocking(sockfd, true);
    apply_ip_recvorigdstaddr(sockfd, 1);
    return sockfd;
}

int new_udp6_listenersock_tproxy(void) {
    int sockfd = new_udp6_response_sock_tproxy();
    apply_non_blocking(sockfd, true);
    // ensure we accept both ipv4 and ipv6
    apply_ipv6only(sockfd, 0);
    apply_ip6_recvorigdstaddr(sockfd, 1);
    return sockfd;
}

int sockaddr_un_set_path(struct sockaddr_un *ret, const char *path) {
    size_t l;

    assert(ret);
    assert(path);

    /* Initialize ret->sun_path from the specified argument. This will interpret paths starting with '@' as
     * abstract namespace sockets, and those starting with '/' as regular filesystem sockets. It won't accept
     * anything else (i.e. no relative paths), to avoid ambiguities. Note that this function cannot be used to
     * reference paths in the abstract namespace that include NUL bytes in the name. */

    l = strlen(path);
    if (l == 0)
        return -EINVAL;
    /* replace IN_SET macro from systemd */
    if (!(path[0] == '/' || path[0] == '@'))
        return -EINVAL;
    if (path[1] == 0)
        return -EINVAL;

    /* Don't allow paths larger than the space in sockaddr_un. Note that we are a tiny bit more restrictive than
     * the kernel is: we insist on NUL termination (both for abstract namespace and regular file system socket
     * addresses!), which the kernel doesn't. We do this to reduce chance of incompatibility with other apps that
     * do not expect non-NUL terminated file system path*/
    if (l + 1 > sizeof(ret->sun_path))
        return -EINVAL;

    *ret = (struct sockaddr_un) {
            .sun_family = AF_UNIX,
    };

    if (path[0] == '@') {
        /* Abstract namespace socket */
        memcpy(ret->sun_path + 1, path + 1, l); /* copy *with* trailing NUL byte */
        return (int) (offsetof(struct sockaddr_un, sun_path) + l); /* 🔥 *don't* 🔥 include trailing NUL in size */

    } else {
        assert(path[0] == '/');

        /* File system socket */
        memcpy(ret->sun_path, path, l + 1); /* copy *with* trailing NUL byte */
        return (int) (offsetof(struct sockaddr_un, sun_path) + l + 1); /* include trailing NUL in size */
    }
}

void print_accepted_client_info(const transocks_socket_address *bindAddr,
                                const transocks_socket_address *srcAddr,
                                const transocks_socket_address *destAddr) {
    char *clientTypeStr = NULL;
    int clientType = TRANSOCKS_SOCKET_ADDRESS_GET_SOCKTYPE(srcAddr);
    char srcaddrstr[TRANSOCKS_ADDRPORTSTRLEN_MAX];
    char bindaddrstr[TRANSOCKS_ADDRPORTSTRLEN_MAX];
    char destaddrstr[TRANSOCKS_ADDRPORTSTRLEN_MAX];
    generate_sockaddr_readable_str(bindaddrstr, TRANSOCKS_ADDRPORTSTRLEN_MAX,
                                   bindAddr);

    generate_sockaddr_readable_str(srcaddrstr, TRANSOCKS_ADDRPORTSTRLEN_MAX,
                                   srcAddr);


    generate_sockaddr_readable_str(destaddrstr, TRANSOCKS_ADDRPORTSTRLEN_MAX,
                                   destAddr);

    switch (clientType) {
        case SOCK_DGRAM:
            clientTypeStr = "datagram";
            break;
        case SOCK_STREAM:
            clientTypeStr = "connection";
            break;
        default:
            clientTypeStr = "UNKNOWN";
            break;
    }
    LOGI("%s accepted %s(%d) %s -> %s", bindaddrstr, clientTypeStr, clientType, srcaddrstr, destaddrstr);
}