// vimargs ../testprograms/valid6_heap.cd

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "lib/sds.h"
#include "lib/defs.h"
#include "lib/hashmap.h"
#include "code_generation.h"
#include "symboltable.h"
#include <stdarg.h>

enum addresstypes {
	AJUMP = -1,
	ASTR = -2,
	AINT = -3,
	AREAL = -4,
	AFUNC = -5
};

typedef struct instruction {
	u16 type;
	u32 value;
} Instruction;

typedef struct codegen {
	ASTree *ast;
	FILE *out_file;
	u16 inst_bytes;
	LinkedList *instructions;
	u16 num_ints; // for offset
	LinkedList *ints;
	LinkedList *int_offsets;
	u16 cur_int;
	u16 num_reals;
	LinkedList *reals;
	LinkedList *real_offsets;
	u16 cur_real;
	u16 str_bytes;
	LinkedList *strings;
	LinkedList *str_offsets;
	HashMap *symbol_offset_map; // <symbol, int>
	int *jump_addresses;
	int num_jumps;
	int jump_offsets_capacity;
	int num_global_arrays;
	int global_array_bytes;
	HashMap *array_len_map;
	HashMap *array_structsize_map;
	LinkedList *func_calls;
	u16 scope_of_main;
	// astree_get_attribute(cdg->ast, node->symbol_value)->offset = x;
} Codegen;

	void codegen_stats(Codegen *cdg, ASTNode *node);
	void codegen_numeric_push(Codegen *cdg, ASTNode *node);
	void codegen_expr_push(Codegen *cdg, ASTNode *node);
	void codegen_fncall(Codegen *cdg, ASTNode *node);
	void codegen_array_push(Codegen *cdg, ASTNode *node);
	void codegen_push_adr(Codegen *cdg, ASTNode *node);

static sds sds_substr(const sds src, size_t start, size_t len) {
	size_t src_len = sdslen(src);
	if (start >= src_len) return sdsnew("");  // empty string if start past end
	if (start + len > src_len) len = src_len - start;
	return sdsnewlen(src + start, len);
}

