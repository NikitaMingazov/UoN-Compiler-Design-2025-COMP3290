
#include <string.h>
#include "astree.h"
#include "node.h"
#include <stdlib.h>
#include "lib/hashmap.h"
#include "lib/linkedlist.h" // linkedlist_free
#include "lib/sds.h"

static unsigned hash_str(const void *ptr) {
	char *s = (char *) ptr;
	unsigned hashval;
	for (hashval = 0; *s != '\0'; s++)
		hashval = *s + 31 * hashval;
	return hashval;
}
static int equal_str(const void *a, const void *b) {
	return strcmp((char *)a, (char *)b) == 0;
}

static unsigned hash_symbol(const void *ptr) {
	const Symbol *sym = (const Symbol *)ptr;
	unsigned hash = 17;
	hash = hash * 31 + (unsigned)(sym->index.start ^ (sym->index.start >> 32));
	hash = hash * 31 + (unsigned)(sym->index.len ^ (sym->index.len >> 32));
	hash = hash * 31 + sym->scope;
	return hash;
}

static int equal_symbol(const void *a, const void *b) {
	const Symbol *s1 = (const Symbol *)a;
	const Symbol *s2 = (const Symbol *)b;
	return s1->index.start == s2->index.start &&
	       s1->index.len   == s2->index.len &&
	       s1->scope       == s2->scope;
}

static sds buffer_append(sds buf, sds str) {
	if (sdsavail(buf) < sdslen(str)) {
	    buf = sdsMakeRoomFor(buf, sdslen(str));
	}
	buf = sdscatsds(buf, str);
	return buf;
}

static struct sindex make_index(size_t start, size_t len) {
	struct sindex new = { start, len };
	return new;
}

Symbol *make_symbol(struct sindex index, u16 scope) {
	Symbol *new = malloc(sizeof(Symbol));
	new->index = index;
	new->scope = scope;
	return new;
}

Symbol *symboltable_add(SymbolTable *st, sds iden, u16 scope) {
	if(!hashmap_contains(st->seen_idens, iden)) {
		size_t start = sdslen(st->stringspace);
		st->stringspace = buffer_append(st->stringspace, iden);
		struct sindex *index = malloc(sizeof(struct sindex));
		*index = make_index(start, sdslen(st->stringspace) - start);
		hashmap_add(st->seen_idens, sdsdup(iden), index);
	}
	struct sindex *index = hashmap_get(st->seen_idens, iden);
	Symbol* new_symbol = make_symbol(*index, scope);
	if (!hashmap_contains(st->live_pointers, new_symbol)) {
		hashmap_add(st->live_pointers, new_symbol, NULL);
	}
	// hashmap_add(st->table, new_symbol, attributes);
	// LinkedList *stack = hashmap_bucket_at(st->table, iden);
	// this suffices here, but for a3 checks using symbol_equals(s0, s1) will be needed
	return new_symbol; // for parser to add to AST
}

Symbol *astree_add_symbol(ASTree *ast, sds iden, u16 scope) {
	return symboltable_add(ast->symboltable, iden, scope);
}

Symbol *astree_get_symbol(ASTree *ast, sds iden, u16 scope) {
	struct sindex *index = hashmap_get(ast->symboltable->seen_idens, iden);
	if (!index)
		return NULL;
	return make_symbol(*index, scope);
}

int symboltable_add_attribute(SymbolTable *st, Symbol *key, Attribute *atr) {
	if(!hashmap_contains(st->table, key)) {
		hashmap_add(st->table, key, atr);
		if (!hashmap_contains(st->live_pointers, key)) {
			hashmap_add(st->live_pointers, key, NULL);
		}
		return 0;
	} else {
		return 1; // duplicate symbol - semantic error
	}
}

int astree_add_attribute(ASTree *ast, Symbol *key, Attribute *atr) {
	return symboltable_add_attribute(ast->symboltable, key, atr);
}

