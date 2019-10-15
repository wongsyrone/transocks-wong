//
// Created by wong on 10/15/19.
//

#ifndef TRANSOCKS_WONG_LRUCACHE_H
#define TRANSOCKS_WONG_LRUCACHE_H

#include "mem-allocator.h"

#include "uthash-wrapper.h"
#include "util.h"

// single cache entry
typedef struct lrucache_entry lrucache_entry_t;

// cache collection that holds cache entries
typedef struct lrucache_collection lrucache_collection_t;

// how to release the payload attached to the key
typedef void (*lrucache_entry_free_fn)(void *key, void *element);

typedef struct lrucache_entry {
    char *key;   // the key
    void *data;  // payload
    transocks_timestamp timestamp;
    UT_hash_handle hh; // hash handle for uthash
} lrucache_entry_t;

typedef struct lrucache_collection {
    size_t capacity;          // max entries this lru cache can hold
    lrucache_entry_t *entries; // head ptr for uthash
    lrucache_entry_free_fn entry_free_fn;
} lrucache_collection_t;


lrucache_collection_t *lrucache_setup(size_t capacity, lrucache_entry_free_fn entryFreeFn);

void lrucache_destory(lrucache_collection_t *collection);

int lrucache_clear(lrucache_collection_t *collection, transocks_timestamp age);

int lrucache_lookup(lrucache_collection_t *collection, char *key, size_t keyLen, void *result);

int lrucache_insert(lrucache_collection_t *collection, char *key, size_t keyLen, void *data);

int lrucache_remove(lrucache_collection_t *collection, char *key, size_t keyLen);

int lrucache_touch_if_key_exist(lrucache_collection_t *collection, char *key, size_t keyLen);

#endif //TRANSOCKS_WONG_LRUCACHE_H
