#include "threeaddresscode.h"
#include "astree.h"
#include "node.h"
#include "symboltable.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "lib/linkedlist.h"
#include "lib/sds.h"
#include "lib/defs.h"
#include "lib/hashmap.h"
#include <stdio.h> // used in tac_printf

typedef struct tac_state {
	TAC *tac;
	ASTree *ast;
	HashMap *seen_strings; // due to bad design earlier, literals aren't in symbol table
	HashMap *seen_intvals; // <val: int, offset: int>
	HashMap *seen_floatvals; // <val: double, offset: int>
	// function local
	u16 temp_reg_counter;
	u16 label_counter;
	// global
	u16 string_counter;
	u16 float_counter;
	u16 int_counter;
} T_S;

// predefinitions
void tac_gen_stats(T_S *ts, ASTNode *node);
void tac_gen_sdecl(T_S *ts, ASTNode *node);
Adr tac_get_adr(T_S *ts, ASTNode *node);
Adr tac_gen_fncall(T_S *ts, ASTNode *node);
long *heap_long(long k);
double *heap_double(double x);

// abstracted away because I might want to use dynamic arrays later
void append_int(T_S *ts, long val) {
	linkedlist_push_tail(ts->tac->ints, heap_long(val));
}

void append_float(T_S *ts, double val) {
	linkedlist_push_tail(ts->tac->floats, heap_double(val));
}

// takes ownership
void append_sds(T_S *ts, sds str) {
	linkedlist_push_tail(ts->tac->strings, str);
}

void append_line(T_S *ts, Line *line) {
	linkedlist_push_tail(ts->tac->lines, line);
}

static sds sds_substr(const sds src, size_t start, size_t len) {
	size_t src_len = sdslen(src);
	if (start >= src_len) return sdsnew("");  // empty string if start past end
	if (start + len > src_len) len = src_len - start;
	return sdsnewlen(src + start, len);
}

static sds sds_from_symbol(ASTree *ast, Symbol *s) {
	return sds_substr(
		ast->symboltable->stringspace,
		s->index.start,
		s->index.len
	);
}

static int sym_to_int(ASTree *ast, Symbol *s) {
	sds str = sds_from_symbol(ast, s);
	int val = atoi(str);
	sdsfree(str);
	return val;
}

static double sym_to_double(ASTree *ast, Symbol *s) {
	sds str = sds_from_symbol(ast, s);
	double val = atof(str);
	sdsfree(str);
	return val;
}

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

static unsigned hash_i64(const void *ptr) {
	const i64 *val = (const i64 *)ptr;
	return *val * 7;
}
static int equal_i64(const void *a, const void *b) {
	const i64 *i1 = (const i64 *)a;
	const i64 *i2 = (const i64 *)b;
	return *i1 == *i2;
}

// hashes aren't colliding when they should
// TODO: make this actually work
static inline u64 quantise_double(double x) {
	return (u64) floor(x / 0.0001);
}

static unsigned hash_double(const void *ptr) {
	u64 quantised = quantise_double(*(double*)ptr);
	quantised *= 11400714819323198485ull;
	return (unsigned) quantised;
}

static int equal_double(const void *x, const void *y) {
	u64 a = quantise_double(*(double*)x);
	u64 b = quantise_double(*(double*)y);
	return a == b;
}

Adr blank() {
	return (Adr) {A_EMPTY, 0};
}

Adr mkadr(enum adr_type type, u16 adr) {
	return (Adr) {type, adr};
}

Adr mktmp(T_S *ts) {
	return mkadr(A_TMP, ts->temp_reg_counter++);
}

Adr mklabel(T_S *ts) {
	return mkadr(A_LABEL, ts->label_counter++);
}

Line *ternary_line(enum operation op, Adr left, Adr middle, Adr right) {
	Line *new = malloc(sizeof(Line));
	*new = (Line) { op, left, middle, right };
	return new;
}

Line *binary_line(enum operation op, Adr left, Adr right) {
	Line *new = malloc(sizeof(Line));
	*new = (Line) {op, left, blank(), right};
	return new;
}

Line *unary_line(enum operation op, Adr left) {
	Line *new = malloc(sizeof(Line));
	*new = (Line) {op, left, blank(), blank()};
	return new;
}

