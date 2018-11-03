//
// Created by wong on 10/25/18.
//

#ifndef TRANSOCKS_WONG_LOG_H
#define TRANSOCKS_WONG_LOG_H

#include <stdarg.h>
#include <stdbool.h>

#include "util.h"

enum LOGLEVEL {
    LOG_ERR,
    LOG_INFO,
    LOG_DEBUG
};

#ifdef TRANSOCKS_DEBUG
void dump_data(char *tag, char *text, int len);
#else
#define dump_data(tag, text, len)
#endif

#define log_errno(prio, msg...) _log_write(__FILE__, __LINE__, __func__, 1, prio, ## msg)
#define log_error(prio, msg...) _log_write(__FILE__, __LINE__, __func__, 0, prio, ## msg)

#ifdef TRANSOCKS_DEBUG
#define LOGD(msg...) log_error(LOG_DEBUG, msg)
#define LOGD_ERRNO(msg...) log_errno(LOG_DEBUG, msg)
#else
#define LOGD(msg...)
#define LOGD_ERRNO(msg...)
#endif

#define LOGI(msg...) log_error(LOG_INFO, msg)

#define LOGE(msg...) log_error(LOG_ERR, msg)
#define LOGE_ERRNO(msg...) log_errno(LOG_ERR, msg)

#define FATAL(msg...) do { LOGE(msg); exit(EXIT_FAILURE); } while(0)
#define FATAL_ERRNO(msg...) do { LOGE_ERRNO(msg); exit(EXIT_FAILURE); } while(0)
#define FATAL_WITH_HELPMSG(msg...) do { LOGE(msg); print_help(); exit(EXIT_FAILURE); } while(0)
#define PRINTHELP_EXIT() do { print_help(); exit(EXIT_FAILURE); } while(0)

void _log_write(const char *file, int line, const char *func, int do_errno, int priority, const char *fmt, ...)
#if defined(__GNUC__)
__attribute__ (( format (printf, 6, 7) ))
#endif
;


#endif //TRANSOCKS_WONG_LOG_H
