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
	// global
	u16 label_counter;
	u16 string_counter;
	u16 float_counter;
	u16 int_counter;
	u16 array_counter;
	HashMap *array_len_map; // <symbol, int>
	HashMap *array_structsize_map; // <symbol, int>
	HashMap *const_map; // <symbol, int>
} T_S;

// predefinitions
void tac_gen_stats(T_S *ts, ASTNode *node);
void tac_gen_sdecl(T_S *ts, ASTNode *node);
Adr tac_get_adr(T_S *ts, ASTNode *node);
Adr tac_gen_fncall(T_S *ts, ASTNode *node);
long *heap_long(long k);
int *heap_int(int k);
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

Adr blank(void) {
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

Line *ternary_line(enum operation op, Adr left, Adr middle, Adr right, u16 linenum) {
	Line *new = malloc(sizeof(Line));
	*new = (Line) { op, left, middle, right, linenum};
	return new;
}

Line *binary_line(enum operation op, Adr left, Adr right, u16 linenum) {
	Line *new = malloc(sizeof(Line));
	*new = (Line) {op, left, blank(), right, linenum};
	return new;
}

Line *unary_line(enum operation op, Adr left, u16 linenum) {
	Line *new = malloc(sizeof(Line));
	*new = (Line) {op, left, blank(), blank(), linenum};
	return new;
}

Line *nonary_line(enum operation op, u16 linenum) {
	Line *new = malloc(sizeof(Line));
	*new = (Line) {op, blank(), blank(), blank(), linenum};
	return new;
}

long *heap_long(long k) {
	long *hk = malloc(sizeof(long));
	*hk = k;
	return hk;
}

int *heap_int(int k) {
	int *hk = malloc(sizeof(int));
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

TAC *tac_create(void) {
	TAC *tac = malloc(sizeof(TAC));
	tac->lines = linkedlist_create();
	tac->strings = linkedlist_create();
	tac->floats = linkedlist_create();
	tac->ints = linkedlist_create();
	tac->arrays = linkedlist_create();
	return tac;
}

T_S *t_s_create(TAC *tac, ASTree *ast) {
	T_S *new = malloc(sizeof(T_S));
	*new = (T_S) {0};
	new->tac = tac;
	new->ast = ast;
	new->seen_strings = hashmap_create(50, hash_str, equal_str);
	new->seen_intvals = hashmap_create(50, hash_i64, equal_i64);
	new->seen_floatvals = hashmap_create(50, hash_double, equal_double);
	new->array_len_map = hashmap_create(20, hash_symbol, equal_symbol);
	new->array_structsize_map = hashmap_create(20, hash_symbol, equal_symbol);
	new->const_map = hashmap_create(20, hash_symbol, equal_symbol);
	return new;
}

Adr tac_resolve_numeric(T_S *ts, ASTNode *node) {
	enum operation op;
	int promote_left = 0;
	int promote_right = 0;
	Adr tmp;
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
		case NARRV:
			tmp = mktmp(ts);
			append_line(ts, binary_line(O_DEREF, tmp, tac_get_adr(ts, node), node->row));
			return tmp;
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
		append_line(ts, binary_line(O_ITOF, tmp, lhs, node->row));
		lhs = tmp;
	}
	Adr rhs = tac_resolve_numeric(ts, node->right_child);
	if (promote_right) {
		Adr tmp = mktmp(ts);
		append_line(ts, binary_line(O_ITOF, tmp, rhs, node->row));
		rhs = tmp;
	}
	tmp = mktmp(ts);
	append_line(ts, ternary_line(op, tmp, lhs, rhs, node->row));
	return tmp;
}

enum operation relop_at(ASTNode *node) {
	switch (node->type) {
		case NGRT:
			if (node->left_child->symbol_type == SREAL || node->right_child->symbol_type == SREAL) {
				return O_GTF;
			} else {
				return O_GTI;
			}
		case NGEQ:
			if (node->left_child->symbol_type == SREAL || node->right_child->symbol_type == SREAL) {
				return O_GTEF;
			} else {
				return O_GTEI;
			}
		case NLSS:
			if (node->left_child->symbol_type == SREAL || node->right_child->symbol_type == SREAL) {
				return O_LTF;
			} else {
				return O_LTI;
			}
		case NLEQ:
			if (node->left_child->symbol_type == SREAL || node->right_child->symbol_type == SREAL) {
				return O_LTEF;
			} else {
				return O_LTEI;
			}
		case NEQL:
			if (node->left_child->symbol_type == SREAL || node->right_child->symbol_type == SREAL) {
				return O_EQF;
			} else {
				return O_EQI;
			}
		case NNEQ:
			if (node->left_child->symbol_type == SREAL || node->right_child->symbol_type == SREAL) {
				return O_NEQF;
			} else {
				return O_NEQI;
			}
		default: abort();
	}
}

Adr tac_resolve_boolean(T_S *ts, ASTNode *node) {
	Adr tmp = mktmp(ts);
	Adr not_tmp;
	enum operation op;
	Adr lhs;
	Adr rhs;
	int promotion = 0;
	Adr temp;
	switch (node->type) {
		case NFALS:
			append_line(ts, unary_line(O_FALSE, tmp, node->row));
			break;
		case NTRUE:
			append_line(ts, unary_line(O_TRUE, tmp, node->row));
			break;
		case NSIMV:
		case NARRV:
			return tac_get_adr(ts, node);
		case NNOT:
			if (node->left_child->symbol_type == SREAL || node->right_child->symbol_type == SREAL) {
				promotion = 1;
			}
			lhs = tac_resolve_numeric(ts, node->left_child);
			if (promotion && node->left_child->symbol_type == SINT) {
				temp = mktmp(ts);
				append_line(ts, binary_line(O_ITOF, temp, lhs, node->row));
				lhs = temp;
			}
			rhs = tac_resolve_numeric(ts, node->right_child);
			if (promotion && node->right_child->symbol_type == SINT) {
				temp = mktmp(ts);
				append_line(ts, binary_line(O_ITOF, temp, rhs, node->row));
				rhs = temp;
			}
			op = relop_at(node->middle_child);
			append_line(ts, ternary_line(op, tmp, lhs, rhs, node->row));
			not_tmp = mktmp(ts);
			append_line(ts, binary_line(O_NOT, not_tmp, tmp, node->row));
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
				default: abort();
			}
			append_line(ts, ternary_line(op, tmp, lhs, rhs, node->row));
			break;
		case NEQL:
		case NNEQ:
		case NGRT:
		case NLSS:
		case NLEQ:
		case NGEQ:
			if (node->left_child->symbol_type == SREAL || node->right_child->symbol_type == SREAL) {
				promotion = 1;
			}
			lhs = tac_resolve_numeric(ts, node->left_child);
			if (promotion && node->left_child->symbol_type == SINT) {
				temp = mktmp(ts);
				append_line(ts, binary_line(O_ITOF, temp, lhs, node->row));
				lhs = temp;
			}
			rhs = tac_resolve_numeric(ts, node->right_child);
			if (promotion && node->right_child->symbol_type == SINT) {
				temp = mktmp(ts);
				append_line(ts, binary_line(O_ITOF, temp, rhs, node->row));
				rhs = temp;
			}
			op = relop_at(node);
			append_line(ts, ternary_line(op, tmp, lhs, rhs, node->row));
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
		case SARRAY:
			return tac_get_adr(ts, node);
			break;
		default: abort();
	}
}