Line *nonary_line(enum operation op) {
	Line *new = malloc(sizeof(Line));
	*new = (Line) {op, blank(), blank(), blank()};
	return new;
}

long *heap_long(long k) {
	long *hk = malloc(sizeof(long));
	*hk = k;
	return hk;
}

u16 *heap_u16(u16 k) {
	u16 *hk = malloc(sizeof(u16));
	*hk = k;
	return hk;
}

double *heap_double(double x) {
	double *hx = malloc(sizeof(double));
	*hx = x;
	return hx;
}

Adr adr_of_int(T_S *ts, const int val) {
	u16 adr;
	if (hashmap_get(ts->seen_intvals, heap_long(val))) {
		adr = * (u16*) hashmap_get(ts->seen_intvals, heap_long(val));
	} else {
		append_int(ts, val);
		adr = ts->int_counter++;
		hashmap_add(ts->seen_intvals, heap_long(val), heap_u16(adr));
	}
	return mkadr(A_ILIT, adr);
}

Adr adr_of_double(T_S *ts, double val) {
	u16 adr;
	if (hashmap_get(ts->seen_floatvals, heap_double(val))) {
		adr = * (u16*) hashmap_get(ts->seen_floatvals, heap_double(val));
	} else {
		adr = ts->float_counter;
		append_float(ts, val);
		hashmap_add(ts->seen_floatvals, heap_double(val), heap_long(ts->float_counter++));
	}
	return mkadr(A_FLIT, adr);
}

// takes ownership of the passed string
Adr adr_of_sds(T_S *ts, sds s) {
	i16 adr;
	if (hashmap_get(ts->seen_strings, s)) {
		adr = * (u16*) hashmap_get(ts->seen_strings, s);
	} else {
		append_sds(ts, sdsdup(s));
		adr = ts->string_counter++;

		hashmap_add(ts->seen_strings, sdsdup(s), heap_u16(adr));
	}
	sdsfree(s);
	return mkadr(A_STR, adr);
}

TAC *tac_create() {
	TAC *tac = malloc(sizeof(TAC));
	tac->lines = linkedlist_create();
	tac->strings = linkedlist_create();
	tac->floats = linkedlist_create();
	tac->ints = linkedlist_create();
	return tac;
}

T_S *t_s_create(TAC *tac, ASTree *ast) {
	T_S *new = malloc(sizeof(T_S));
	*new = (T_S) {0};
	new->tac = tac;
	new->ast = ast;
	new->seen_strings = hashmap_create(200, hash_str, equal_str);
	new->seen_intvals = hashmap_create(200, hash_i64, equal_i64);
	new->seen_floatvals = hashmap_create(200, hash_double, equal_double);
	return new;
}

Adr tac_resolve_numeric(T_S *ts, ASTNode *node) {
	enum operation op;
	int promote_left = 0;
	int promote_right = 0;
	switch (node->type) {
		case NADD:
			if (node->symbol_type == SINT) {
				op = O_ADDI;
			} else {
				op = O_ADDF;
			}
			break;
		case NSUB:
			if (node->symbol_type == SINT) {
				op = O_SUBI;
			} else {
				op = O_SUBF;
			}
			break;
		case NMUL:
			if (node->symbol_type == SINT) {
				op = O_MULI;
			} else {
				op = O_MULF;
			}
			break;
		case NDIV:
			if (node->symbol_type == SINT) {
				op = O_DIVI;
			} else {
				op = O_DIVF;
			}
			break;
		case NPOW:
			if (node->right_child->symbol_type == SINT) {
				op = O_POWII;
			} else {
				op = O_POWIF;
			}
			break;
		case NMOD:
			op = O_MOD; // mod is for ints only in CD25
			break;
		case NFCALL:
			return tac_gen_fncall(ts, node);
		default: // terminal node
			return tac_get_adr(ts, node);
	}
	if (node->symbol_type == SREAL) {
		if (node->left_child->symbol_type == SINT) {
			promote_left = 1;
		}
		if (node->right_child->symbol_type == SINT) {
			promote_right = 1;
		}
	}
	// assumption: no numeric has 1 child (correct I think)
	Adr lhs = tac_resolve_numeric(ts, node->left_child);
	if (promote_left) {
		Adr tmp = mktmp(ts);
		append_line(ts, binary_line(O_ITOF, tmp, lhs));
		lhs = tmp;
	}
	Adr rhs = tac_resolve_numeric(ts, node->right_child);
	if (promote_right) {
		Adr tmp = mktmp(ts);
		append_line(ts, binary_line(O_ITOF, tmp, rhs));
		rhs = tmp;
	}
	Adr tmp = mktmp(ts);
	append_line(ts, ternary_line(op, tmp, lhs, rhs));
	return tmp;
}

