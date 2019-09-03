//
// Created by wong on 9/3/19.
//

#ifndef TRANSOCKS_WONG_MEM_ALLOCATOR_H
#define TRANSOCKS_WONG_MEM_ALLOCATOR_H

#if defined(TRANSOCKS_ALLOCATOR_USE_SYSTEM)
#define TR_USED_MEM_ALLOCATOR "system"

#define tr_malloc(size) malloc((size))
#define tr_calloc(count, size) calloc((count), (size))
#define tr_realloc(p, newsize) realloc((p), (newsize))
#define tr_expand(p, newsize) expand((p), (newsize))
#define tr_free(p) free((p))
#define tr_strdup(s) strdup((s))
#define tr_strndup(s, n) strndup((s), (n))
#define tr_realpath(fname, resolved_name) realpath((fname), (resolved_name))

#elif defined(TRANSOCKS_ALLOCATOR_USE_MIMALLOC)
#define TR_USED_MEM_ALLOCATOR "mimalloc"
#include <mimalloc.h>

#define tr_malloc(size) mi_malloc((size))
#define tr_calloc(count, size) mi_calloc((count), (size))
#define tr_realloc(p, newsize) mi_realloc((p), (newsize))
#define tr_expand(p, newsize) mi_expand((p), (newsize))
#define tr_free(p) mi_free((p))
#define tr_strdup(s) mi_strdup((s))
#define tr_strndup(s, n) mi_strndup((s), (n))
#define tr_realpath(fname, resolved_name) mi_realpath((fname), (resolved_name))

#else
#error memory allocator not specified
#endif

#endif //TRANSOCKS_WONG_MEM_ALLOCATOR_H
