// implementation of a hashmap

#ifndef HASHMAP_H
#define HASHMAP_H

#include <stddef.h>
#include "defs.h"
#include "linkedlist.h"

// the hash function passed is not modular, size bounding is done by the table
typedef u32 (*hash_func_t)(const void *key);
typedef int (*equals_func_t)(const void *a, const void *b);

typedef struct entry {
    void *key;
    void *val;
} Entry;

Entry *entry_create(void *key, void *val);
void entry_free(void *entry, void (*free_key)(void*), void (*free_val)(void*));

typedef struct hashmap {
    u32 size;
    LinkedList **table;
    hash_func_t hash_func;
    equals_func_t equals_func;
} HashMap;

HashMap *hashmap_create(u32 size, hash_func_t hash_func, equals_func_t equals_func);
void hashmap_free(HashMap *ht, void (*free_key)(void*), void (*free_val)(void*));

void hashmap_add(HashMap *ht, void *key, void *val);
void hashmap_remove(HashMap *ht, void *key, void (*free_key)(void*), void (*free_val)(void*));

int hashmap_contains(const HashMap *ht, void *key);
void *hashmap_get(const HashMap *ht, void *key);
LinkedList *hashmap_bucket_at(const HashMap *ht, void *key);

#endif