enum operation relop_at(ASTNode *node) {
	switch (node->type) {
		case NGRT:
			return O_GT;
		case NGEQ:
			return O_GTE;
		case NLSS:
			return O_LT;
		case NLEQ:
			return O_LTE;
		case NEQL:
			return O_EQ;
		case NNEQ:
			return O_NEQ;
		default: abort();
	}
}

Adr tac_resolve_boolean(T_S *ts, ASTNode *node) {
	Adr tmp = mktmp(ts);
	Adr not_tmp;
	enum operation op;
	Adr lhs;
	Adr rhs;
	switch (node->type) {
		case NFALS:
			append_line(ts, unary_line(O_FALSE, tmp));
			break;
		case NTRUE:
			append_line(ts, unary_line(O_TRUE, tmp));
			break;
		case NSIMV:
		case NARRV:
			return tac_get_adr(ts, node);
		case NNOT:
			lhs = tac_resolve_numeric(ts, node->left_child);
			rhs = tac_resolve_numeric(ts, node->right_child);
			op = relop_at(node->middle_child);
			append_line(ts, ternary_line(op, tmp, lhs, rhs));
			not_tmp = mktmp(ts);
			append_line(ts, binary_line(O_NOT, not_tmp, tmp));
			return not_tmp;
		case NBOOL:
			lhs = tac_resolve_boolean(ts, node->left_child);
			rhs = tac_resolve_boolean(ts, node->right_child);
			switch (node->middle_child->type) {
				case NAND:
					op = O_AND;
					break;
				case NOR:
					op = O_OR;
					break;
				case NXOR:
					op = O_XOR;
					break;
				default: break;
			}
			append_line(ts, ternary_line(op, tmp, lhs, rhs));
			break;
		case NEQL:
		case NNEQ:
		case NGRT:
		case NLSS:
		case NLEQ:
		case NGEQ:
			lhs = tac_resolve_numeric(ts, node->left_child);
			rhs = tac_resolve_numeric(ts, node->right_child);
			op = relop_at(node);
			append_line(ts, ternary_line(op, tmp, lhs, rhs));
			break;
		case NFCALL:
			return tac_gen_fncall(ts, node);
		default: abort();
	}
	return tmp;
}

Adr tac_resolve_expr(T_S *ts, ASTNode *node) {
	switch (node->symbol_type) {
		case SBOOL:
			return tac_resolve_boolean(ts, node);
			break;
		case SINT:
		case SREAL:
			return tac_resolve_numeric(ts, node);
			break;
		// TODO arrays
		/* case SARRAY: */
		/* 	codegen_array_push(cdg, node); */
		/* 	break; */
		default: abort();
	}
}

Adr tac_get_adr(T_S *ts, ASTNode *node) {
	enum adr_type type;
	u16 adr;
	int ival;
	double fval;
	switch (node->type) {
		case NSIMV:
			// TODO consts
			if (astree_is_param(ts->ast, node->symbol_value)) {
				type = A_PARAM;
			} else {
				type = A_VAR;
			}
			adr = astree_get_offset(ts->ast, node->symbol_value);
			break;
		case NILIT:
			ival = sym_to_int(ts->ast, node->symbol_value);
			return adr_of_int(ts, ival);
		case NFLIT:
			fval = sym_to_double(ts->ast, node->symbol_value);
			return adr_of_double(ts, fval);
		case NADD: case NSUB: case NMUL: case NDIV: case NMOD: case NPOW:
			return tac_resolve_numeric(ts, node);
		case NFALS: case NTRUE: case NBOOL: case NNOT: case NEQL: case NNEQ: case NGRT: case NGEQ: case NLSS: case NLEQ:
			return tac_resolve_boolean(ts, node);
		/* case NARRV: */
		/* 	break; */
		/* case NAELT: */
		/* 	break; */
		default: abort();
	}
	return mkadr(type, adr);
}

