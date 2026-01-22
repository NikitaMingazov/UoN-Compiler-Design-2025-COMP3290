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
	O_EQI,
	O_NEQI,
	O_LTI,
	O_LTEI,
	O_GTI,
	O_GTEI,
	O_EQF,
	O_NEQF,
	O_LTF,
	O_LTEF,
	O_GTF,
	O_GTEF,
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
	O_DEREF,
	O_STORE,
};

static char print_op[51][14] = {
	"print_i", "print_f", "print_str", "print_ln", "print_space",
	"read_i", "read_f", "itof",
	"label", "goto", "goto_if_true",  "goto_if_false", "alloc",
	"let", "add_i", "add_f", "sub_i", "sub_f", "mul_i", "mul_f", "div_i", "div_f", "mod", "pow_ii", "pow_if",
	"true", "false",
	"==i", "!=i", "<i", "<=i", ">i", ">=i",
	"==f", "!=f", "<f", "<=f", ">f", ">=f",
	"and", "or", "xor", "not",
	"param", "call", "call_v", "return", "return value", "function",
	"deref", "store",
};

enum adr_type {
	A_TMP,
	A_LABEL,
	A_VAR,
	A_PARAM,
	A_ARRAY,
	A_ILIT,
	A_FLIT,
	A_STR,
	A_EMPTY,
};

static char adr_prefix[9][2] = {
	"T", "L", "V", "P", "A", "I", "F", "S", "",
};

/* enum val_type { */
/* 	v_u8, */
/* 	v_u64, */
/* 	v_f64, */
/* }; */

typedef struct address {
	enum adr_type type;
	/* enum val_type val_type */
	u16 adr;
} Adr;

// left, then right, then middle for 1/2/3 address operations
typedef struct tac_line {
	enum operation op;
	Adr left; // no named variables in the IR
	Adr middle;
	Adr right;
	u16 linenum;
} Line;

typedef struct threeaddresscode {
	LinkedList *lines; // Line
	LinkedList *strings; // char*
	LinkedList *floats; // double
	LinkedList *ints; // long
	LinkedList *arrays; // int
} TAC;

TAC *tac_from_ast(ASTree *ast);

void tac_free(TAC* tac);
void tac_printf(TAC* tac);

/* it's up to the user to cast the type (it's in the adr after all) */
void* tac_data(TAC* tac, Adr adr);

#endif