static int unscoped_symbol_equals(const Symbol *s1, const Symbol *s2) {
	return s1->index.start == s2->index.start &&
	       s1->index.len   == s2->index.len;
}

// TODO: fix name. this gets values, except for arrays which return pointers
Adr tac_get_adr(T_S *ts, ASTNode *node) {
	enum adr_type type;
	u16 adr;
	int ival;
	double fval;
	Adr tmp0;
	Adr tmp1;
	Adr tmp2;
	Adr array_start;
	u16 offset;
	Attribute *array_atr;
	int struct_size;
	Symbol globscoped;
	switch (node->type) {
		case NSIMV:
			globscoped = *node->symbol_value;
			globscoped.scope = 0;
			if (hashmap_get(ts->const_map, &globscoped)) {
				int val = *(int*)hashmap_get(ts->const_map, &globscoped);
				switch (node->symbol_type) {
					case SINT:
						return mkadr(A_ILIT, val);
					case SREAL:
						return mkadr(A_FLIT, val);
					default: abort();
				}
			}
			if (astree_is_param(ts->ast, node->symbol_value)) {
				type = A_PARAM;
			} else if (node->symbol_type == SARRAY) {
				type = A_ARRAY;
			} else {
				type = A_VAR;
			}
			adr = astree_get_offset(ts->ast, node->symbol_value);
			return mkadr(type, adr);
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
		case NARRV:
			array_atr = astree_get_attribute(ts->ast, node->left_child->symbol_value);
			offset = astree_get_offset(ts->ast, node->left_child->symbol_value);
			if (array_atr->is_param) {
				type = A_PARAM;
			} else {
				type = A_ARRAY;
			}
			array_start = mkadr(type, offset);
			Adr index = tac_resolve_numeric(ts, node->right_child);
			// todo: why is the scope of that not 0? it's written as 0
			((Symbol*)array_atr->data)->scope = 0;
			struct_size = *(int*)hashmap_get(ts->array_structsize_map, array_atr->data);
			tmp0 = mktmp(ts);
			append_line(ts, ternary_line(O_MULI, tmp0, index, adr_of_int(ts, struct_size), node->row));
			tmp1 = mktmp(ts);
			append_line(ts, ternary_line(O_ADDI, tmp1, array_start, tmp0, node->row));
			int struct_offset = 0;
			Attribute *struct_atr = astree_get_attribute(ts->ast, array_atr->data);
			Attribute *struct_fields = astree_get_attribute(ts->ast, struct_atr->data);
			LinkedList *elements = (LinkedList *)struct_fields->data;
			linkedlist_start(elements);
			Symbol *target_element = node->symbol_value;
			while (!unscoped_symbol_equals(
				((Element*)linkedlist_get_current(elements))->name,
				target_element
			) ) {
				linkedlist_forward(elements);
				struct_offset += 8;
			}
			Adr withinstruct = adr_of_int(ts, struct_offset);
			/* tmp4 = mktmp(ts); */
			/* append_line(ts, ternary_line(O_MULI, tmp4, adr_of_int(ts, 8), withinstruct)); */
			tmp2 = mktmp(ts);
			append_line(ts, ternary_line(O_ADDI, tmp2, tmp1, withinstruct, node->row));
			return tmp2;
			/* tmp3 = mktmp(ts); */
			/* binary_line(O_DEREF, tmp3, tmp2); */
			/* return tmp3; */
			break;
		case NAELT:
			array_atr = astree_get_attribute(ts->ast, node->left_child->symbol_value);
			offset = astree_get_offset(ts->ast, node->left_child->symbol_value);
			if (array_atr->is_param) {
				type = A_PARAM;
			} else {
				type = A_ARRAY;
			}
			array_start = mkadr(type, offset);
			// TODO: study how the next line breaks the whole program (tmp0 unitialised)
			/* append_line(ts, binary_line(O_ASIGN, tmp0, array_start, node->row)); */
			Adr diff = tac_resolve_numeric(ts, node->right_child);
			Adr muldiff = mktmp(ts);
			struct_size = *(int*)hashmap_get(ts->array_structsize_map, array_atr->data);
			append_line(ts, ternary_line(O_MULI, muldiff, diff, adr_of_int(ts, struct_size), node->row));
			tmp1 = mktmp(ts);
			append_line(ts, ternary_line(O_ADDI, tmp1, array_start, muldiff, node->row));
			return tmp1;
		case NFCALL:
			return tac_gen_fncall(ts, node);
		default: abort();
	}
}