void tac_gen_plist(T_S *ts, ASTNode *node) {
	// todo: this could be in parser
	u16 num_vars = 0;
	while (node->type == NPLIST) {
		astree_set_offset(ts->ast, node->left_child->symbol_value, num_vars++);
		astree_mark_param(ts->ast, node->left_child->symbol_value);
		node = node->right_child;
	}
	astree_set_offset(ts->ast, node->symbol_value, num_vars++);
	astree_mark_param(ts->ast, node->symbol_value);
}

void tac_gen_func_locals(T_S *ts, ASTNode *node) {
	if (!node) return;
	int num_vars = 0;
	ASTNode *cursor = node;
	while (cursor->type == NDLIST) {
		astree_set_offset(ts->ast, cursor->left_child->symbol_value, num_vars++);
		cursor = cursor->right_child;
	}
	// somehow cursor->symbol_vaue is null???
	astree_set_offset(ts->ast, cursor->symbol_value, num_vars++);
	Adr vars_num_adr = adr_of_int(ts, num_vars);
	append_line(ts, unary_line(O_ALLOC, vars_num_adr));
	cursor = node;
	while (cursor->type == NDLIST) {
		tac_gen_sdecl(ts, cursor->left_child); // technically this is _decl, but identical code
		cursor = cursor->right_child;
	}
	tac_gen_sdecl(ts, cursor);
}

void tac_gen_func(T_S *ts, ASTNode* nfuncs) {
	Adr fname = adr_of_sds(ts, sds_from_symbol(ts->ast, nfuncs->symbol_value));
	append_line(ts, unary_line(O_FUNC, fname));
	tac_gen_plist(ts, nfuncs->left_child);
	tac_gen_func_locals(ts, nfuncs->middle_child);
	tac_gen_stats(ts, nfuncs->right_child);
	// reset these counters to keep labels and temp reg's local (might remove later)
	ts->temp_reg_counter = 0;
	ts->label_counter = 0;
}

void tac_gen_funcs(T_S *ts, ASTNode *nfuncs) {
	if (!nfuncs) return;
	while (nfuncs && nfuncs->left_child) {
		tac_gen_func(ts, nfuncs->left_child);
		nfuncs = nfuncs->right_child;
	}
	if (nfuncs) {
		tac_gen_func(ts, nfuncs);
	}
}

void tac_gen_nasgn(T_S *ts, ASTNode *node) {
	Adr lhs = tac_get_adr(ts, node->left_child);
	Adr rhs;
	switch (node->right_child->symbol_type) {
		case SINT:
		case SREAL:
			rhs = tac_resolve_numeric(ts, node->right_child);
			append_line(ts, binary_line(O_ASIGN, lhs, rhs));
			break;
		case SBOOL:
			rhs = tac_resolve_boolean(ts, node->right_child);
			append_line(ts, binary_line(O_ASIGN, lhs, rhs));
			break;
			/* TODO arrays
		case SARRAY:
			Attribute *array_atr0 = astree_get_attribute(cdg->ast, node->left_child->symbol_value);
			int array_size = *(int*)hashmap_get(cdg->array_len_map, array_atr0->data);
			// todo: figure out how to do an inline loop, by creating i as a variable on the stack, then referencing it. would be easy if "top of stack" was a base register
			for (int i = 0; i < array_size; ++i) {
				codegen_array_push(cdg, node->left_child);
				push_int_by_val(cdg, i);
				push_instruction(cdg, INDEX, 0);
				codegen_array_push(cdg, node->right_child);
				push_int_by_val(cdg, i);
				push_instruction(cdg, INDEX, 0);
				push_instruction(cdg, L, 0);
				push_instruction(cdg, ST, 0);
			}
			break;
		case SSTRUCT:
			Attribute *array_atr = astree_get_attribute(cdg->ast, node->left_child->left_child->symbol_value);
			int struct_size = *(int*)hashmap_get(cdg->array_structsize_map, array_atr->data);
			int field = 0;
			for (int field = 0; field < struct_size; ++field) {
				codegen_array_push(cdg, node->left_child->left_child);
				codegen_numeric_push(cdg, node->left_child->right_child);
				if (struct_size != 1) {
					push_int_by_val(cdg, struct_size);
					push_instruction(cdg, MUL, 0);
				}
				push_int_by_val(cdg, field);
				push_instruction(cdg, ADD, 0);
				push_instruction(cdg, INDEX, 0);
				codegen_array_push(cdg, node->right_child->left_child);
				codegen_numeric_push(cdg, node->right_child->right_child);
				if (struct_size != 1) {
					push_int_by_val(cdg, struct_size);
					push_instruction(cdg, MUL, 0);
				}
				push_int_by_val(cdg, field);
				push_instruction(cdg, ADD, 0);
				push_instruction(cdg, INDEX, 0);
				push_instruction(cdg, L, 0);
				push_instruction(cdg, ST, 0);
			}
			break;
			*/
		default: abort();
	}
}