static sds sds_from_symbol(Codegen *cdg, Symbol *s) {
	return sds_substr(
		cdg->ast->symboltable->stringspace,
		s->index.start,
		s->index.len
	);
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

Codegen *codegen_create(const char *outfile, ASTree *ast) {
	Codegen *temp = malloc(sizeof(Codegen));
	*temp = (Codegen) {
		ast,
		fopen(outfile, "w"),
		0, // inst_bytes
		linkedlist_create(), // insts
		0, // num_ints
		linkedlist_create(), // ints
		linkedlist_create(), // int offsets
		0, // cur_int
		0, // num_reals
		linkedlist_create(), // reals
		linkedlist_create(), // real offsets
		0, // cur_real
		0, // str_bytes
		linkedlist_create(), // strings
		linkedlist_create(), // strlens
		hashmap_create(66, hash_symbol, equal_symbol),
		calloc(16, sizeof(int)),
		0, // num_jumps
		16, // jump offset capacity
		0, // number of global arrays
		0, // bytes created by global arrays
		hashmap_create(20, hash_symbol, equal_symbol), // symbol->arraysize
		hashmap_create(20, hash_symbol, equal_symbol), // symbol->structsize of the array
		linkedlist_create(), // function calls
		1, // scope_of_main
	};
	if (!temp->out_file) {
		fprintf(stderr, "could not create module file\n");
		abort();
	}
	return temp;
}

static void print_padded(Codegen *cdg, int byte) {
	fprintf(cdg->out_file, " %3d", byte);
	// if (byte < 10)
	// 	fprintf(cdg->out_file, "  0%d", byte);
	// else if (byte < 100)
	// 	fprintf(cdg->out_file, "  %d", byte);
	// else
	// 	fprintf(cdg->out_file, " %d", byte);
}

static void output_byte_to_mod(Codegen *cdg, int byte, int *counter) {
	print_padded(cdg, byte);
	(*counter)++;
	if ((*counter) % 8 == 0)
		fprintf(cdg->out_file, "\n");
}

void instr_print(Codegen* cdg, Instruction *instr, int *printed_insts) {
	output_byte_to_mod(cdg, instr->type, printed_insts);
	u8 bytes[4];
	memcpy(bytes, &instr->value, sizeof(instr->value));
	switch (instr->type) {
		case 41:
			output_byte_to_mod(cdg, bytes[0], printed_insts);
			break;
		case 42:
			output_byte_to_mod(cdg, bytes[1], printed_insts);
			output_byte_to_mod(cdg, bytes[0], printed_insts);
			break;
		case 80:
		case 81:
		case 82:
		case 90:
		case 91:
		case 92:
			for (int i = 0; i < 4; ++i) {
				output_byte_to_mod(cdg, bytes[3-i], printed_insts);
			}
			break;
	}
}

void print_sm25_string(Codegen* cdg, sds str_const, int *printed_chars) {
	for (int i = 0; i < sdslen(str_const); ++i) {
		output_byte_to_mod(cdg, str_const[i], printed_chars);
	}
	output_byte_to_mod(cdg, 0, printed_chars);
}

static void noop_free_ctx(void *p, va_list args) {
	(void)p;
	(void)args;
}

static void noop_free(void *p) {
	(void)p;
}

void codegen_close(Codegen *cdg) {
	fprintf(cdg->out_file, "%d\n", cdg->inst_bytes/8);
	int printed_insts = 0;
	Instruction *instr;
	// todo: use an iterator for this
	while ( (instr = (Instruction *)linkedlist_pop_head(cdg->instructions)) != NULL) {
		instr_print(cdg, instr, &printed_insts);
		free(instr);
	}
	int padded = 0;
	while (printed_insts % 8 != 0) {
		padded = 1;
		fprintf(cdg->out_file, "   0");
		printed_insts++;
	}
	if (padded)
		fprintf(cdg->out_file, "\n");
	linkedlist_free(cdg->instructions);
	fprintf(cdg->out_file, "%d\n", cdg->num_ints);
	Symbol *const_sym;
	while ( (const_sym = (Symbol *)linkedlist_pop_head(cdg->ints)) != NULL) {
		sds const_sds = sds_from_symbol(cdg, const_sym);
		fprintf(cdg->out_file, "%s\n", const_sds);
		sdsfree(const_sds);
	}
	linkedlist_free(cdg->ints);
	fprintf(cdg->out_file, "%d\n", cdg->num_reals);
	while ( (const_sym = (Symbol *)linkedlist_pop_head(cdg->reals)) != NULL) {
		sds const_sds = sds_from_symbol(cdg, const_sym);
		fprintf(cdg->out_file, "%s\n", const_sds);
		sdsfree(const_sds);
	}
	linkedlist_free(cdg->reals);
	fprintf(cdg->out_file, "%d\n", (int)ceil(cdg->str_bytes / 8.0));
	int printed_chars = 0;
	while ( (const_sym = (Symbol *)linkedlist_pop_head(cdg->strings)) != NULL) {
		sds const_sds = sds_from_symbol(cdg, const_sym);
		print_sm25_string(cdg, const_sds, &printed_chars);
		sdsfree(const_sds);
	}
	while (printed_chars % 8 != 0) {
		fprintf(cdg->out_file, "   0");
		printed_chars++;
	}
	linkedlist_free(cdg->strings);
	fclose(cdg->out_file);
	linkedlist_free_ctx(cdg->int_offsets, noop_free_ctx);
	linkedlist_free_ctx(cdg->real_offsets, noop_free_ctx);
	linkedlist_free_ctx(cdg->str_offsets, noop_free_ctx);
	hashmap_free(cdg->symbol_offset_map, noop_free, free);
	free(cdg->jump_addresses);
	free(cdg);
}

void push_instruction(Codegen *cdg, int type, int val) {
	Instruction *temp = malloc(sizeof(Instruction));
	*temp = (Instruction){ type, val };
	linkedlist_push_tail(cdg->instructions, temp);
	cdg->inst_bytes++;
	switch (type) {
		case 41:
			cdg->inst_bytes += 1;
			break;
		case 42:
			cdg->inst_bytes += 2;
			break;
		case 80:
		case 81:
		case 82:
		case 90:
		case 91:
		case 92:
			cdg->inst_bytes += 4;
			break;
	}
}

// todo: int to str conversion for word-sized ints?
void push_int_by_val(Codegen *cdg, int val) {
	if (val == 0) {
		push_instruction(cdg, 3, 0);
	} else if (val <= 255) {
		push_instruction(cdg, 41, val);
	} else if (val <= 65535) {
		push_instruction(cdg, 42, val);
	}
}

static int unscoped_symbol_equals(const Symbol *s1, const Symbol *s2) {
	return s1->index.start == s2->index.start &&
	       s1->index.len   == s2->index.len;
}

void codegen_var_push(Codegen *cdg, ASTNode *node) {
	if (node->type == NARRV) {
		codegen_push_adr(cdg, node);
		push_instruction(cdg, 40, 0);
		return;
	}
	if (node->type == NAELT)
	// todo: struct value asignment
		return;
	if (hashmap_get(cdg->symbol_offset_map, node->symbol_value)) { // local
		int address = 8 * *(int*)hashmap_get(cdg->symbol_offset_map, node->symbol_value);
		if (node->symbol_value->scope == cdg->scope_of_main) {
			address += cdg->global_array_bytes + 8*cdg->num_global_arrays;
			push_instruction(cdg, 81, address);
		} else
			push_instruction(cdg, 82, address);
	} else { // global
		Symbol *globscoped = malloc(sizeof(Symbol));
		*globscoped = *(node->symbol_value);
		globscoped->scope = 0;
		int *offset = (int*)hashmap_get(cdg->symbol_offset_map, globscoped);
		switch (node->symbol_type) {
			case SINT:
				linkedlist_push_tail(cdg->int_offsets, offset);
				push_instruction(cdg, 80, AINT);
				break;
			case SREAL:
				linkedlist_push_tail(cdg->real_offsets, offset);
				push_instruction(cdg, 80, AREAL);
				break;
		}
		free(globscoped);
	}
}

void codegen_numeric_push(Codegen *cdg, ASTNode *node) {
	Symbol *temp;
	int *offset;
	// first handle types that don't require recursive arithmetic
	switch (node->type) {
		case NILIT:
			sds glyph = sds_from_symbol(cdg, node->symbol_value);
			int value = atoi(glyph);
			if (value <= 65535) {
				push_int_by_val(cdg, value);
			} else {
				push_instruction(cdg, 80, AINT);
				temp = node->symbol_value;
				if (hashmap_contains(cdg->symbol_offset_map, temp)) {
					offset = (int*)hashmap_get(cdg->symbol_offset_map, temp);
					linkedlist_push_tail(cdg->int_offsets, offset);
				} else {
					linkedlist_push_tail(cdg->ints, temp);
					offset = malloc(sizeof(int));
					*offset = cdg->num_ints*8;
					hashmap_add(cdg->symbol_offset_map, temp, offset);
					linkedlist_push_tail(cdg->int_offsets, offset);
					cdg->num_ints++;
				}
			}
			sdsfree(glyph);
			return;
		case NFLIT:
			push_instruction(cdg, 80, AREAL);
			temp = node->symbol_value;
			if (hashmap_contains(cdg->symbol_offset_map, temp)) {
				offset = (int*)hashmap_get(cdg->symbol_offset_map, temp);
				linkedlist_push_tail(cdg->real_offsets, offset);
			} else {
				linkedlist_push_tail(cdg->reals, temp);
				offset = malloc(sizeof(int));
				*offset = cdg->num_reals*8;
				hashmap_add(cdg->symbol_offset_map, temp, offset);
				linkedlist_push_tail(cdg->real_offsets, offset);
				cdg->num_reals++;
			}
			return;
		case NSIMV:
		case NAELT:
		case NARRV:
			codegen_var_push(cdg, node);
			return;
		case NFCALL:
			codegen_fncall(cdg, node);
			return;
	}
	// it wasn't a leaf, so continue recursion
	codegen_numeric_push(cdg, node->left_child);
	codegen_numeric_push(cdg, node->right_child);
	switch (node->type) {
		case NADD:
			push_instruction(cdg, 11, 0);
			break;
		case NSUB:
			push_instruction(cdg, 12, 0);
			break;
		case NMUL:
			push_instruction(cdg, 13, 0);
			break;
		case NDIV:
			push_instruction(cdg, 14, 0);
			break;
		case NMOD:
			push_instruction(cdg, 15, 0);
			break;
		case NPOW:
			push_instruction(cdg, 16, 0);
			break;
	}
}

void codegen_printitem(Codegen *cdg, ASTNode *node) {
	if (node->type == NSTRG) {
		push_instruction(cdg, 90, ASTR);
		push_instruction(cdg, 63, 0);
		int *stroffset = malloc(sizeof(int));
		*stroffset = cdg->str_bytes;
		linkedlist_push_tail(cdg->str_offsets, stroffset);
		Symbol *temp = node->symbol_value;
		linkedlist_push_tail(cdg->strings, temp);
		cdg->str_bytes += temp->index.len + 1;
	} else {
		codegen_numeric_push(cdg, node);
		push_instruction(cdg, 62, 0);
	}
}

void codegen_prlist(Codegen *cdg, ASTNode *node) {
	if (node->type == NPRLST) {
		codegen_printitem(cdg, node->left_child);
		codegen_prlist(cdg, node->right_child);
	} else {
		codegen_printitem(cdg, node);
	}
}

void codegen_noutp(Codegen *cdg, ASTNode *node) {
	codegen_prlist(cdg, node->left_child);
}

void codegen_noutl(Codegen *cdg, ASTNode *node) {
	if (!node->left_child) {
		push_instruction(cdg, 65, 0);
		return;
	}
	codegen_prlist(cdg, node->left_child);
	push_instruction(cdg, 65, 0);
}

void codegen_input_var(Codegen *cdg, ASTNode *node) {
	if (node->type == NSIMV) {
		if (node->symbol_value->scope == cdg->scope_of_main) {
			int offset = cdg->global_array_bytes + 8*cdg->num_global_arrays + 8 * *(int*)hashmap_get(cdg->symbol_offset_map, node->symbol_value);
			push_instruction(cdg, 91, offset);
		} else {
			int offset = 8 * *(int*)hashmap_get(cdg->symbol_offset_map, node->symbol_value);
			push_instruction(cdg, 92, offset);
		}
		if (node->symbol_type == SREAL) {
			push_instruction(cdg, 60, 0);
		} else {
			push_instruction(cdg, 61, 0);
		}
		push_instruction(cdg, 43, 0);
	} else if (node->type == NARRV) {
		Attribute *array_atr = astree_get_attribute(cdg->ast, node->left_child->symbol_value);
		codegen_array_push(cdg, node->left_child);
		codegen_numeric_push(cdg, node->right_child);
		int struct_size = * (int*)hashmap_get(cdg->array_structsize_map, array_atr->data);
		if (struct_size != 1) {
			push_instruction(cdg, 41, struct_size);
			push_instruction(cdg, 13, 0);
		}
		int struct_offset = 0;
		Attribute *struct_atr = astree_get_attribute(cdg->ast, array_atr->data);
		Attribute *struct_fields = astree_get_attribute(cdg->ast, struct_atr->data);
		LinkedList *elements = (LinkedList *)struct_fields->data;
		linkedlist_start(elements);
		Symbol *target_element = node->symbol_value;
		while (!unscoped_symbol_equals(
			((Element*)linkedlist_get_current(elements))->name,
			target_element
		) ) {
			linkedlist_forward(elements);
			struct_offset++;
		}
		if (struct_offset != 0) {
			push_int_by_val(cdg, struct_offset);
			push_instruction(cdg, 11, 0);
		}
		push_instruction(cdg, 54, 0);
		if (node->symbol_type == SREAL) {
			push_instruction(cdg, 60, 0);
		} else {
			push_instruction(cdg, 61, 0);
		}
		push_instruction(cdg, 43, 0);
	}
}

void codegen_nvlist(Codegen *cdg, ASTNode *node) {
	if (node->type == NVLIST) {
		codegen_input_var(cdg, node->left_child);
		codegen_nvlist(cdg, node->right_child);
	} else {
		codegen_input_var(cdg, node);
	}
}

void codegen_ninput(Codegen *cdg, ASTNode *node) {
	codegen_nvlist(cdg, node->left_child);
}

void codegen_relop_push(Codegen *cdg, ASTNode *node) {
	push_instruction(cdg, 12, 0);
	switch (node->type) {
		case NGRT:
			push_instruction(cdg, 21, 0);
			break;
		case NGEQ:
			push_instruction(cdg, 22, 0);
			break;
		case NLSS:
			push_instruction(cdg, 23, 0);
			break;
		case NLEQ:
			push_instruction(cdg, 24, 0);
			break;
		case NEQL:
			push_instruction(cdg, 25, 0);
			break;
		case NNEQ:
			push_instruction(cdg, 26, 0);
			break;
	}
}

void codegen_boolean_push(Codegen *cdg, ASTNode *node) {
	switch (node->type) {
		case NFALS:
			push_instruction(cdg, 4, 0);
			break;
		case NTRUE:
			push_instruction(cdg, 5, 0);
			break;
		case NSIMV:
		case NARRV:
			codegen_var_push(cdg, node);
			break;
		case NNOT:
			codegen_numeric_push(cdg, node->left_child);
			codegen_numeric_push(cdg, node->right_child);
			codegen_relop_push(cdg, node->middle_child);
			push_instruction(cdg, 34, 0);
			break;
		case NBOOL:
			codegen_boolean_push(cdg, node->left_child);
			codegen_boolean_push(cdg, node->right_child);
			switch (node->middle_child->type) {
				case NAND:
					push_instruction(cdg, 31, 0);
					break;
				case NOR:
					push_instruction(cdg, 32, 0);
					break;
				case NXOR:
					push_instruction(cdg, 33, 0);
					break;
			}
			break;
		case NEQL:
		case NNEQ:
		case NGRT:
		case NLSS:
		case NLEQ:
		case NGEQ:
			codegen_numeric_push(cdg, node->left_child);
			codegen_numeric_push(cdg, node->right_child);
			codegen_relop_push(cdg, node);
			break;
		case NFCALL:
			codegen_fncall(cdg, node);
			break;
	}
}

// push the address of the given variable in SM25
void codegen_push_adr(Codegen *cdg, ASTNode *node) {
	switch (node->type) {
		case NARRV:
			Attribute *array_atr = astree_get_attribute(cdg->ast, node->left_child->symbol_value);
			codegen_array_push(cdg, node->left_child);
			codegen_numeric_push(cdg, node->right_child);
			// todo: why is the scope of that not 0? it's written as 0
			((Symbol*)array_atr->data)->scope = 0;
			int struct_size = * (int*)hashmap_get(cdg->array_structsize_map, array_atr->data);
			if (struct_size != 1) {
				push_int_by_val(cdg, struct_size);
				push_instruction(cdg, 13, 0);
			}
			int struct_offset = 0;
			Attribute *struct_atr = astree_get_attribute(cdg->ast, array_atr->data);
			Attribute *struct_fields = astree_get_attribute(cdg->ast, struct_atr->data);
			LinkedList *elements = (LinkedList *)struct_fields->data;
			linkedlist_start(elements);
			Symbol *target_element = node->symbol_value;
			while (!unscoped_symbol_equals(
				((Element*)linkedlist_get_current(elements))->name,
				target_element
			) ) {
				linkedlist_forward(elements);
				struct_offset++;
			}
			// this should be in an optimiser module, if there were one
			if (struct_offset != 0) {
				push_int_by_val(cdg, struct_offset);
				push_instruction(cdg, 11, 0);
			}
			push_instruction(cdg, 54, 0);
			break;
		case NSIMV:
			int offset = 8 * *(int*)hashmap_get(cdg->symbol_offset_map, node->symbol_value);
			if (node->symbol_value->scope == cdg->scope_of_main) {
				offset += cdg->global_array_bytes + 8*cdg->num_global_arrays;
				push_instruction(cdg, 91, offset);
			} else
				push_instruction(cdg, 92, offset);
			break;
	}
}

void codegen_nasgn(Codegen *cdg, ASTNode *node) {
	switch (node->right_child->symbol_type) {
		case SINT:
		case SREAL:
			codegen_push_adr(cdg, node->left_child);
			codegen_numeric_push(cdg, node->right_child);
			push_instruction(cdg, 43, 0);
			break;
		case SBOOL:
			codegen_push_adr(cdg, node->left_child);
			codegen_boolean_push(cdg, node->right_child);
			push_instruction(cdg, 43, 0);
			break;
		case SARRAY:
			Attribute *array_atr0 = astree_get_attribute(cdg->ast, node->left_child->symbol_value);
			int array_size = *(int*)hashmap_get(cdg->array_len_map, array_atr0->data);
			// todo: figure out how to do an inline loop, by creating i as a variable on the stack, then referencing it. would be easy if "top of stack" was a base register
			for (int i = 0; i < array_size; ++i) {
				codegen_array_push(cdg, node->left_child);
				push_int_by_val(cdg, i);
				push_instruction(cdg, 54, 0);
				codegen_array_push(cdg, node->right_child);
				push_int_by_val(cdg, i);
				push_instruction(cdg, 54, 0);
				push_instruction(cdg, 40, 0);
				push_instruction(cdg, 43, 0);
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
					push_instruction(cdg, 13, 0);
				}
				push_int_by_val(cdg, field);
				push_instruction(cdg, 11, 0);
				push_instruction(cdg, 54, 0);
				codegen_array_push(cdg, node->right_child->left_child);
				codegen_numeric_push(cdg, node->right_child->right_child);
				if (struct_size != 1) {
					push_int_by_val(cdg, struct_size);
					push_instruction(cdg, 13, 0);
				}
				push_int_by_val(cdg, field);
				push_instruction(cdg, 11, 0);
				push_instruction(cdg, 54, 0);
				push_instruction(cdg, 40, 0);
				push_instruction(cdg, 43, 0);
			}
			break;
	}
}

