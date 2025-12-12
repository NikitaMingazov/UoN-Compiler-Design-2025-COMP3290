#ifndef TAC_H
#define TAC_H

#include "astree.h"

// label L1
// label L2
// goto L1
enum operation {
	O_PRINT,
	O_ADD,
};

// left, then right, then middle for 1/2/3 address operations
typedef struct tac_line {
	Symbol *left;
	Symbol *middle;
	Symbol *right;
	enum operation op;
} Line;

typedef struct threeaddresscode {
	Line *lines;
	size_t num_lines;
	char **strings;
	size_t num_strings;
} TAC;

TAC *tac_from_ast(ASTree *ast);

void tac_free(TAC* tac);
void tac_printf(TAC* tac);

#endif