void tac_gen_nasgnop(T_S *ts, ASTNode *node) {
	Adr lhs = tac_get_adr(ts, node->left_child);
	enum operation op;
	Adr rhs = tac_resolve_numeric(ts, node->right_child);
	switch (node->type) {
		case NPLEQ:
			if (node->symbol_type == SINT) {
				op = O_ADDI;
			} else {
				op = O_ADDF;
			}
			break;
		case NMNEQ:
			if (node->symbol_type == SINT) {
				op = O_SUBI;
			} else {
				op = O_SUBF;
			}
			break;
		case NSTEA:
			if (node->symbol_type == SINT) {
				op = O_MULI;
			} else {
				op = O_MULF;
			}
			break;
		case NDVEQ:
			if (node->symbol_type == SINT) {
				op = O_DIVI;
			} else {
				op = O_DIVF;
			}
			break;
		default: abort();
	}
	append_line(ts, ternary_line(op, lhs, lhs, rhs));
}

void tac_gen_asgnlist(T_S *ts, ASTNode *node) {
	if (!node)
		return;
	if (node->type == NASGNS) {
		tac_gen_nasgn(ts, node->left_child);
		tac_gen_asgnlist(ts, node->right_child);
	}
	tac_gen_nasgn(ts, node);
}

void tac_gen_if(T_S *ts, ASTNode *node) {
	Adr label = mklabel(ts);
	Adr cond = tac_resolve_boolean(ts, node->left_child);
	append_line(ts, binary_line(O_GOTOF, label, cond));
	tac_gen_stats(ts, node->right_child);
	append_line(ts, unary_line(O_LABEL, label));
}

void tac_gen_ifelse(T_S *ts, ASTNode *node) {
	Adr truelabel = mklabel(ts);
	Adr falselabel = mklabel(ts);
	Adr cond = tac_resolve_boolean(ts, node->left_child);
	append_line(ts, binary_line(O_GOTOF, falselabel, cond));
	tac_gen_stats(ts, node->middle_child);
	append_line(ts, unary_line(O_GOTO, truelabel));
	append_line(ts, unary_line(O_LABEL, falselabel));
	tac_gen_stats(ts, node->right_child);
	append_line(ts, unary_line(O_LABEL, truelabel));
}

void tac_gen_for(T_S *ts, ASTNode *node) {
	tac_gen_asgnlist(ts, node->left_child);
	Adr start = mklabel(ts);
	Adr end = mklabel(ts);
	append_line(ts, unary_line(O_LABEL, start));
	Adr cond = tac_resolve_boolean(ts, node->middle_child);
	append_line(ts, binary_line(O_GOTOF, end, cond));
	tac_gen_stats(ts, node->right_child);
	append_line(ts, unary_line(O_GOTO, start));
	append_line(ts, unary_line(O_LABEL, end));
}

void tac_gen_repeat(T_S *ts, ASTNode *node) {
	tac_gen_asgnlist(ts, node->left_child);
	Adr start = mklabel(ts);
	append_line(ts, unary_line(O_LABEL, start));
	tac_gen_stats(ts, node->middle_child);
	Adr cond = tac_resolve_boolean(ts, node->right_child);
	append_line(ts, binary_line(O_GOTOF, start, cond));
}