void codegen_nasgnop(Codegen *cdg, ASTNode *node) {
	int offset = 8 * *(int*)hashmap_get(cdg->symbol_offset_map, node->left_child->symbol_value);
	if (node->left_child->symbol_value->scope == cdg->scope_of_main) {
		offset += cdg->global_array_bytes + 8*cdg->num_global_arrays;
		push_instruction(cdg, 91, offset);
	} else
		push_instruction(cdg, 92, offset);
	switch (node->type) {
		case NPLEQ:
		case NMNEQ:
		case NSTEA:
		case NDVEQ:
			push_instruction(cdg, 56, 0);
			push_instruction(cdg, 40, 0);
			break;
	}
	switch (node->right_child->symbol_type) {
		case SINT:
		case SREAL:
			codegen_numeric_push(cdg, node->right_child);
			break;
		case SBOOL:
			codegen_boolean_push(cdg, node->right_child);
			break;
	}
	switch (node->type) {
		case NPLEQ:
			push_instruction(cdg, 11, offset);
			break;
		case NMNEQ:
			push_instruction(cdg, 12, offset);
			break;
		case NSTEA:
			push_instruction(cdg, 13, offset);
			break;
		case NDVEQ:
			push_instruction(cdg, 14, offset);
			break;
	}
	push_instruction(cdg, 43, 0);
}

