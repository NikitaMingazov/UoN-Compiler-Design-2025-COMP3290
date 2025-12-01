#include "hashmap.h"
#include <stdlib.h>

#include <stdio.h>

Entry *entry_create(void *key, void *val) {
	Entry *entry = malloc(sizeof(Entry));
	if (!entry) return NULL;
	entry->key = key;
	entry->val = val;
	return entry;
}

void entry_free(void *ptr, void (*free_key)(void*), void (*free_val)(void*)) {
	Entry *entry = (Entry *)ptr;
	free_key(entry->key);
	free_val(entry->val);
	free(entry);
}

HashMap *hashmap_create(u32 size, hash_func_t hash_func, equals_func_t equals_func) {
	HashMap *ht = malloc(sizeof(HashMap));
	if (!ht) return NULL;

	ht->size = size;
	ht->table = calloc(size, sizeof(LinkedList*));
	if (!ht->table) {
		free(ht);
		return NULL;
	}

	for (u32 i = 0; i < size; ++i) {
		ht->table[i] = linkedlist_create();
	}

	ht->hash_func = hash_func;
	ht->equals_func = equals_func;
	return ht;
}

static void entry_free_with_context(void *entry_ptr, va_list args) {
	void (*free_key)(void*) = va_arg(args, void (*)(void*));
	void (*free_val)(void*) = va_arg(args, void (*)(void*));

	Entry *entry = (Entry*)entry_ptr;
	entry_free(entry, free_key, free_val);
}

void hashmap_free(HashMap *ht, void (*free_key)(void*), void (*free_val)(void*)) {
	if (!ht) return;
	for (u32 i = 0; i < ht->size; ++i) {
		linkedlist_free_ctx(ht->table[i], entry_free_with_context, free_key, free_val);
	}
	free(ht->table);
	free(ht);
}

void hashmap_add(HashMap *ht, void *key, void *val) {
	if (!ht || !key) return;

	u32 index = ht->hash_func(key) % ht->size;
	Entry *entry = entry_create(key, val);
	linkedlist_push_tail(ht->table[index], entry);
}

int hashmap_contains(const HashMap *ht, void *key) {
	if (!ht || !key) return 0;

	u32 index = ht->hash_func(key) % ht->size;
	LinkedList *list = ht->table[index];

	linkedlist_start(list);
	while (linkedlist_get_current(list) != NULL) {
		Entry *entry = (Entry *)linkedlist_get_current(list);
		if (ht->equals_func(entry->key, key)) {
			return 1;
		}
		linkedlist_forward(list);
	}
	return 0;
}

void *hashmap_get(const HashMap *ht, void *key) {
	if (!ht || !key) return NULL;

	u32 index = ht->hash_func(key) % ht->size;
	LinkedList *list = ht->table[index];

	linkedlist_start(list);
	while (linkedlist_get_current(list) != NULL) {
		Entry *entry = (Entry *)linkedlist_get_current(list);
		if (ht->equals_func(entry->key, key)) {
			return entry->val;
		}
		linkedlist_forward(list);
	}
	return NULL;
}

LinkedList *hashmap_bucket_at(const HashMap *ht, void *key) {
	if (!ht || !key) return NULL;
	u32 index = ht->hash_func(key) % ht->size;
	return ht->table[index];
}

void hashmap_remove(HashMap *ht, void *key, void (*free_key)(void*), void (*free_val)(void*)) {
	if (!ht || !key) return;

	u32 index = ht->hash_func(key) % ht->size;
	LinkedList *list = ht->table[index];

	linkedlist_start(list);
	while (linkedlist_get_current(list) != NULL) {
		Entry *entry = (Entry *)linkedlist_get_current(list);
		if (ht->equals_func(entry->key, key)) {
			Entry *removed = linkedlist_pop_current(list);
			entry_free(removed, free_key, free_val);
			return;
		}
		linkedlist_forward(list);
	}
}

// LinkedList *hashtable_get_bucket(HashTable *ht, void *item) {
// 	return ht->table[ht->hashfunc(item) % ht->size];
// }