void tac_gen_printitem(T_S*ts, ASTNode *node) {
	if (node->type == NSTRG) {
		Adr str_adr = adr_of_sds(ts, sds_from_symbol(ts->ast, node->symbol_value));
		append_line(ts, unary_line(O_PRINTSTR, str_adr));
	} else {
		/* Adr space = adr_of_sds(ts, sdsnew(" ")); */
		/* append_line(ts, unary_line(O_PARAM, space)); */
		/* Adr printf_adr = adr_of_sds(ts, sdsnew("printf")); */
		/* append_line(ts, binary_line(O_CALL, printf_adr, adr_of_int(ts, 1))); */
		append_line(ts, nonary_line(O_PRINTSPC));
		Adr outp = tac_get_adr(ts, node);
		enum operation op;
		if (node->symbol_type == SINT) {
			op = O_PRINTI;
		} else {
			op = O_PRINTF;
		}
		append_line(ts, unary_line(op, outp));
	}
}

void tac_gen_prlist(T_S *ts, ASTNode *node) {
	if (node->type == NPRLST) {
		tac_gen_printitem(ts, node->left_child);
		tac_gen_prlist(ts, node->right_child);
	} else {
		tac_gen_printitem(ts, node);
	}
}

void tac_gen_noutp(T_S *ts, ASTNode *node) {
	tac_gen_prlist(ts, node->left_child);
}

void tac_gen_noutl(T_S *ts, ASTNode *node) {
	if (!node->left_child) {
		append_line(ts, nonary_line(O_PRINTLN));
		return;
	}
	tac_gen_prlist(ts, node->left_child);
	append_line(ts, nonary_line(O_PRINTLN));
}

void tac_gen_input_var(T_S *ts, ASTNode *node) {
	Adr left = tac_get_adr(ts, node);
	if (node->symbol_type == SREAL) {
		append_line(ts, unary_line(O_READF, left));
	} else {
		append_line(ts, unary_line(O_READI, left));
	}
}

void tac_gen_nvlist(T_S *ts, ASTNode *node) {
	if (node->type == NVLIST) {
		tac_gen_input_var(ts, node->left_child);
		tac_gen_nvlist(ts, node->right_child);
	} else {
		tac_gen_input_var(ts, node);
	}
}

void tac_gen_ninput(T_S *ts, ASTNode *node) {
	tac_gen_nvlist(ts, node->left_child);
}

void tac_gen_parameters(T_S *ts, ASTNode *node, u16 *paramcount) {
	if (!node) return;
	if (node->type == NEXPL) {
		Adr par = tac_resolve_expr(ts, node->left_child);
		append_line(ts, unary_line(O_PARAM, par));
		(*paramcount)++;
		tac_gen_parameters(ts, node->right_child, paramcount);
	} else {
		Adr par = tac_resolve_expr(ts, node);
		append_line(ts, unary_line(O_PARAM, par));
		(*paramcount)++;
	}
}

void tac_gen_callstat(T_S *ts, ASTNode *node) {
	u16 paramcount = 0;
	tac_gen_parameters(ts, node->left_child, &paramcount);
	Adr pcount = adr_of_int(ts, paramcount);
	Adr fname = adr_of_sds(ts, sds_from_symbol(ts->ast, node->symbol_value));
	append_line(ts, binary_line(O_CALL, fname, pcount));
}

Adr tac_gen_fncall(T_S *ts, ASTNode *node) {
	Adr tmp = mktmp(ts);
	u16 paramcount = 0;
	tac_gen_parameters(ts, node->left_child, &paramcount);
	Adr pcount = adr_of_int(ts, paramcount);
	Adr fname = adr_of_sds(ts, sds_from_symbol(ts->ast, node->symbol_value));
	append_line(ts, ternary_line(O_CALLVAL, tmp, fname, pcount));
	return tmp;
}

void tac_gen_returnstat(T_S *ts, ASTNode *node) {
	if (node->left_child) {
		Adr radr = tac_resolve_expr(ts, node->left_child);
		append_line(ts, unary_line(O_RVAL, radr));
	} else {
		append_line(ts, nonary_line(O_RETN));
	}
}