void anchor_jump(Codegen *cdg, int arr_index) {
	if (arr_index >= cdg->jump_offsets_capacity) {
		cdg->jump_addresses = realloc(cdg->jump_addresses, cdg->num_jumps * 2 * sizeof(int));
		cdg->jump_offsets_capacity *= 2;
	}
	cdg->jump_addresses[arr_index] = cdg->inst_bytes;
}

void codegen_if(Codegen *cdg, ASTNode *node) {
	int arr_index = cdg->num_jumps++;
	push_instruction(cdg, 90, AJUMP);
	codegen_boolean_push(cdg, node->left_child);
	push_instruction(cdg, 36, 0);
	codegen_stats(cdg, node->right_child);
	anchor_jump(cdg, arr_index);
}

void codegen_ifelse(Codegen *cdg, ASTNode *node) {
	int arr_index = cdg->num_jumps++;
	push_instruction(cdg, 90, AJUMP);
	codegen_boolean_push(cdg, node->left_child);
	push_instruction(cdg, 36, 0); // first jump
	codegen_stats(cdg, node->middle_child);
	push_instruction(cdg, 90, AJUMP);
	push_instruction(cdg, 37, 0); // second jump

	anchor_jump(cdg, arr_index); // first jump
	arr_index = cdg->num_jumps++;
	codegen_stats(cdg, node->right_child);
	anchor_jump(cdg, arr_index); // second jump
}