void tac_gen_plist(T_S *ts, ASTNode *node) {
	// TODO: this could be in parser
	u16 num_vars = 0;
	while (node->type == NPLIST) {
		if (node->left_child->type == NARRC) {
			astree_set_offset(ts->ast, node->left_child->left_child->symbol_value, num_vars++);
			astree_mark_param(ts->ast, node->left_child->left_child->symbol_value);
		} else {
			astree_set_offset(ts->ast, node->left_child->symbol_value, num_vars++);
			astree_mark_param(ts->ast, node->left_child->symbol_value);
		}
		node = node->right_child;
	}
	if (node->left_child && node->left_child->type == NARRC) {
		astree_set_offset(ts->ast, node->left_child->left_child->symbol_value, num_vars++);
		astree_mark_param(ts->ast, node->left_child->left_child->symbol_value);
	} else {
		astree_set_offset(ts->ast, node->symbol_value, num_vars++);
		astree_mark_param(ts->ast, node->symbol_value);
	}
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
	/* Adr vars_num_adr = adr_of_int(ts, num_vars); */
	/* append_line(ts, unary_line(O_ALLOC, vars_num_adr)); */
	cursor = node;
	while (cursor->type == NDLIST) {
		tac_gen_sdecl(ts, cursor->left_child); // technically this is _decl, but identical code
		cursor = cursor->right_child;
	}
	tac_gen_sdecl(ts, cursor);
}

