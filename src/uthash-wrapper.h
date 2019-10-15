//
// Created by wong on 10/15/19.
//

#ifndef TRANSOCKS_WONG_UTHASH_WRAPPER_H
#define TRANSOCKS_WONG_UTHASH_WRAPPER_H

#define TRANSOCKS_SHOULD_INCLUDE_UTHASH_WRAPPER

#include "mem-allocator.h"

#define uthash_malloc(sz) tr_malloc(sz)      /* malloc fcn                      */

#define uthash_free(ptr,sz) tr_free(ptr)     /* free fcn                        */

#include "uthash.h"

#endif //TRANSOCKS_WONG_UTHASH_WRAPPER_H