void codegen_asgnlist(Codegen *cdg, ASTNode *node) {
	if (!node)
		return;
	if (node->type == NASGNS) {
		codegen_nasgnop(cdg, node->left_child);
		codegen_asgnlist(cdg, node->right_child);
	}
	codegen_nasgnop(cdg, node);
}

void codegen_for(Codegen *cdg, ASTNode *node) {
	codegen_asgnlist(cdg, node->left_child);
	int start_anchor = cdg->inst_bytes; // second jump
	int arr_index = cdg->num_jumps++;
	push_instruction(cdg, 90, AJUMP);
	codegen_boolean_push(cdg, node->middle_child);
	push_instruction(cdg, 36, 0); // first jump
	codegen_stats(cdg, node->right_child);
	// jump back to start
	push_instruction(cdg, 90, start_anchor);
	push_instruction(cdg, 37, 0); // second jump
	anchor_jump(cdg, arr_index); // first jump
}

void codegen_repeat(Codegen *cdg, ASTNode *node) {
	codegen_asgnlist(cdg, node->left_child);
	int start_anchor = cdg->inst_bytes;
	codegen_stats(cdg, node->middle_child);
	push_instruction(cdg, 90, start_anchor);
	codegen_boolean_push(cdg, node->right_child);
	push_instruction(cdg, 36, 0);
}

void codegen_array_push(Codegen *cdg, ASTNode *node) {
	// todo: local array
	int arr_offset;
	Symbol *globscoped = malloc(sizeof(Symbol));
	*globscoped = *(node->symbol_value);
	globscoped->scope = 0;
	if (hashmap_get(cdg->symbol_offset_map, node->symbol_value)) {
		arr_offset = 8 * *(int*)hashmap_get(cdg->symbol_offset_map, node->symbol_value);
		push_instruction(cdg, 82, arr_offset);
	} else {
		arr_offset = 8 * *(int*)hashmap_get(cdg->symbol_offset_map, globscoped);
		push_instruction(cdg, 81, arr_offset);
	}
	free(globscoped);
}

