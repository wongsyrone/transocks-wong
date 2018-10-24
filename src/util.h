//
// Created by wong on 10/24/18.
//

#ifndef TRANSOCKS_WONG_UTIL_H
#define TRANSOCKS_WONG_UTIL_H

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

#define TRANSOCKS_FREE(ptr) do { free(ptr); (ptr) = NULL; } while(0)
#define TRANSOCKS_UNUSED(obj) ((void)obj)


enum {
    GETOPT_VAL_LISTENERADDR,
    GETOPT_VAL_LISTENERPORT,
    GETOPT_VAL_SOCKS5ADDR,
    GETOPT_VAL_SOCKS5PORT,
    GETOPT_VAL_HELP
};

bool setnonblocking(int, bool);

#endif //TRANSOCKS_WONG_UTIL_H
