
#ifndef ASTREE_H
#define ASTREE_H

#include "symboltable.h"
#include "node.h"
#include "lib/hashmap.h"
#include "lib/sds.h"
#include "lib/defs.h"

void node_free(ASTNode *n);

// struct ssindex {
// 	size_t start;
// 	size_t len;
// };

// symboltable_add
typedef struct struct_element {
	Symbol *name;
	enum symbol_type type;
} Element;


typedef struct astree {
	ASTNode *root;
	SymbolTable *symboltable;
	int is_valid; //bool
} ASTree;

ASTree *astree_create(size_t table_size);
void astree_free(ASTree *tree);

// union symbol_value
// Symbol
// union symbol_data {
// 	Symbol *symbol;
// 	LinkedList/*attribute*/ *parameters; // for functions
// };

// data is disambiguated thus:
// if type is struct, linkedlist*<element>
// if type is function, linkedlist*<attribute>
typedef struct attribute {
	enum symbol_type type;
	void *data; // Symbol*, LinkedList*<attribute>, LinkedList*<element>
	int offset;
	// union symbol_data data;
	// u16 row;
	// u16 col;
} Attribute;

Attribute *astree_attribute_create(ASTree *ast, enum symbol_type type, void *data);

Symbol *astree_add_symbol(ASTree *ast, sds iden, u16 scope);
Symbol *astree_get_symbol(ASTree *ast, sds iden, u16 scope);

int astree_add_attribute(ASTree *ast, Symbol *key, Attribute *atr);
Attribute *astree_get_attribute(ASTree *ast, Symbol *key);

#endif

// enum symbol_type {
// 	SNONE, SIDEN, SFUNC, SREAL, SINT, SBOOL, SARRAY, SSTRUCT
// };