void codegen_expr_push(Codegen *cdg, ASTNode *node) {
	switch (node->symbol_type) {
		case SBOOL:
			codegen_boolean_push(cdg, node);
			break;
		case SINT:
		case SREAL:
			codegen_numeric_push(cdg, node);
			break;
		case SARRAY:
			codegen_array_push(cdg, node);
			break;
	}
}

// push in reverse order, to be accessed in the right order on the stack
void codegen_parameters(Codegen *cdg, ASTNode *node, int *paramcount) {
	if (!node) return;
	if (node->type == NEXPL) {
		codegen_parameters(cdg, node->right_child, paramcount);
		codegen_expr_push(cdg, node->left_child);
		(*paramcount)++;
	} else {
		codegen_expr_push(cdg, node);
		(*paramcount)++;
	}
}

void codegen_callstat(Codegen *cdg, ASTNode *node) {
	linkedlist_push_tail(cdg->func_calls, node->symbol_value);
	int paramcount = 0;
	codegen_parameters(cdg, node->left_child, &paramcount);
	if (paramcount == 0) {
		push_instruction(cdg, 3, 0);
	} else if (paramcount <= 255) {
		push_instruction(cdg, 41, paramcount);
	} else if (paramcount <= 65535) {
		push_instruction(cdg, 42, paramcount);
	}
	push_instruction(cdg, 90, AFUNC);
	push_instruction(cdg, 72, 0);
}

void codegen_fncall(Codegen *cdg, ASTNode *node) {
	// the return address
	push_instruction(cdg, 41, 1);
	push_instruction(cdg, 52, 0);
	codegen_callstat(cdg, node);
}

void codegen_returnstat(Codegen *cdg, ASTNode *node) {
	if (node->left_child) {
		codegen_expr_push(cdg, node->left_child);
		push_instruction(cdg, 70, 0);
	}
	push_instruction(cdg, 71, 0);
}

void codegen_stat(Codegen *cdg, ASTNode *node) {
	switch (node->type) {
		case NOUTP:
			codegen_noutp(cdg, node);
			break;
		case NOUTL:
			codegen_noutl(cdg, node);
			break;
		case NINPUT:
			codegen_ninput(cdg, node);
			break;
		case NIFTH:
			codegen_if(cdg, node);
			break;
		case NIFTE:
			codegen_ifelse(cdg, node);
			break;
		case NREPT:
			codegen_repeat(cdg, node);
			break;
		case NFORL:
			codegen_for(cdg, node);
			break;
		case NASGN:
			codegen_nasgn(cdg, node);
			break;
		case NPLEQ:
		case NMNEQ:
		case NSTEA:
		case NDVEQ:
			codegen_nasgnop(cdg, node);
			break;
		case NCALL:
			codegen_callstat(cdg, node);
			break;
		case NRETN:
			codegen_returnstat(cdg, node);
			break;
	}
}

void codegen_stats(Codegen *cdg, ASTNode *node) {
	if (node->type == NSTATS) {
		codegen_stat(cdg, node->left_child);
		if (node->right_child)
			codegen_stats(cdg, node->right_child);
	} else {
		codegen_stat(cdg, node);
	}
}

void codegen_sdecl(Codegen *cdg, ASTNode *node) {
	int offset;
	switch (node->symbol_type) {
		case SREAL:
			// todo: ask what happens to uninitialised reals/bools
			offset = cdg->global_array_bytes + 8*cdg->num_global_arrays + 8 * *(int*)hashmap_get(cdg->symbol_offset_map, node->symbol_value);
			push_instruction(cdg, 91, offset);
			push_instruction(cdg, 3, 0);
			push_instruction(cdg, 7, 0);
			push_instruction(cdg, 43, 0);
			break;
		case SINT:
			offset = cdg->global_array_bytes + 8*cdg->num_global_arrays + 8 * *(int*)hashmap_get(cdg->symbol_offset_map, node->symbol_value);
			push_instruction(cdg, 91, offset);
			push_instruction(cdg, 3, 0);
			push_instruction(cdg, 43, 0);
			break;
		case SBOOL: // assumption: uninitialised booleans are false
			offset = cdg->global_array_bytes + 8*cdg->num_global_arrays + 8 * *(int*)hashmap_get(cdg->symbol_offset_map, node->symbol_value);
			push_instruction(cdg, 91, offset);
			push_instruction(cdg, 4, 0);
			push_instruction(cdg, 43, 0);
			break;
	}
}

void add_var_offset(Codegen *cdg, ASTNode *sdecl, int offset) {
	if (sdecl->type == NARRC)
		sdecl = sdecl->left_child;
	int *heap_num_vars = malloc(sizeof(int));
	*heap_num_vars = offset;
	hashmap_add(cdg->symbol_offset_map, sdecl->symbol_value, heap_num_vars);
}

void codegen_slist(Codegen *cdg, ASTNode *node) {
	int num_vars = 0;
	ASTNode *cursor = node;
	while (cursor->type == NSDLST) {
		add_var_offset(cdg, cursor->left_child, num_vars);
		num_vars++;
		cursor = cursor->right_child;
	}
	add_var_offset(cdg, cursor, num_vars);
	num_vars++;
	push_instruction(cdg, 41, num_vars);
	push_instruction(cdg, 52, 0);
	cursor = node;
	while (cursor->type == NSDLST) {
		codegen_sdecl(cdg, cursor->left_child);
		cursor = cursor->right_child;
	}
	codegen_sdecl(cdg, cursor);
}

