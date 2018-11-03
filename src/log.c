//
// Created by wong on 10/25/18.
//

#include <errno.h>
#include <event2/buffer.h>

#include "log.h"

static const char* getprioname(int priority)
{
    switch (priority) {
        case LOG_ERR:     return "err";
        case LOG_INFO:    return "info";
        case LOG_DEBUG:   return "debug";
        default:          return "?";
    }
}

static void fprint_timestamp(
        FILE* fd,
        const char *file, int line, const char *func, int priority, const char *message, const char *appendix)
{
    struct timeval tv = { };
    gettimeofday(&tv, 0);

    /* XXX: there is no error-checking, IMHO it's better to lose messages
     *      then to die and stop service */
    const char* sprio = getprioname(priority);
    if (appendix)
        fprintf(fd, "%ld.%6.6ld %s %s:%d %s() %s: %s\n", tv.tv_sec, tv.tv_usec, sprio, file, line, func, message, appendix);
    else
        fprintf(fd, "%ld.%6.6ld %s %s:%d %s() %s\n", tv.tv_sec, tv.tv_usec, sprio, file, line, func, message);
}

static void stderr_msg(const char *file, int line, const char *func, int priority, const char *message, const char *appendix)
{
    fprint_timestamp(stderr, file, line, func, priority, message, appendix);
}

void _log_write(const char *file, int line, const char *func, int do_errno, int priority, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);

    int saved_errno = errno;
    struct evbuffer *buff = evbuffer_new();
    const char *message;

    if (buff) {
        evbuffer_add_vprintf(buff, fmt, ap);
        message = (const char*)evbuffer_pullup(buff, -1);
    }
    else
        message = "fail to allocate mem.";

    stderr_msg(file, line, func, priority, message, do_errno ? strerror(saved_errno) : NULL);

    if (buff)
        evbuffer_free(buff);

    va_end(ap);
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