Attribute *astree_get_attribute(ASTree *ast, Symbol *key) {
	if (!key) return NULL;
	Attribute *result = hashmap_get(ast->symboltable->table, key);
	if (result)
		return result;
	Symbol global_scoped = *key; // it might be in the global scope, so give that a try
	global_scoped.scope = 0;
	return hashmap_get(ast->symboltable->table, &global_scoped);
}

static inline unsigned hash_ptr(const void *p) {
	uintptr_t x = (uintptr_t)p;
	return (unsigned)(x>>4) ^ (unsigned)(x * 2654435761u);
}

static inline int equal_ptr(const void *a, const void *b) {
	return a == b;
}

SymbolTable *symboltable_create(size_t table_size) {
	SymbolTable *temp = malloc(sizeof(SymbolTable));
	temp->stringspace = sdsnewlen(NULL, 32);
	// temp->table_size = table_size;
	temp->seen_idens = hashmap_create(table_size, hash_str, equal_str);
	temp->table = hashmap_create(table_size, hash_symbol, equal_symbol);
	temp->live_pointers = hashmap_create(50, hash_ptr, equal_ptr);
	return temp;
};

// wrapper function to store symbols in the resource management map
Attribute *astree_attribute_create(ASTree *ast, enum symbol_type type, void *data) {
	Attribute *atr = malloc(sizeof(Attribute));
	atr->type = type;
	if (type == SARRAY || type == SSTRUCT) {
		if (!hashmap_contains(ast->symboltable->live_pointers, (Symbol *)data)) {
			hashmap_add(ast->symboltable->live_pointers, (Symbol *)data, NULL);
		}
		atr->data = data;
	} else {
		atr->data = NULL;
	}
	atr->offset = -1;
	atr->is_param = 0;
	return atr;
}

static void sdsfree_wrapper(void *ptr) {
	sdsfree((sds)ptr);
}

static void free_noop(void *ptr) {
    (void)ptr; // do nothing (not the owner)
}

static void free_attribute(void *ptr) {
	// if (!ptr) return;
	Attribute *atr = (Attribute *)ptr;
	switch (atr->type) {
		case SARRAY:
		case SSTRUCT:
			free(atr->data);
			break;
		case SFIELDS:
			linkedlist_free(atr->data);
			break;
		case SINT:
		case SBOOL:
		case SREAL:
		case SVOID:
			if (atr->data)
				linkedlist_free(atr->data);
			break;
	}
	free(atr);
}

void symboltable_free(SymbolTable *st) {
	sdsfree(st->stringspace);
	hashmap_free(st->seen_idens, sdsfree_wrapper, free);
	hashmap_free(st->table, free_noop, free_attribute);
	hashmap_free(st->live_pointers, free, free_noop);
	free(st);
}

void node_free(ASTNode *n) {
	if (!n) return;
	// node does not own the symbol it holds
	node_free(n->left_child);
	node_free(n->middle_child);
	node_free(n->right_child);
	free(n);
}

ASTree *astree_create(size_t table_size) {
	ASTree *temp = malloc(sizeof(ASTree));
	temp->root = NULL;
	temp->symboltable = symboltable_create(table_size);
	temp->is_valid = 1;
	return temp;
}

void astree_free(ASTree *tree) {
	node_free(tree->root);
	symboltable_free(tree->symboltable);
	free(tree);
}

void astree_set_offset(ASTree *ast, Symbol *key, u16 val) {
	Attribute *atr = astree_get_attribute(ast, key);
	atr->offset = val;
}

u16 astree_get_offset(ASTree *ast, Symbol *key) {
	Attribute *atr = astree_get_attribute(ast, key);
	return atr->offset;
}

void astree_mark_param(ASTree *ast, Symbol *key) {
	Attribute *atr = astree_get_attribute(ast, key);
	atr->is_param = 1;
}

int astree_is_param(ASTree *ast, Symbol *key) {
	Attribute *atr = astree_get_attribute(ast, key);
	return atr->is_param;
}