void tac_gen_func(T_S *ts, ASTNode* nfuncs) {
	Adr fname = adr_of_sds(ts, sds_from_symbol(ts->ast, nfuncs->symbol_value));
	append_line(ts, unary_line(O_FUNC, fname, 0));
	tac_gen_plist(ts, nfuncs->left_child);
	tac_gen_func_locals(ts, nfuncs->middle_child);
	tac_gen_stats(ts, nfuncs->right_child);
	// reset these counters to keep temp reg's local
	ts->temp_reg_counter = 0;
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
	Attribute *array_atr;
	switch (node->right_child->symbol_type) {
		case SINT:
		case SREAL:
			rhs = tac_resolve_numeric(ts, node->right_child);
			if (node->left_child->type == NARRV) {
				append_line(ts, binary_line(O_STORE, lhs, rhs, node->row));
			} else {
				append_line(ts, binary_line(O_ASIGN, lhs, rhs, node->row));
			}
			break;
		case SBOOL:
			rhs = tac_resolve_boolean(ts, node->right_child);
			if (node->left_child->type == NARRV) {
				append_line(ts, binary_line(O_STORE, lhs, rhs, node->row));
			} else {
				append_line(ts, binary_line(O_ASIGN, lhs, rhs, node->row));
			}
			break;
		case SARRAY:
			array_atr = astree_get_attribute(ts->ast, node->left_child->symbol_value);
			int array_size = *(int*)hashmap_get(ts->array_len_map, array_atr->data);
			// todo: inline loop with labels?
			rhs = tac_get_adr(ts, node->right_child);
			for (int i = 0; i < array_size; ++i) {
				Adr tmp0 = mktmp(ts);
				append_line(ts, ternary_line(O_ADDI, tmp0, lhs, adr_of_int(ts, i*8), node->row));
				Adr tmp1 = mktmp(ts);
				append_line(ts, ternary_line(O_ADDI, tmp1, rhs, adr_of_int(ts, i*8), node->row));
				Adr tmp2 = mktmp(ts);
				append_line(ts, binary_line(O_DEREF, tmp2, tmp1, node->row));
				append_line(ts, binary_line(O_STORE, tmp0, tmp2, node->row));
			}
			break;
		case SSTRUCT:
			array_atr = astree_get_attribute(ts->ast, node->left_child->left_child->symbol_value);
			int struct_size = *(int*)hashmap_get(ts->array_structsize_map, array_atr->data);
			// todo: inline loop with labels?
			rhs = tac_get_adr(ts, node->right_child);
			for (int i = 0; i < struct_size; i += 8) {
				Adr tmp0 = mktmp(ts);
				append_line(ts, ternary_line(O_ADDI, tmp0, lhs, adr_of_int(ts, i), node->row));
				Adr tmp1 = mktmp(ts);
				append_line(ts, ternary_line(O_ADDI, tmp1, rhs, adr_of_int(ts, i), node->row));
				Adr tmp2 = mktmp(ts);
				append_line(ts, binary_line(O_DEREF, tmp2, tmp1, node->row));
				append_line(ts, binary_line(O_STORE, tmp0, tmp2, node->row));
			}
			break;
		default: abort();
	}
}

