//
// Created by wong on 10/25/18.
//

#include "log.h"

static char *loglevel_str[] = {
        [LOG_ERR] = "err",
        [LOG_INFO] = "info",
        [LOG_DEBUG] = "debug"
};

void _log_write(FILE *fd, const char *file, int line, const char *func,
                bool do_errno, int priority, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    int saved_errno = errno;
    struct timespec tv;
    clock_gettime(CLOCK_REALTIME, &tv);

    // header
    fprintf(fd, "%ld.%6.6ld %s %s:%d %s() ", tv.tv_sec, tv.tv_nsec / 1000 /* to microseconds */,
            loglevel_str[priority], file, line, func);

    // message
    vfprintf(fd, fmt, ap);
    va_end(ap);

    // appendix
    if (do_errno) {
        fprintf(fd, ": %s\n", strerror(saved_errno));
    } else {
        fprintf(fd, "\n");
    }
}

#ifdef TRANSOCKS_DEBUG
void dump_data(char *tag, char *text, int len)
{
    int i;
    printf("%s: ", tag);
    for (i = 0; i < len; i++)
        printf("0x%02x ", (uint8_t)text[i]);
    printf("\n");
}
#endif
