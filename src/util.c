//
// Created by wong on 10/24/18.
//

#include "util.h"
#include "log.h"
#include "pump.h"

void generate_sockaddr_port_str(char *outstrbuf, size_t strbufsize, const struct sockaddr *sa, socklen_t socklen) {
    char unknown[] = "???:???";
    char ipstr[INET6_ADDRSTRLEN];
    uint16_t port;
    const char *parsed;

    if (socklen == sizeof(struct sockaddr_in) || sa->sa_family == AF_INET) {
        const struct sockaddr_in *in = (const struct sockaddr_in *) sa;
        parsed = inet_ntop(AF_INET, &(in->sin_addr), ipstr, INET_ADDRSTRLEN);
        if (parsed == NULL) {
            LOGE_ERRNO("inet_ntop");
        }
        port = ntohs(in->sin_port);
        snprintf(outstrbuf, strbufsize, "%s:%d", ipstr, port);
    } else if (socklen == sizeof(struct sockaddr_in6) || sa->sa_family == AF_INET6) {
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *) sa;
        parsed = inet_ntop(AF_INET6, &(in6->sin6_addr), ipstr, INET6_ADDRSTRLEN);
        if (parsed == NULL) {
            LOGE_ERRNO("inet_ntop");
        }
        port = ntohs(in6->sin6_port);
        snprintf(outstrbuf, strbufsize, "[%s]:%d", ipstr, port);
    } else {
        snprintf(outstrbuf, strbufsize, "%s", unknown);
    }
}

int apply_tcp_keepalive(int fd) {
    if (fd < 0) return -1;
    int keepAlive = 1;
    int keepIdle = 40;
    int keepInterval = 20;
    int keepCount = 5;
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void *) &keepAlive, sizeof(keepAlive)) < 0) {
        LOGE_ERRNO("fail to set SO_KEEPALIVE");
        return -1;
    }
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, (void *) &keepIdle, sizeof(keepIdle)) < 0) {
        LOGE_ERRNO("fail to set TCP_KEEPIDLE");
        return -1;
    }
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, (void *) &keepInterval, sizeof(keepInterval)) < 0) {
        LOGE_ERRNO("fail to set SO_KEEPALIVE");
        return -1;
    }
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, (void *) &keepCount, sizeof(keepCount)) < 0) {
        LOGE_ERRNO("fail to set SO_KEEPALIVE");
        return -1;
    }
    return 0;
}

int apply_ipv6only(int fd, int on) {
    if (fd < 0) return -1;
    if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (void *) &on, sizeof(int)) < 0) {
        LOGE_ERRNO("fail to set TCP_NODELAY");
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


int createpipe(int *readEndFd, int *writeEndFd) {
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

int getorigdst(int fd, struct sockaddr_storage *destaddr, socklen_t *addrlen) {
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

bool validatePort(struct sockaddr_storage *ss) {
    switch (ss->ss_family) {
        case AF_INET:
            if (((struct sockaddr_in *) ss)->sin_port == 0) {
                return false;
            }
            break;
        case AF_INET6:
            if (((struct sockaddr_in6 *) ss)->sin6_port == 0) {
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

void print_help() {
    fprintf(stdout, "transocks-wong help:\n");
    fprintf(stdout, "\t WARNING: data must be NATed to our listener endpoint\n");
    fprintf(stdout, "\t --listener-addr-port   what address and port we are listening\n");
    fprintf(stdout, "\t --socks5-addr-port     the SOCKS5 server address and port\n");
    fprintf(stdout, "\t --pump-method          " PUMPMETHOD_BUFFER "/" PUMPMETHOD_SPLICE "\n");
    fprintf(stdout, "\t --help                 this message\n");
    fprintf(stdout, "\t Address must in this format:\n");
    fprintf(stdout, "\t\t - [IPv6Address]\n");
    fprintf(stdout, "\t\t - IPv4Address\n");

}


