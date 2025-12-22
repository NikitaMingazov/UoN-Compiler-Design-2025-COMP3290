#ifndef TAC_H
#define TAC_H

#include "astree.h"
#include "lib/linkedlist.h"

enum operation {
	O_PRINTI,
	O_PRINTF,
	O_PRINTSTR,
	O_PRINTLN,
	O_PRINTSPC,
	O_READI,
	O_READF,
	O_ITOF,
	O_LABEL,
	O_GOTO,
	O_GOTOT,
	O_GOTOF,
	O_ALLOC,
	O_ASIGN,
	O_ADDI,
	O_ADDF,
	O_SUBI,
	O_SUBF,
	O_MULI,
	O_MULF,
	O_DIVI,
	O_DIVF,
	O_MOD,
	O_POWII,
	O_POWIF,
	O_TRUE,
	O_FALSE,
	O_EQ,
	O_NEQ,
	O_LT,
	O_LTE,
	O_GT,
	O_GTE,
	O_AND,
	O_OR,
	O_XOR,
	O_NOT,
	O_PARAM,
	O_CALL,
	O_CALLVAL,
	O_RETN,
	O_RVAL,
	O_FUNC,
};

static char print_op[50][14] = {
	"print_i", "print_f", "print_str", "print_ln", "print_space",
	"read_i", "read_f", "itof",
	"label", "goto", "goto_if_true",  "goto_if_false", "alloc",
	"let", "add_i", "add_f", "sub_i", "sub_f", "mul_i", "mul_f", "div_i", "div_f", "pow_ii", "pow_if", "mod",
	"true", "false", "==", "!=", "<", "<=", ">", ">=",
	"and", "or", "xor", "not",
	"param", "call", "call_v", "return", "return value", "function",
};

enum adr_type {
	A_TMP,
	A_LABEL,
	A_VAR,
	A_PARAM,
	A_ILIT,
	A_FLIT,
	A_STR,
	A_CONST,
	A_EMPTY,
};

typedef struct address {
	enum adr_type type;
	u16 adr;
} Adr;

// left, then right, then middle for 1/2/3 address operations
typedef struct tac_line {
	enum operation op;
	Adr left; // variables are numbers
	Adr middle;
	Adr right;
} Line;

typedef struct threeaddresscode {
	LinkedList *lines;
	LinkedList *strings;
	LinkedList *floats;
	LinkedList *ints;
} TAC;

TAC *tac_from_ast(ASTree *ast);

void tac_free(TAC* tac);
void tac_printf(TAC* tac);

#endif