void tac_gen_stat(T_S *ts, ASTNode *node) {
	switch (node->type) {
		case NOUTP:
			tac_gen_noutp(ts, node);
			break;
		case NOUTL:
			tac_gen_noutl(ts, node);
			break;
		case NINPUT:
			tac_gen_ninput(ts, node);
			break;
		case NIFTH:
			tac_gen_if(ts, node);
			break;
		case NIFTE:
			tac_gen_ifelse(ts, node);
			break;
		case NREPT:
			tac_gen_repeat(ts, node);
			break;
		case NFORL:
			tac_gen_for(ts, node);
			break;
		case NASGN:
			tac_gen_nasgn(ts, node);
			break;
		case NPLEQ:
		case NMNEQ:
		case NSTEA:
		case NDVEQ:
			tac_gen_nasgnop(ts, node);
			break;
		case NCALL:
			tac_gen_callstat(ts, node);
			break;
		case NRETN:
			tac_gen_returnstat(ts, node);
			break;
		default: abort();
	}
}

void tac_gen_stats(T_S *ts, ASTNode *node) {
	if (node->type == NSTATS) {
		tac_gen_stat(ts, node->left_child);
		if (node->right_child) {
			tac_gen_stats(ts, node->right_child);
		}
	} else {
		tac_gen_stat(ts, node);
	}
}

// set local variables to 0
void tac_gen_sdecl(T_S *ts, ASTNode *node) {
	int offset;
	switch (node->symbol_type) {
		case SREAL:
			offset = astree_get_offset(ts->ast, node->symbol_value);
			append_line(ts, binary_line(O_ASIGN, mkadr(A_VAR, offset), adr_of_double(ts, 0.0)));
			break;
		case SINT:
			offset = astree_get_offset(ts->ast, node->symbol_value);
			append_line(ts, binary_line(O_ASIGN, mkadr(A_VAR, offset), adr_of_int(ts, 0)));
			break;
		case SBOOL: // assumption: uninitialised booleans are false
			offset = astree_get_offset(ts->ast, node->symbol_value);
			append_line(ts, unary_line(O_FALSE, mkadr(A_VAR, offset)));
			break;
		default: abort();
	}
}

void tac_gen_slist(T_S *ts, ASTNode *node) {
	int num_vars = 0;
	ASTNode *cursor = node;
	while (cursor->type == NSDLST) {
		astree_set_offset(ts->ast, cursor->left_child->symbol_value, num_vars++);
		cursor = cursor->right_child;
	}
	astree_set_offset(ts->ast, cursor->symbol_value, num_vars++);
	Adr vars_num_adr = adr_of_int(ts, num_vars);
	append_line(ts, unary_line(O_ALLOC, vars_num_adr));
	cursor = node;
	while (cursor->type == NSDLST) {
		tac_gen_sdecl(ts, cursor->left_child);
		cursor = cursor->right_child;
	}
	tac_gen_sdecl(ts, cursor);
}

void tac_gen_main(T_S *ts, ASTNode *nmain) {
	tac_gen_slist(ts, nmain->left_child);
	tac_gen_stats(ts, nmain->right_child);
}

void sdsfree_wrapper_unary(void *ptr) {
	sdsfree((sds)ptr);
}

void t_s_free(T_S *ts) {
	hashmap_free(ts->seen_intvals, free, free);
	hashmap_free(ts->seen_floatvals, free, free);
	hashmap_free(ts->seen_strings, sdsfree_wrapper_unary, free);
}

TAC *tac_from_ast(ASTree *ast) {
	TAC *tac = tac_create();
	T_S *state = t_s_create(tac, ast);
	tac_gen_funcs(state, ast->root->middle_child);
	Adr main = adr_of_sds(state, sdsnew("main"));
	linkedlist_push_tail(tac->lines, unary_line(O_FUNC, main));
	tac_gen_main(state, ast->root->right_child);
	t_s_free(state);
	return tac;
}

void sdsfree_wrapper(void *ptr, va_list args) {
	sdsfree((sds)ptr);
}