void tac_gen_nasgnop(T_S *ts, ASTNode *node) {
	Adr lhs = tac_get_adr(ts, node->left_child);
	enum operation op;
	Adr rhs = tac_resolve_numeric(ts, node->right_child);
	int promote_right = 0;
	switch (node->type) {
		case NPLEQ:
			if (node->left_child->symbol_type == SINT) {
				op = O_ADDI;
			} else {
				op = O_ADDF;
			}
			break;
		case NMNEQ:
			if (node->left_child->symbol_type == SINT) {
				op = O_SUBI;
			} else {
				op = O_SUBF;
			}
			break;
		case NSTEA:
			if (node->left_child->symbol_type == SINT) {
				op = O_MULI;
			} else {
				op = O_MULF;
			}
			break;
		case NDVEQ:
			if (node->left_child->symbol_type == SINT) {
				op = O_DIVI;
			} else {
				op = O_DIVF;
			}
			break;
		default: abort();
	}
	if (node->left_child->symbol_type == SREAL) {
		if (node->right_child->symbol_type == SINT) {
			promote_right = 1;
		}
	}
	if (promote_right) {
		Adr tmp = mktmp(ts);
		append_line(ts, binary_line(O_ITOF, tmp, rhs, node->row));
		rhs = tmp;
	}
	if (node->left_child->type == NAELT) {
		Adr t1 = mktmp(ts);
		append_line(ts, binary_line(O_DEREF, t1, lhs, node->row));
		Adr t2 = mktmp(ts);
		append_line(ts, ternary_line(op, t2, t1, rhs, node->row));
		append_line(ts, binary_line(O_STORE, lhs, t2, node->row));
	} else {
		append_line(ts, ternary_line(op, lhs, lhs, rhs, node->row));
	}
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
	append_line(ts, binary_line(O_GOTOF, label, cond, 0));
	tac_gen_stats(ts, node->right_child);
	append_line(ts, unary_line(O_LABEL, label, 0));
}

void tac_gen_ifelse(T_S *ts, ASTNode *node) {
	Adr truelabel = mklabel(ts);
	Adr falselabel = mklabel(ts);
	Adr cond = tac_resolve_boolean(ts, node->left_child);
	append_line(ts, binary_line(O_GOTOF, falselabel, cond, 0));
	tac_gen_stats(ts, node->middle_child);
	append_line(ts, unary_line(O_GOTO, truelabel, 0));
	append_line(ts, unary_line(O_LABEL, falselabel, 0));
	tac_gen_stats(ts, node->right_child);
	append_line(ts, unary_line(O_LABEL, truelabel, 0));
}

void tac_gen_for(T_S *ts, ASTNode *node) {
	tac_gen_asgnlist(ts, node->left_child);
	Adr start = mklabel(ts);
	Adr end = mklabel(ts);
	append_line(ts, unary_line(O_LABEL, start, 0));
	Adr cond = tac_resolve_boolean(ts, node->middle_child);
	append_line(ts, binary_line(O_GOTOF, end, cond, 0));
	tac_gen_stats(ts, node->right_child);
	append_line(ts, unary_line(O_GOTO, start, 0));
	append_line(ts, unary_line(O_LABEL, end, 0));
}

void tac_gen_repeat(T_S *ts, ASTNode *node) {
	tac_gen_asgnlist(ts, node->left_child);
	Adr start = mklabel(ts);
	append_line(ts, unary_line(O_LABEL, start, 0));
	tac_gen_stats(ts, node->middle_child);
	Adr cond = tac_resolve_boolean(ts, node->right_child);
	append_line(ts, binary_line(O_GOTOF, start, cond, 0));
}