void codegen_main(Codegen *cdg, ASTNode *nmain) {
	codegen_slist(cdg, nmain->left_child);
	codegen_stats(cdg, nmain->right_child);
}

void codegen_update_addresses(Codegen *cdg) {
	linkedlist_start(cdg->int_offsets);
	linkedlist_start(cdg->real_offsets);
	linkedlist_start(cdg->str_offsets);
	linkedlist_start(cdg->func_calls);
	int jump_address_index = 0;
	linkedlist_start(cdg->instructions);
	while (linkedlist_get_current(cdg->instructions)) {
		Instruction *cur = (Instruction*)linkedlist_get_current(cdg->instructions);
		switch (cur->type) {
			case 80:
				switch (cur->value) {
					case AINT: //int
						int int_offset = *(int *)linkedlist_get_current(cdg->int_offsets);
						cur->value = cdg->inst_bytes + int_offset;
						linkedlist_forward(cdg->int_offsets);
						break;
					case AREAL: //real
						int real_offset = *(int *)linkedlist_get_current(cdg->real_offsets);
						cur->value = cdg->inst_bytes + cdg->num_ints*8 + real_offset;
						linkedlist_forward(cdg->real_offsets);
						break;
				}
			case 90:
				switch (cur->value) {
					case AINT: //int
						int int_offset = *(int *)linkedlist_get_current(cdg->int_offsets);
						cur->value = cdg->inst_bytes + int_offset;
						linkedlist_forward(cdg->int_offsets);
						break;
					case AREAL: //real
						int real_offset = *(int *)linkedlist_get_current(cdg->real_offsets);
						cur->value = cdg->inst_bytes + cdg->num_ints*8 + real_offset;
						linkedlist_forward(cdg->real_offsets);
						break;
					case ASTR: //string
						int str_offset = *(int *)linkedlist_get_current(cdg->str_offsets);
						cur->value = cdg->inst_bytes + cdg->num_ints*8 + cdg->num_reals*8 + str_offset;
						linkedlist_forward(cdg->str_offsets);
						break;
					case AJUMP: //jump
						cur->value = cdg->jump_addresses[jump_address_index++];
						break;
					case AFUNC: //function call
						Symbol glob_scoped = *(Symbol*)linkedlist_get_current(cdg->func_calls);
						glob_scoped.scope = 0;
						cur->value = *(int *)hashmap_get(cdg->symbol_offset_map, &glob_scoped);
						linkedlist_forward(cdg->func_calls);
				}
				break;
		}
		linkedlist_forward(cdg->instructions);
	}
}

void codegen_init(Codegen *cdg, ASTNode* node) {
	if (node->left_child->type != NILIT && node->left_child->type != NFLIT) {
		// todo: put constexpr here (nontrivial because int is not symbol)
		printf("sorry, only integer/float literals are implemented as constants\n");
		return;
	}
	int *offset;
	if (node->left_child->type == NILIT) {
		Symbol *temp = node->left_child->symbol_value;
		linkedlist_push_tail(cdg->ints, temp);
		offset = malloc(sizeof(int));
		*offset = cdg->num_ints*8;
		hashmap_add(cdg->symbol_offset_map, node->symbol_value, offset);
		// linkedlist_push_tail(cdg->int_offsets, offset);
		cdg->num_ints++;
	}
	if (node->left_child->type == NFLIT) {
		Symbol *temp = node->left_child->symbol_value;
		linkedlist_push_tail(cdg->reals, temp);
		offset = malloc(sizeof(int));
		*offset = cdg->num_reals*8;
		hashmap_add(cdg->symbol_offset_map, node->symbol_value, offset);
		// linkedlist_push_tail(cdg->real_offsets, offset);
		cdg->num_reals++;
	}
}

void codegen_consts(Codegen *cdg, ASTNode* node) {
	if (!node) return;
	while (node->type == NILIST) {
		codegen_init(cdg, node->left_child);
		node = node->right_child;
	}
	codegen_init(cdg, node);
}

int codegen_constexpr(Codegen *cdg, ASTNode* node) {
	sds glyph;
	int result;
	switch (node->type) {
		case NILIT:
			glyph = sds_from_symbol(cdg, node->symbol_value);
			result = atoi(glyph);
			sdsfree(glyph);
			return result;
		case NSIMV:
			int offset = * (int*)hashmap_get(cdg->symbol_offset_map, node->symbol_value);
			linkedlist_start(cdg->ints);
			for (int i = 0; i < offset; ++i) {
				linkedlist_forward(cdg->ints);
			}
			glyph = sds_from_symbol(cdg, (Symbol*)linkedlist_get_current(cdg->ints));
			result = atoi(glyph);
			sdsfree(glyph);
			return result;
			break;
	}
	int lhs = codegen_constexpr(cdg, node->left_child);
	int rhs = codegen_constexpr(cdg, node->right_child);
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
	}
}

void codegen_arrtype(Codegen *cdg, ASTNode *node) {
	Attribute *struct_atr = astree_get_attribute(cdg->ast, node->symbol_value);
	Attribute *struct_fields = astree_get_attribute(cdg->ast, struct_atr->data);
	LinkedList *elements = (LinkedList *)struct_fields->data;
	int structsize = 0;
	linkedlist_start(elements);
	while (linkedlist_get_current(elements)) {
		linkedlist_forward(elements);
		structsize++;
	}
	int *len = malloc(sizeof(int));
	*len = structsize * codegen_constexpr(cdg, node->left_child);
	// cdg->global_array_bytes += 8 * *len;
	hashmap_add(cdg->array_len_map, node->symbol_value, len);
	int *structsize_heap = malloc(sizeof(int));
	*structsize_heap = structsize;
	hashmap_add(cdg->array_structsize_map, node->symbol_value, structsize_heap);
}

