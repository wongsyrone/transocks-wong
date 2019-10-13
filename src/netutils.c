//
// Created by wong on 10/13/19.
//

#include "transparent-method.h"
#include "pump.h"
#include "log.h"
#include "util.h"
#include "netutils.h"

void generate_sockaddr_port_str(char *outstrbuf, size_t strbufsize, const struct sockaddr *sa, socklen_t socklen) {
    char ipstr[INET6_ADDRSTRLEN];
    uint16_t port;

    if (socklen == sizeof(struct sockaddr_in) || sa->sa_family == AF_INET) {
        const struct sockaddr_in *in = (const struct sockaddr_in *) sa;
        if (inet_ntop(AF_INET, &(in->sin_addr), ipstr, INET_ADDRSTRLEN) == NULL) {
            LOGE_ERRNO("inet_ntop");
        }
        port = ntohs(in->sin_port);
        snprintf(outstrbuf, strbufsize, "%s:%d", ipstr, port);
    } else if (socklen == sizeof(struct sockaddr_in6) || sa->sa_family == AF_INET6) {
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *) sa;
        if (inet_ntop(AF_INET6, &(in6->sin6_addr), ipstr, INET6_ADDRSTRLEN) == NULL) {
            LOGE_ERRNO("inet_ntop");
        }
        port = ntohs(in6->sin6_port);
        snprintf(outstrbuf, strbufsize, "[%s]:%d", ipstr, port);
    } else {
        LOGE("unknown sa_family %d, socklen %d", sa->sa_family, socklen);
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

int setnonblocking(int fd, bool isenable) {
    if (fd < 0) return -1;
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1) {
        LOGE_ERRNO("fcntl F_GETFL");
        return -1;
    }
    if (isenable) {
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

int get_orig_dst_tcp_redirect(int fd, struct sockaddr_storage *destaddr, socklen_t *addrlen) {
    socklen_t v6_len = sizeof(struct sockaddr_in6);
    socklen_t v4_len = sizeof(struct sockaddr_in);
    int err;
    err = getsockopt(fd, SOL_IPV6, IP6T_SO_ORIGINAL_DST, destaddr, &v6_len);
    if (err) {
        LOGD_ERRNO("IP6T_SO_ORIGINAL_DST");
        err = getsockopt(fd, SOL_IP, SO_ORIGINAL_DST, destaddr, &v4_len);
        if (err) {
            LOGE_ERRNO("SO_ORIGINAL_DST");
            return -1;
        }
        *addrlen = v4_len;
        return 0;
    }
    *addrlen = v6_len;
    return 0;
}

bool validateAddrPort(struct sockaddr_storage *ss) {
    struct sockaddr_in6 *in6 = NULL;
    struct sockaddr_in *in = NULL;
    switch (ss->ss_family) {
        case AF_INET:
            in = (struct sockaddr_in *) ss;
            if (in->sin_port == 0) {
                return false;
            }
            break;
        case AF_INET6:
            in6 = (struct sockaddr_in6 *) ss;
            if (in6->sin6_port == 0) {
                return false;
            }
            if (IN6_IS_ADDR_V4MAPPED(&(in6->sin6_addr))) {
                LOGE("Please input IPv4 address directly");
                return false;
            }
            break;
        default:
            LOGE("unknown sockaddr_storage ss_family\n");
            return false;
    }
    return true;
}

int transocks_parse_sockaddr_port(const char *str, struct sockaddr *sa, socklen_t *actualSockAddrLen) {
    int v6len = sizeof(struct sockaddr_in6);
    int v4len = sizeof(struct sockaddr_in);

    int err;
    err = evutil_parse_sockaddr_port(str, sa, &v6len);
    if (err != 0) {
        // try again as v4
        err = evutil_parse_sockaddr_port(str, sa, &v4len);
        if (err != 0) {
            return -1;
        }
        *actualSockAddrLen = (socklen_t) v4len;
        return 0;
    }
    *actualSockAddrLen = (socklen_t) v6len;
    return 0;
}