void tac_free(TAC* tac) {
	linkedlist_free(tac->lines);
	linkedlist_free(tac->ints);
	linkedlist_free(tac->floats);
	linkedlist_free_ctx(tac->strings, sdsfree_wrapper);
}

void print_adr(Adr adr) {
	switch (adr.type) {
		case A_TMP:
			printf("T");
			break;
		case A_LABEL:
			printf("L");
			break;
		case A_VAR:
			printf("V");
			break;
		case A_PARAM:
			printf("P");
			break;
		case A_ILIT:
			printf("I");
			break;
		case A_FLIT:
			printf("F");
			break;
		case A_STR:
			printf("S");
			break;
		case A_CONST:
			printf("C");
			break;
		case A_EMPTY: return;
	}
	printf("%d", adr.adr);
}

void print_tac_line(Line *l) {
	switch (l->op) {
		// outliers
		case O_ASIGN:
			print_adr(l->left);
			printf(" = ");
			print_adr(l->right);
			break;
		case O_FUNC:
			printf("_");
			print_adr(l->left);
			printf(":");
			break;
		case O_CALLVAL:
			print_adr(l->left);
			printf(" = ");
			printf("%s ", print_op[l->op]);
			print_adr(l->middle);
			printf(" ");
			print_adr(l->right);
			break;
		// nonary
		case O_PRINTLN: case O_PRINTSPC:
		case O_RETN:
			printf("%s", print_op[l->op]);
			break;
		// unary void
		case O_PRINTI: case O_PRINTF: case O_PRINTSTR:
		case O_LABEL: case O_GOTO: case O_ALLOC:
		case O_PARAM: case O_RVAL:
			printf("%s ", print_op[l->op]);
			print_adr(l->left);
			break;
		// unary asign
		case O_READI: case O_READF: case O_TRUE: case O_FALSE:
			print_adr(l->left);
			printf(" = %s", print_op[l->op]);
			break;
		// binary void
		case O_GOTOF: case O_GOTOT:
		case O_CALL:
			printf("%s ", print_op[l->op]);
			print_adr(l->left);
			printf(" ");
			print_adr(l->right);
			break;
			break;
		// binary asign
		case O_ITOF:
		case O_NOT:
			print_adr(l->left);
			printf(" = ");
			printf("%s ", print_op[l->op]);
			print_adr(l->right);
			break;
		// ternary
		case O_ADDF: case O_ADDI: case O_SUBF: case O_SUBI: case O_MULF: case O_MULI: case O_DIVF: case O_DIVI: case O_POWIF: case O_POWII: case O_MOD:
		case O_EQ: case O_NEQ: case O_LT: case O_LTE: case O_GT: case O_GTE:
		case O_OR: case O_AND: case O_XOR:
			print_adr(l->left);
			printf(" = ");
			print_adr(l->middle);
			printf(" %s ", print_op[l->op]);
			print_adr(l->right);
			break;
	}
	printf("\n");
}

void tac_printf(TAC* tac) {
	// todo: consts
	printf(".ints:\n");
	linkedlist_start(tac->ints);
	int linenum = 1;
	int i = 0;
	int *k;
	while ((k=(int*)linkedlist_get_current(tac->ints))) {
		printf("I%d: %d\n", i, *k);
		linkedlist_forward(tac->ints);
		++i;
	}
	printf(".floats:\n");
	linkedlist_start(tac->floats);
	i = 0;
	double *x;
	while ((x=(double*)linkedlist_get_current(tac->floats))) {
		printf("F%d: %f\n", i, *x);
		linkedlist_forward(tac->floats);
		++i;
	}
	printf(".strings:\n");
	linkedlist_start(tac->strings);
	i = 0;
	char *s;
	while ((s=(char*)linkedlist_get_current(tac->strings))) {
		printf("S%d: \"%s\"\n", i, s);
		linkedlist_forward(tac->strings);
		++i;
	}
	printf(".code:\n");
	linkedlist_start(tac->lines);
	Line *l;
	while ((l=(Line*)linkedlist_get_current(tac->lines))) {
		if (l->op != O_FUNC && l->op != O_LABEL) {
			printf("%d: ", linenum++);
		}
		print_tac_line(l);
		linkedlist_forward(tac->lines);
	}
}
