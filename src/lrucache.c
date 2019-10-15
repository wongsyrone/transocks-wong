//
// Created by wong on 10/15/19.
//
#include <stdlib.h>
#include <errno.h>
#include "lrucache.h"

/** Creates a new cache collection
 *
 *  @param dstCollection
 *  Where the newly allocated cache object will be stored in
 *
 *  @param capacity
 *  The maximum number of elements this cache collection can hold
 *
 *  @param entryFreeFn
 *  How to release resource of the entry
 *
 *  @return EINVAL if dst is NULL or entryFreeFn is NULL, ENOMEM if malloc fails, 0 otherwise
 */
lrucache_collection_t *lrucache_setup(const size_t capacity, lrucache_entry_free_fn entryFreeFn) {
    lrucache_collection_t *pCollection = NULL;

    // enforce free fn
    if (entryFreeFn == NULL) {
        return NULL;
    }
    if ((pCollection = tr_malloc(sizeof(lrucache_collection_t))) == NULL) {
        return NULL;
    }

    pCollection->capacity = capacity;
    pCollection->entries = NULL;
    pCollection->entry_free_fn = entryFreeFn;
    return pCollection;
}

/** Destory lru cache allocated via lrucache_setup()
 *
 *  @param collection
 *  The cache object to free
 *
 *  @return
 */
void lrucache_destory(lrucache_collection_t *collection) {
    lrucache_entry_t *entry, *tmp;

    if (collection == NULL) {
        return;
    }

    HASH_ITER(hh, collection->entries, entry, tmp) {
        HASH_DEL(collection->entries, entry);
        if (entry->data != NULL) {
            if (collection->entry_free_fn) {
                collection->entry_free_fn(entry->key, entry->data);
            }
        }
        TRANSOCKS_FREE(tr_free, entry->key);
        TRANSOCKS_FREE(tr_free, entry);
    }


    TRANSOCKS_FREE(tr_free, collection);
}

/** Clear old cache object
 *
 *  @param collection
 *  The lru cache collection to clear
 *
 *  @param age
 *  Clear only objects older than the age (sec)
 *
 *  @return EINVAL if cache is NULL, 0 otherwise
 */
int lrucache_clear(lrucache_collection_t *collection, transocks_timestamp age) {
    lrucache_entry_t *entry, *tmp;

    if (collection == NULL) {
        return EINVAL;
    }

    transocks_timestamp now = transocks_now();

    HASH_ITER(hh, collection->entries, entry, tmp) {
        if (now - entry->timestamp > age) {
            HASH_DEL(collection->entries, entry);
            if (entry->data != NULL) {
                if (collection->entry_free_fn) {
                    collection->entry_free_fn(entry->key, entry->data);
                }
            }
            TRANSOCKS_FREE(tr_free, entry->key);
            TRANSOCKS_FREE(tr_free, entry);
        }
    }

    return 0;
}

/** Checks if a given key is in the cache
 *
 *  @param collection
 *  The lru cache collection
 *
 *  @param key
 *  The key to look-up
 *
 *  @param key_len
 *  The length of key
 *
 *  @param result
 *  Where to store the result if key is found.
 *
 *  A warning: Even though result is just a pointer,
 *  you have to call this function with a **ptr,
 *  otherwise this will blow up in your face.
 *
 *  @return EINVAL if cache is NULL, 0 otherwise
 */
int lrucache_lookup(lrucache_collection_t *collection, char *key, size_t keyLen, void *result) {
    lrucache_entry_t *tmp = NULL;
    char **dirty_hack = result;

    if (!collection || !key || !result) {
        return EINVAL;
    }

    HASH_FIND(hh, collection->entries, key, keyLen, tmp);
    if (tmp) {
        HASH_DELETE(hh, collection->entries, tmp);
        tmp->timestamp = transocks_now();
        HASH_ADD_KEYPTR(hh, collection->entries, tmp->key, keyLen, tmp);
        *dirty_hack = tmp->data;
    } else {
        *dirty_hack = result = NULL;
    }

    return 0;
}

/** Inserts a given <key, value> pair into the cache
 *
 *  @param collection
 *  The lru collection object
 *
 *  @param key
 *  The key that identifies <value>
 *
 *  @param key_len
 *  The length of key
 *
 *  @param data
 *  Data associated with <key>
 *
 *  @return EINVAL if cache is NULL, ENOMEM if malloc fails, 0 otherwise
 */
int lrucache_insert(lrucache_collection_t *collection, char *key, size_t keyLen, void *data) {
    lrucache_entry_t *entry = NULL;
    lrucache_entry_t *tmp_entry = NULL;

    if (!collection) {
        return EINVAL;
    }

    if ((entry = tr_malloc(sizeof(lrucache_entry_t))) == NULL) {
        return ENOMEM;
    }

    entry->key = tr_malloc(sizeof(char) * (keyLen + 1));
    memcpy(entry->key, key, keyLen);
    entry->key[keyLen] = '\0';

    entry->data = data;
    entry->timestamp = transocks_now();
    HASH_ADD_KEYPTR(hh, collection->entries, entry->key, keyLen, entry);

    if (HASH_COUNT(collection->entries) >= collection->capacity) {
        HASH_ITER(hh, collection->entries, entry, tmp_entry) {
            HASH_DELETE(hh, collection->entries, entry);
            if (entry->data != NULL) {
                if (collection->entry_free_fn) {
                    collection->entry_free_fn(entry->key, entry->data);
                }
            }
            TRANSOCKS_FREE(tr_free, entry->key);
            TRANSOCKS_FREE(tr_free, entry);
            break;
        }
    }

    return 0;
}

/** Removes a cache entry
 *
 *  @param collection
 *  The collection object
 *
 *  @param key
 *  The key of the entry to remove
 *
 *  @param key_len
 *  The length of key
 *
 *  @return EINVAL if cache is NULL, 0 otherwise
 */
int lrucache_remove(lrucache_collection_t *collection, char *key, size_t keyLen) {
    lrucache_entry_t *tmp = NULL;

    if (!collection || !key) {
        return EINVAL;
    }

    HASH_FIND(hh, collection->entries, key, keyLen, tmp);

    if (tmp != NULL) {
        HASH_DEL(collection->entries, tmp);
        if (tmp->data != NULL) {
            if (collection->entry_free_fn) {
                collection->entry_free_fn(tmp->key, tmp->data);
            }
        }
        TRANSOCKS_FREE(tr_free, tmp->key);
        TRANSOCKS_FREE(tr_free, tmp);
    }

    return 0;
}

/** Update timestamp if key already exists
 *
 *  @param collection
 *  The collection object
 *
 *  @param key
 *  The key of the entry to remove
 *
 *  @param key_len
 *  The length of key
 *
 *  @return EINVAL if cache is NULL, 0 otherwise
 */
int lrucache_touch_if_key_exist(lrucache_collection_t *collection, char *key, size_t keyLen) {
    lrucache_entry_t *tmp = NULL;

    if (collection == NULL || key == NULL) {
        return EINVAL;
    }

    HASH_FIND(hh, collection->entries, key, keyLen, tmp);
    if (tmp != NULL) {
        HASH_DELETE(hh, collection->entries, tmp);
        tmp->timestamp = transocks_now();
        HASH_ADD_KEYPTR(hh, collection->entries, tmp->key, keyLen, tmp);
        return 1;
    }

    return 0;
}