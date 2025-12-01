
#ifndef SYMBOLTABLE_H
#define SYMBOLTABLE_H

#include "lib/sds.h"
#include "lib/hashmap.h"
#include "lib/defs.h"

typedef struct symboltable {
	sds stringspace;
	HashMap *seen_idens; /* string -> index */
	HashMap *table; /* sds -> symbol */
	HashMap *live_pointers; /* symbol* -> null */
} SymbolTable;

struct sindex {
	size_t start;
	size_t len;
};

typedef struct symbol {
	struct sindex index;
	u16 scope;
} Symbol;

#endif