void codegen_types(Codegen *cdg, ASTNode *node) {
	if (!node) return;
	while (node->type == NTYPEL) {
		if (node->type == NATYPE)
			codegen_arrtype(cdg, node->left_child);
		node = node->right_child;
	}
	if (node->type == NATYPE)
		codegen_arrtype(cdg, node);
}

// todo: initialise the array using struct fields for 0, 0.0, false
void codegen_array(Codegen *cdg, ASTNode *node) {
	if (!node) return;
	Attribute *atr = astree_get_attribute(cdg->ast, node->symbol_value);
	int *arr_offset = malloc(sizeof(int));
	*arr_offset = cdg->num_global_arrays;
	hashmap_add(cdg->symbol_offset_map, node->symbol_value, arr_offset);
	cdg->global_array_bytes += 8 * *(int*)hashmap_get(cdg->array_len_map, atr->data);
	push_instruction(cdg, 91, 8*cdg->num_global_arrays++);
	int len = * (int*)hashmap_get(cdg->array_len_map, atr->data);
	push_int_by_val(cdg, len);
	push_instruction(cdg, 53, 0);
}

void codegen_arrays(Codegen *cdg, ASTNode *node) {
	if (!node) return;
	int count = 1;
	ASTNode *scan = node;
	while (scan->type == NALIST) {
		count++;
		scan = scan->right_child;
	}
	push_int_by_val(cdg, count);
	push_instruction(cdg, 52, 0);
	while (node->type == NALIST) {
		codegen_array(cdg, node->left_child);
		node = node->right_child;
	}
	codegen_array(cdg, node);
}

void codegen_globals(Codegen *cdg, ASTNode* nglobs) {
	if (!nglobs) return;
	codegen_consts(cdg, nglobs->left_child);
	codegen_types(cdg, nglobs->middle_child);
	codegen_arrays(cdg, nglobs->right_child);
}

void codegen_plist(Codegen *cdg, ASTNode *node) {
	// todo: this could be in parser
	int num_vars = 0;
	while (node->type == NPLIST) {
		add_var_offset(cdg, node->left_child, -1*(1+num_vars++));
		node = node->right_child;
	}
	add_var_offset(cdg, node, -1*(1+num_vars++));
}

void codegen_decl(Codegen *cdg, ASTNode *node) {
	int offset = 8 * *(int*)hashmap_get(cdg->symbol_offset_map, node->symbol_value);
	switch (node->symbol_type) {
		case SREAL:
			push_instruction(cdg, 92, offset);
			push_instruction(cdg, 3, 0);
			push_instruction(cdg, 7, 0);
			push_instruction(cdg, 43, 0);
			break;
		case SINT:
			push_instruction(cdg, 92, offset);
			push_instruction(cdg, 3, 0);
			push_instruction(cdg, 43, 0);
			break;
		case SBOOL:
			break;
	}
}

void codegen_func_locals(Codegen *cdg, ASTNode* node) {
	if (!node) return;
	int num_vars = 0;
	ASTNode *cursor = node;
	while (cursor->type == NDLIST) {
		add_var_offset(cdg, cursor->left_child, 2+num_vars++);
		cursor = cursor->right_child;
	}
	add_var_offset(cdg, cursor, 2+num_vars++);
	push_instruction(cdg, 41, num_vars);
	push_instruction(cdg, 52, 0);
	cursor = node;
	while (cursor->type == NDLIST) {
		codegen_decl(cdg, cursor->left_child);
		cursor = cursor->right_child;
	}
	codegen_decl(cdg, cursor);
}

void codegen_func(Codegen *cdg, ASTNode* nfuncs) {
	int *adr = malloc(sizeof(int));
	*adr = cdg->inst_bytes;
	hashmap_add(cdg->symbol_offset_map, nfuncs->symbol_value, adr);
	codegen_plist(cdg, nfuncs->left_child);
	codegen_func_locals(cdg, nfuncs->middle_child);
	codegen_stats(cdg, nfuncs->right_child);
}

void codegen_funcs(Codegen *cdg, ASTNode* nfuncs) {
	if (!nfuncs) return;
	while (nfuncs && nfuncs->left_child) {
		codegen_func(cdg, nfuncs->left_child);
		nfuncs = nfuncs->right_child;
	}
	if (nfuncs)
		codegen_func(cdg, nfuncs);
}

void code_gen(char *mod_output, ASTree *ast) {
	Codegen *cdg = codegen_create(mod_output, ast);
	cdg->scope_of_main = ast->root->right_child->symbol_value->scope;
	codegen_globals(cdg, ast->root->left_child);
	codegen_main(cdg, ast->root->right_child);
	push_instruction(cdg, 0, 0);
	codegen_funcs(cdg, ast->root->middle_child);
	// padding byte counter to word size (printing doesn't use this value, but constant addresses do)
	cdg->str_bytes += cdg->str_bytes % 8 == 0
		? 0
		: 8 - cdg->str_bytes % 8;
	cdg->inst_bytes += cdg->inst_bytes % 8 == 0
		? 0
		: 8 - cdg->inst_bytes % 8;
	codegen_update_addresses(cdg);
	codegen_close(cdg);
}

