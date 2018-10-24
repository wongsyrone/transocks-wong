//
// Created by wong on 10/24/18.
//




#include "util.h"

bool setnonblocking(int fd, bool isenable) {
    if (fd < 0) return false;
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    if (isenable) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return (fcntl(fd, F_SETFL, flags) == 0) ? true : false;
}


