//
// Created by wong on 10/24/18.
//

#ifndef TRANSOCKS_WONG_UTIL_H
#define TRANSOCKS_WONG_UTIL_H

#include "mem-allocator.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>


#include <event2/util.h>

/* If for some reason more than 4M are allocated on the stack, let's abort immediately. It's better than
 * proceeding and smashing the stack limits. Note that by default RLIMIT_STACK is 8M on Linux. */
#define TRANSOCKS_ALLOCA_MAX (4U*1024U*1024U)

#if defined(__GNUC__) || defined(__clang__)
#define TRANSOCKS_ATTR(s) __attribute__((s))
#else
#define TRANSOCKS_ATTR(s)
#endif

#define TRANSOCKS_ALWAYS_INLINE TRANSOCKS_ATTR(always_inline) inline

#define TRANSOCKS_SIZEOF_ARRAY(arr)             (sizeof(arr) / sizeof(arr[0]))
#define TRANSOCKS_FOREACH(ptr, array)           for (ptr = array; ptr < array + TRANSOCKS_SIZEOF_ARRAY(array); ptr++)
#define TRANSOCKS_FOREACH_REVERSE(ptr, array)   for (ptr = array + TRANSOCKS_SIZEOF_ARRAY(array) - 1; ptr >= array; ptr--)
#define TRANSOCKS_UNUSED(obj)                   ((void)(obj))

#define TRANSOCKS_CHKBIT(val, flag)             (((val) & (flag)) == (flag))
#define TRANSOCKS_SETBIT(val, flag)             ((val) |= (flag))
#define TRANSOCKS_CLRBIT(val, flag)             ((val) &= ~(flag))
#define TRANSOCKS_TOGGLEBIT(val, flag)          ((val) ^= (flag))


/* Evaluate EXPRESSION, and repeat as long as it returns -1 with `errno'
   set to EINTR.  */
/* taken from glibc unistd.h and fixes musl */
#ifndef TEMP_FAILURE_RETRY
# define TEMP_FAILURE_RETRY(expression) \
  (__extension__							      \
    ({ long int __result;						      \
       do __result = (long int) (expression);				      \
       while (__result == -1L && errno == EINTR);			      \
       __result; }))
#endif

#define TRANSOCKS_FREE(free_fn, ptr)     \
    do {                                 \
        if ((ptr) != NULL) {             \
            free_fn(ptr);                \
            (ptr) = NULL;                \
        }                                \
    } while (0)

#define memdupa_suffix0(p, l)                             \
        ({                                                \
                void *_q_;                                \
                size_t _l_ = l;                           \
                assert(_l_ <= TRANSOCKS_ALLOCA_MAX);      \
                _q_ = alloca(_l_ + 1);                    \
                ((uint8_t*) _q_)[_l_] = 0;                \
                memcpy(_q_, p, _l_);                      \
        })

enum {
    GETOPT_VAL_TCPLISTENERADDRPORT,
    GETOPT_VAL_UDPLISTENERADDRPORT,
    GETOPT_VAL_SOCKS5ADDRPORT,
    GETOPT_VAL_PUMPMETHOD,
    GETOPT_VAL_TRANSPARENTMETHOD,
    GETOPT_VAL_HELP
};

typedef double transocks_timestamp;

int create_pipe(int *readfd, int *writefd);

void print_help(void);

transocks_timestamp transocks_now(void);

#endif //TRANSOCKS_WONG_UTIL_H