void tac_gen_printitem(T_S*ts, ASTNode *node) {
	if (node->type == NSTRG) {
		Adr str_adr = adr_of_sds(ts, sds_from_symbol(ts->ast, node->symbol_value));
		append_line(ts, unary_line(O_PRINTSTR, str_adr, node->row));
	} else {
		/* Adr space = adr_of_sds(ts, sdsnew(" ")); */
		/* append_line(ts, unary_line(O_PARAM, space)); */
		/* Adr printf_adr = adr_of_sds(ts, sdsnew("printf")); */
		/* append_line(ts, binary_line(O_CALL, printf_adr, adr_of_int(ts, 1))); */
		append_line(ts, nonary_line(O_PRINTSPC, node->row));
		Adr outp = tac_get_adr(ts, node);
		enum operation op;
		if (node->symbol_type == SINT) {
			op = O_PRINTI;
		} else {
			op = O_PRINTF;
		}
		if (node->type == NARRV) {
			Adr tmp = mktmp(ts);
			append_line(ts, binary_line(O_DEREF, tmp, outp, node->row));
			outp = tmp;
		}
		append_line(ts, unary_line(op, outp, node->row));
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
		append_line(ts, nonary_line(O_PRINTLN, node->row));
		return;
	}
	tac_gen_prlist(ts, node->left_child);
	append_line(ts, nonary_line(O_PRINTLN, node->row));
}

void tac_gen_input_var(T_S *ts, ASTNode *node) {
	Adr left = tac_get_adr(ts, node);
	if (node->type == NARRV) {
		Adr input = mktmp(ts);
		if (node->symbol_type == SREAL) {
			append_line(ts, unary_line(O_READF, input, node->row));
		} else {
			append_line(ts, unary_line(O_READI, input, node->row));
		}
		append_line(ts, binary_line(O_STORE, left, input, node->row));
	} else {
		if (node->symbol_type == SREAL) {
			append_line(ts, unary_line(O_READF, left, node->row));
		} else {
			append_line(ts, unary_line(O_READI, left, node->row));
		}
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
		(*paramcount)++;
		tac_gen_parameters(ts, node->right_child, paramcount);
		append_line(ts, unary_line(O_PARAM, par, node->row));
	} else {
		Adr par = tac_resolve_expr(ts, node);
		append_line(ts, unary_line(O_PARAM, par, node->row));
		(*paramcount)++;
	}
}

void tac_gen_callstat(T_S *ts, ASTNode *node) {
	u16 paramcount = 0;
	tac_gen_parameters(ts, node->left_child, &paramcount);
	Adr pcount = adr_of_int(ts, paramcount);
	Adr fname = adr_of_sds(ts, sds_from_symbol(ts->ast, node->symbol_value));
	append_line(ts, binary_line(O_CALL, fname, pcount, node->row));
}

Adr tac_gen_fncall(T_S *ts, ASTNode *node) {
	Adr tmp = mktmp(ts);
	u16 paramcount = 0;
	tac_gen_parameters(ts, node->left_child, &paramcount);
	Adr pcount = adr_of_int(ts, paramcount);
	Adr fname = adr_of_sds(ts, sds_from_symbol(ts->ast, node->symbol_value));
	append_line(ts, ternary_line(O_CALLVAL, tmp, fname, pcount, node->row));
	return tmp;
}

void tac_gen_returnstat(T_S *ts, ASTNode *node) {
	if (node->left_child) {
		Adr radr = tac_resolve_expr(ts, node->left_child);
		append_line(ts, unary_line(O_RVAL, radr, node->row));
	} else {
		append_line(ts, nonary_line(O_RETN, node->row));
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
			append_line(ts, binary_line(O_ASIGN, mkadr(A_VAR, offset), adr_of_double(ts, 0.0), 0));
			break;
		case SINT:
			offset = astree_get_offset(ts->ast, node->symbol_value);
			append_line(ts, binary_line(O_ASIGN, mkadr(A_VAR, offset), adr_of_int(ts, 0), 0));
			break;
		case SBOOL: // assumption: uninitialised booleans are false
			offset = astree_get_offset(ts->ast, node->symbol_value);
			append_line(ts, unary_line(O_FALSE, mkadr(A_VAR, offset), 0));
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
	/* Adr vars_num_adr = adr_of_int(ts, num_vars); */
	/* append_line(ts, unary_line(O_ALLOC, vars_num_adr)); */
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

void tac_gen_init(T_S *ts, ASTNode* node) {
	if (node->left_child->type != NILIT && node->left_child->type != NFLIT) {
		// todo: put constexpr here (nontrivial because int is not symbol)
		printf("sorry, only integer/float literals are implemented as constants\n");
		return;
	}
	if (node->left_child->type == NILIT) {
		sds s = sds_from_symbol(ts->ast, node->left_child->symbol_value);
		long val = atol(s);
		linkedlist_push_tail(ts->tac->ints, heap_long(val));
		/* astree_set_offset(ts->ast, node->symbol_value, ts->int_counter++); */
		hashmap_add(ts->const_map, node->symbol_value, heap_int(ts->int_counter++));
	}
	if (node->left_child->type == NFLIT) {
		sds s = sds_from_symbol(ts->ast, node->left_child->symbol_value);
		double val = atof(s);
		linkedlist_push_tail(ts->tac->ints, heap_double(val));
		/* astree_set_offset(ts->ast, node->symbol_value, ts->float_counter++); */
		hashmap_add(ts->const_map, node->symbol_value, heap_int(ts->float_counter++));
	}
}

void tac_gen_consts(T_S *ts, ASTNode* node) {
	if (!node) return;
	while (node->type == NILIST) {
		tac_gen_init(ts, node->left_child);
		node = node->right_child;
	}
	tac_gen_init(ts, node);
}

// TODO: constfloat
int tac_gen_constint(T_S *ts, ASTNode* node) {
	sds glyph;
	int result;
	int offset;
	switch (node->type) {
		case NILIT:
			glyph = sds_from_symbol(ts->ast, node->symbol_value);
			result = atoi(glyph);
			sdsfree(glyph);
			return result;
		case NSIMV:
			offset = *(int*)hashmap_get(ts->const_map, node->symbol_value);
			linkedlist_start(ts->tac->ints);
			for (int i = 0; i < offset; ++i) {
				linkedlist_forward(ts->tac->ints);
			}
			return *(int*)linkedlist_get_current(ts->tac->ints);
		default: abort();
	}
	int lhs = tac_gen_constint(ts, node->left_child);
	int rhs = tac_gen_constint(ts, node->right_child);
	switch (node->type) {
		case NADD:
			return lhs + rhs;
		case NMUL:
			return lhs * rhs;
		case NSUB:
			return lhs - rhs;
		case NDIV:
			return lhs / rhs;
		case NMOD:
			return lhs % rhs;
		default: abort();
	}
}

void tac_gen_arrtype(T_S *ts, ASTNode *node) {
	Attribute *struct_atr = astree_get_attribute(ts->ast, node->symbol_value);
	Attribute *struct_fields = astree_get_attribute(ts->ast, struct_atr->data);
	LinkedList *elements = (LinkedList *)struct_fields->data;
	int structsize = 8 * linkedlist_len(elements);
	int len = structsize * tac_gen_constint(ts, node->left_child);
	hashmap_add(ts->array_len_map, node->symbol_value, heap_int(len));
	hashmap_add(ts->array_structsize_map, node->symbol_value, heap_int(structsize));
}

void tac_gen_types(T_S *ts, ASTNode *node) {
	if (!node) return;
	while (node->type == NTYPEL) {
		if (node->type == NATYPE)
			tac_gen_arrtype(ts, node->left_child);
		node = node->right_child;
	}
	if (node->type == NATYPE)
		tac_gen_arrtype(ts, node);
}

void tac_gen_array(T_S *ts, ASTNode *node) {
	if (!node) return;
	Attribute *atr = astree_get_attribute(ts->ast, node->symbol_value);
	u16 arr_offset = ts->array_counter++;
	astree_set_offset(ts->ast, node->symbol_value, arr_offset);
	int len = * (int*)hashmap_get(ts->array_len_map, atr->data);
	linkedlist_push_tail(ts->tac->arrays, heap_int(len));
}

void tac_gen_arrays(T_S *ts, ASTNode *node) {
	if (!node) return;
	while (node->type == NALIST) {
		tac_gen_array(ts, node->left_child);
		node = node->right_child;
	}
	tac_gen_array(ts, node);
}

void tac_gen_globals(T_S *ts, ASTNode* nglobs) {
	if (!nglobs) return;
	tac_gen_consts(ts, nglobs->left_child);
	tac_gen_types(ts, nglobs->middle_child);
	tac_gen_arrays(ts, nglobs->right_child);
}

TAC *tac_from_ast(ASTree *ast) {
	TAC *tac = tac_create();
	T_S *state = t_s_create(tac, ast);
	tac_gen_globals(state, ast->root->left_child);
	tac_gen_funcs(state, ast->root->middle_child);
	Adr main = adr_of_sds(state, sdsnew("main"));
	linkedlist_push_tail(tac->lines, unary_line(O_FUNC, main, 0));
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
	linkedlist_free(tac->arrays);
	linkedlist_free_ctx(tac->strings, sdsfree_wrapper);
}

void print_adr(Adr adr) {
	if (adr.type == A_EMPTY) {
		abort();
	}
	printf("%s%d", adr_prefix[adr.type], adr.adr);
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
		case O_LABEL: case O_GOTO:
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
		case O_STORE:
			printf("%s ", print_op[l->op]);
			print_adr(l->left);
			printf(" ");
			print_adr(l->right);
			break;
			break;
		// binary asign
		case O_ITOF:
		case O_NOT:
		case O_DEREF:
		case O_ALLOC:
			print_adr(l->left);
			printf(" = %s ", print_op[l->op]);
			print_adr(l->right);
			break;
		// ternary
		case O_ADDF: case O_ADDI: case O_SUBF: case O_SUBI: case O_MULF: case O_MULI: case O_DIVF: case O_DIVI: case O_POWIF: case O_POWII: case O_MOD:
		case O_EQI: case O_NEQI: case O_LTI: case O_LTEI: case O_GTI: case O_GTEI:
		case O_EQF: case O_NEQF: case O_LTF: case O_LTEF: case O_GTF: case O_GTEF:
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
	int i;
	i = 0;
	int *k;
	printf(".arrays (zero-init):\n");
	linkedlist_start(tac->arrays);
	while ((k=(int*)linkedlist_get_current(tac->arrays))) {
		printf("A%d: %d\n", i, *k);
		linkedlist_forward(tac->arrays);
		++i;
	}
	i = 0;
	printf(".ints:\n");
	linkedlist_start(tac->ints);
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
		if (l->linenum != 0) {
			printf("%d: ", l->linenum);
		}
		print_tac_line(l);
		linkedlist_forward(tac->lines);
	}
}

void* tac_data(TAC* tac, Adr adr) {
	int i;
	switch (adr.type) {
		case A_ILIT:
			linkedlist_start(tac->ints);
			for (i = 0; i < adr.adr; ++i) {
				linkedlist_forward(tac->ints);
			}
			return linkedlist_get_current(tac->ints);
		case A_ARRAY:
			linkedlist_start(tac->arrays);
			for (i = 0; i < adr.adr; ++i) {
				linkedlist_forward(tac->arrays);
			}
			return linkedlist_get_current(tac->arrays);
		case A_FLIT:
			linkedlist_start(tac->floats);
			for (i = 0; i < adr.adr; ++i) {
				linkedlist_forward(tac->floats);
			}
			return linkedlist_get_current(tac->floats);
		case A_STR:
			linkedlist_start(tac->strings);
			for (i = 0; i < adr.adr; ++i) {
				linkedlist_forward(tac->strings);
			}
			return linkedlist_get_current(tac->strings);
		default:
			abort();
	}
}

