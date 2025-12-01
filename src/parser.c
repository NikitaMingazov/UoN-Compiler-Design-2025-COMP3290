// vimargs ../cd25_programs/valid3_softmax.cd
/*
  produces AST from the given filename (or returns null and prints errors to lister)
  known memory leak when the longjmp skips freeing allocated memory, but whatever
*/
#include "lib/sds.h"
#include "lib/defs.h"
#include "parser.h"
#include "lexer.h"
#include "lister.h"
#include "token.h"
#include "node.h"
#include "symboltable.h"
#include <string.h>
#include <stdlib.h>
#include <libgen.h>
#include <setjmp.h>

typedef struct parser {
	Lexer *lex;
	Token *c/*urrent*/;
	Token *n/*ext*/;
	ASTree *ast;
	u16 scope; // global=0, func∈[1,N), main=N
	Lister *lst;
	u16 progress;
	int fresh_error;
	int in_recovery;
	u16 int_offset;
	u16 real_offset;
	u16 string_offset;
} Parser;


void *heap_wrap_stype(enum symbol_type s) {
	enum symbol_type *sp = malloc(sizeof(enum symbol_type));
	*sp = s;
	return sp;
}

void symbol_redefinition_error(Parser *p, u16 row, u16 col) {
	p->ast->is_valid = 0;
	lister_sem_error(p->lst, row, col, "redefinition of variable within the same scope");
}

void append_attribute_from_node(Parser *p, LinkedList *attributes, ASTNode *leaf) {
	if (leaf->type == NARRC) // todo: const checking
		leaf = leaf->left_child;
	Attribute *cur_atr = malloc(sizeof(Attribute));
	if (leaf->type == NARRD) {
		cur_atr = astree_get_attribute(p->ast, leaf->symbol_value);
	} else {
		cur_atr->type = leaf->symbol_type;
		cur_atr->data = NULL;
	}
	linkedlist_push_tail(attributes, cur_atr);
}

Attribute *make_func_attribute(Parser *p, ASTNode *param_nodes, enum symbol_type type) {
	Attribute *result = malloc(sizeof(Attribute));
	result->type = type;
	if (!param_nodes) {
		result->data = linkedlist_create();
		return result;
	}
	LinkedList *params = linkedlist_create();
	if (param_nodes->type == NPLIST) {
		while (param_nodes->type == NPLIST) {
			append_attribute_from_node(p, params, param_nodes->left_child);
			param_nodes = param_nodes->right_child;
		}
	}
	append_attribute_from_node(p, params, param_nodes);
	result->data = params;
	return result;
}

void append_struct_element(Parser *p, LinkedList *elements, ASTNode *field) {
	// Attribute *cur = astree_get_attribute(p->ast, field->symbol_value);
	Element *element = malloc(sizeof(Element));
	*element = (Element){ field->symbol_value, field->symbol_type };
	linkedlist_push_tail(elements, element);
}

Attribute *struct_types_attribute(Parser *p, ASTNode *fields) {
	Attribute *result = malloc(sizeof(Attribute));
	result->type = SFIELDS;
	if (!fields) {
		result->data = linkedlist_create();
		return result;
	}
	LinkedList *elements = linkedlist_create();
	while (fields->type == NFLIST) {
		append_struct_element(p, elements, fields->left_child);
		fields = fields->right_child;
	}
	append_struct_element(p, elements, fields);
	result->data = elements;
	return result;
}

// this is the only file that needs to free tokens
void free_token(Token *t) {
	if (t->val)
		sdsfree(t->val);
	free(t);
}

// gets the next token and releases the current one
void next_token(Parser *p, int owner_of_value) {
	owner_of_value ? free_token(p->c) : free(p->c);
	p->c = p->n;
	p->n = lexer_get_token(p->lex);
}

static char *format_cstr(const char *format, const char *str1, const char *str2) {
    // Calculate required size
    int needed = snprintf(NULL, 0, format, str1, str2);
    if (needed < 0) return NULL;
    // Allocate and format
    char *msg = malloc(needed + 1);
    if (!msg) return NULL;
    snprintf(msg, needed + 1, format, str1, str2);
    return msg;
}

void error_recovery(Parser *p);

// wrapper for next_token with a syntax check (returns 1 if error)
int match(Parser *p, enum token_type expected) {
	int result = 0;
	if (p->c->type != expected && p->fresh_error) {
		p->fresh_error = 0; // only one error per recovery
		lister_syn_error(p->lst, p->c->row, p->c->col, format_cstr(
			"expected to see %s, but saw %s", TPRINT[expected], TPRINT[p->c->type]
			// todo: create a map from token back to plaintext
		));
		if (!p->in_recovery) { // preventing recursion in the error state
			p->ast->is_valid = 0;
			error_recovery(p);
		}
		result = 1;
	}
	next_token(p, 1);
	return result;
}

ASTNode *make_node(enum node_type type, u16 row, u16 col, enum symbol_type symbol_type, Symbol *symbol_value) {
	ASTNode *temp = malloc(sizeof(ASTNode));
	temp->type = type;
	temp->row = row;
	temp->col = col;
	temp->symbol_type = symbol_type;
	temp->symbol_value = symbol_value;
	temp->left_child = NULL;
	temp->middle_child = NULL;
	temp->right_child = NULL;
	// *temp = { type, row, col, symbol_type, symbol_value, NULL, NULL, NULL };
	return temp;
}

	ASTNode *n_program(Parser *p);
	ASTNode *n_globals(Parser *p);
	ASTNode *n_initlist(Parser *p);
	ASTNode *n_init(Parser *p);
	ASTNode *n_funcs(Parser *p);
	ASTNode *n_mainbody(Parser *p);
	ASTNode *n_slist(Parser *p);
	ASTNode *n_typelist(Parser *p);
	ASTNode *n_type(Parser *p);
	ASTNode *n_fields(Parser *p);
	ASTNode *n_sdecl(Parser *p);
	ASTNode *n_arrdecls(Parser *p);
	ASTNode *n_arrdecl(Parser *p);
	ASTNode *n_func(Parser *p);
	ASTNode *n_plist(Parser *p);
	ASTNode *n_params(Parser *p);
	ASTNode *n_param(Parser *p);
	// ASTNode *n_funcbody(Parser *p);
	ASTNode *n_locals(Parser *p);
	ASTNode *n_dlist(Parser *p);
	ASTNode *n_decl(Parser *p);
	ASTNode *n_stats(Parser *p);
	ASTNode *n_stat(Parser *p);
	ASTNode *n_forstat(Parser *p);
	ASTNode *n_repstat(Parser *p);
	ASTNode *n_asgnlist(Parser *p);
	ASTNode *n_ifstat(Parser *p);
	ASTNode *n_asgnstat(Parser *p);
	ASTNode *n_iostat(Parser *p);
	ASTNode *n_callstat(Parser *p);
	ASTNode *n_returnstat(Parser *p);
	ASTNode *n_vlist(Parser *p);
	ASTNode *n_var(Parser *p);
	ASTNode *n_fncall(Parser *p);
	ASTNode *n_elist(Parser *p);
	ASTNode *n_bool(Parser *p);
	ASTNode *n_rel(Parser *p);
	ASTNode *n_relop(Parser *p);
	ASTNode *n_expr(Parser *p);
	ASTNode *n_term(Parser *p);
	ASTNode *n_fact(Parser *p);
	ASTNode *n_exponent(Parser *p);
	ASTNode *n_fncall(Parser *p);
	ASTNode *n_prlist(Parser *p);
	ASTNode *n_printitem(Parser *p);

jmp_buf error_close;
// parse the rest of the program, dropping the nodes as no compilation will proceed
void error_recovery(Parser *p) {
	p->in_recovery = 1;
	while (p->c->type != T_EOF) {
		p->fresh_error = 1;
		// printf("token: %s, state=%d\n", TPRINT[p->c->type], p->progress);
		switch (p->progress) {
			case 0: // CD25 <id>
			case 1: // consts
				switch (p->c->type) {
					// synchronise on , types, arrays, func, main
					case TCNST: // state 0
						match(p, TCNST);
						node_free(n_init(p));
						break;
					case TCOMA:
						match(p, TCOMA);
						node_free(n_init(p));
						break;
					case TTYPS:
						p->progress = 2;
						break;
					case TARRS:
						p->progress = 3;
						break;
					case TFUNC:
						p->progress = 5;
						break;
					case TMAIN:
						p->progress = 11;
						break;
					default:
						next_token(p, 1);
						break;
				}
				break;
			case 2: // types
				switch (p->c->type) {
					case TTYPS:
						match(p, TTYPS);
						node_free(n_type(p));
						break;
					case TIDEN:
						node_free(n_type(p));
						break;
					case TARRS:
						p->progress = 3;
						break;
					case TFUNC:
						p->progress = 5;
						break;
					case TMAIN:
						p->progress = 11;
						break;
					default:
						next_token(p, 1);
						break;
				}
				break;
			case 3: // arrays
				switch (p->c->type) {
					case TARRS:
						match(p, TARRS);
						node_free(n_arrdecl(p));
						break;
					case TCOMA:
						match(p, TCOMA);
						node_free(n_arrdecl(p));
						break;
					case TFUNC:
						p->progress = 5;
						break;
					case TMAIN:
						p->progress = 11;
						break;
					default:
						next_token(p, 1);
						break;
				}
				break;
			case 5: // funcs (4 was a mistake)
				switch (p->c->type) {
					case TFUNC:
						match(p, TFUNC);
						match(p, TIDEN);
						match(p, TLPAR);
						p->progress = 6;
						break;
					case TMAIN:
						p->progress = 11;
						break;
					default:
						next_token(p, 1);
						break;
				}
				break;
			case 6: // func params (sync on ')' | func | main)
				switch (p->c->type) {
					case TIDEN:
						node_free(n_param(p));
						if (p->c->type == TCOMA)
							match(p, TCOMA);
						break;
					case TRPAR:
						match(p, TRPAR);
						match(p, TCOLN);
						switch(p->c->type) {
							case TINTG:
							case TREAL:
							case TBOOL:
							case TVOID:
								next_token(p, 1);
								break;
							default:
								lister_syn_error(p->lst, p->c->row, p->c->col, "invalid return type");
								next_token(p, 1);
								break;
						}
						p->progress = 8;
						break;
					case TFUNC:
						p->progress = 5;
						break;
					case TMAIN:
						p->progress = 11;
						break;
					default:
						next_token(p, 1);
						break;
				}
				break;
			case 8: // funcbody (7 was a mistake)
				switch(p->c->type) {
					case TIDEN:
						node_free(n_decl(p));
						if (p->c->type == TCOMA)
							match(p, TCOMA);
						break;
					case TBEGN:
						match(p, TBEGN);
						p->progress = 9;
						node_free(n_stat(p));
						break;
					case TFUNC:
						p->progress = 5;
						break;
					case TMAIN:
						p->progress = 11;
						break;
					default:
						next_token(p, 1);
						break;
				}
				break;
			case 9: // stats [func]
				switch(p->c->type) {
					case TSEMI:
						match(p, TSEMI);
						if (p->c->type != TTEND)
							node_free(n_stat(p));
						break;
					case TTEND:
						match(p, TTEND);
						if (p->c->type == TFUNC)
							p->progress = 5;
						else if (p->c->type != TMAIN)
							node_free(n_stat(p));
						break;
					case TMAIN:
						p->progress = 11;
						break;
					default:
						next_token(p, 1);
						break;
				}
				break;
			case 11:
				switch(p->c->type) {
					case TMAIN:
						match(p, TMAIN);
						node_free(n_sdecl(p));
						break;
					case TCOMA:
						match(p, TCOMA);
						node_free(n_sdecl(p));
						break;
					case TBEGN:
						p->progress = 12;
						match(p, TBEGN);
						node_free(n_stat(p));
						break;
					// case TSEMI:
					// 	p->progress = 12;
					// 	break;
					default:
						next_token(p, 1);
						break;
				}
				break;
			case 12: // stats [main]
				switch(p->c->type) {
					case TSEMI:
						match(p, TSEMI);
						if (p->c->type == TTEND && p->n->type == TCD25) {
							match(p, TTEND);
							match(p, TCD25);
							match(p, TIDEN);
							break;
						}
						node_free(n_stat(p));
						break;
					case TTEND:
						match(p, TTEND);
						if (p->c->type == TCD25) {
							match(p, TCD25);
							match(p, TIDEN);
							break;
						}
						node_free(n_stat(p));
						break;
					default: // end CD25 <id> is not considered here
						next_token(p, 1);
						break;
				}
				break;
		}
	}
	longjmp(error_close, 1);
}

ASTree *get_AST(const char *filename, Lister *list_file) {
    Lexer *scanner = lexer_create(filename, list_file);
	ASTree *tree = astree_create(128);

	struct parser p = {
		scanner,
		lexer_get_token(scanner),
		lexer_get_token(scanner),
		tree,
		0, // scope
		list_file,
		0, // parsing progress (recovery)
		1, // fresh_error
		0, // in_recovery
		0 // offset
	};

	if (setjmp(error_close) == 0)
		tree->root = n_program(&p);
	else
		tree->root = NULL;

	lexer_free(scanner);
	free_token(p.c);
	free_token(p.n);
	return tree;
}

int unscoped_symbol_equals(const Symbol *s1, const Symbol *s2) {
	return s1->index.start == s2->index.start &&
	       s1->index.len   == s2->index.len;
}

ASTNode *n_program(Parser *p) {
	match(p, TCD25);
	Symbol *symbol = astree_add_symbol(p->ast, p->c->val, p->scope);
	ASTNode *prog = make_node(NPROG, p->c->row, p->c->col, SNONE, symbol);
	match(p, TIDEN);
	// use current when next production is already known, use next when it can branch
	if (p->c->type == TCONS || p->c->type == TTYPS || p->c->type == TARRS)
		prog->left_child = n_globals(p); // can be epsilon
	if (p->c->type == TFUNC)
		prog->middle_child = n_funcs(p); // can be epsilon
	prog->right_child = n_mainbody(p);
	match(p, T_EOF);
	return prog;
}

ASTNode *n_globals(Parser *p) {
	p->progress = 1; // synchronise on constants , types, arrays, func, main
	ASTNode *globs = make_node(NGLOB, p->c->row, p->c->col, SNONE, NULL);
	if (p->c->type == TCONS) {
		match(p, TCONS);
		globs->left_child = n_initlist(p);
	}
	if (p->c->type == TTYPS) {
		match(p, TTYPS);
		globs->middle_child = n_typelist(p);
	}
	if (p->c->type == TARRS) {
		match(p, TARRS);
		globs->right_child = n_arrdecls(p);
	}
	return globs;
}

ASTNode *n_initlist(Parser *p) {
	u16 row = p->c->row;
	u16 col = p->c->col;
	ASTNode *ninit = n_init(p);
	if (p->c->type != TCOMA)
		return ninit;
	match(p, TCOMA);
	ASTNode *initlist = make_node(NILIST, row, col, SNONE, NULL);
	initlist->left_child = ninit;
	initlist->right_child = n_initlist(p);
	return initlist;
}

ASTNode *n_init(Parser *p) {
	Symbol *symbol = astree_add_symbol(p->ast, p->c->val, p->scope);
	ASTNode *ninit = make_node(NINIT, p->c->row, p->c->col, SNONE, symbol);
	match(p, TIDEN);
	match(p, TTTIS);
	u16 row = p->c->row;
	u16 col = p->c->col;
	ninit->left_child = n_expr(p);
	// symboltable attribute is handled in semantic analysis
	return ninit;
}

ASTNode *n_funcs(Parser *p) {
	if (p->c->type != TFUNC)
		return NULL;
	ASTNode *nfuncs = make_node(NFUNCS, p->c->row, p->c->col, SNONE, NULL);
	nfuncs->left_child = n_func(p);
	nfuncs->right_child = n_funcs(p);
	return nfuncs;
}

ASTNode *n_mainbody(Parser *p) {
	p->scope++;
	u16 row = p->c->row;
	u16 col = p->c->row;
	match(p, TMAIN);
	p->progress = 11; // synchronise on iden
	ASTNode *slist = n_slist(p);
	p->progress = 12; // synchronise on ; | end
	match(p, TBEGN);
	ASTNode *stats = n_stats(p);
	match(p, TTEND);
	match(p, TCD25);
	Symbol *progname = astree_add_symbol(p->ast, p->c->val, p->scope);
	match(p, TIDEN);
	ASTNode *nmain = make_node(NMAIN, row, col, SNONE, progname);
	nmain->left_child = slist;
	nmain->right_child = stats;
	return nmain;
}

ASTNode *n_slist(Parser *p) {
	u16 row = p->c->row;
	u16 col = p->c->col;
	ASTNode *sdecl = n_sdecl(p);
	if (p->c->type != TCOMA) {
		return sdecl;
	}
	ASTNode *nsdlst = make_node(NSDLST, row, col, SNONE, NULL);
	nsdlst->left_child = sdecl;
	match(p, TCOMA);
	nsdlst->right_child = n_slist(p);
	return nsdlst;
}

ASTNode *n_typelist(Parser *p) {
	p->progress = 2;
	int row = p->c->row;
	int col = p->c->col;
	ASTNode *type = n_type(p);
	if (p->c->type == TIDEN) {
		ASTNode *ntypel = make_node(NTYPEL, row, col, SNONE, NULL);
		ntypel->left_child = type;
		ntypel->right_child = n_typelist(p);
		return ntypel;
	}
	return type;
}

ASTNode *n_type(Parser *p) {
	Symbol *name_symbol;
	int row = p->c->row;
	int col = p->c->col;
	name_symbol = astree_add_symbol(p->ast, p->c->val, p->scope);
	match(p, TIDEN);
	match(p, TTTIS);
	if (p->c->type == TIDEN) { // struct
		ASTNode *nrtype = make_node(NRTYPE, row, col, SNONE, name_symbol);
		nrtype->left_child = n_fields(p);
		if (astree_add_attribute(p->ast, name_symbol, struct_types_attribute(p, nrtype->left_child)))
			symbol_redefinition_error(p, p->c->row, p->c->col);
		match(p, TTEND);
		return nrtype;
	}
	else if (p->c->type == TARAY) { // array
		ASTNode *natype = make_node(NATYPE, row, col, SNONE, name_symbol);
		match(p, TARAY);
		match(p, TLBRK);
		natype->left_child = n_expr(p);
		match(p, TRBRK);
		match(p, TTTOF);
		Symbol *type_symbol = astree_add_symbol(p->ast, p->c->val, p->scope);
		if (astree_add_attribute(p->ast, name_symbol, astree_attribute_create(p->ast, SSTRUCT, type_symbol)))
			symbol_redefinition_error(p, p->c->row, p->c->col);
		match(p, TIDEN);
		match(p, TTEND);
		return natype;
	}
	return NULL;
}

ASTNode *n_fields(Parser *p) {
	ASTNode *nsdecl = n_sdecl(p);
	if (p->c->type == TCOMA) {
		ASTNode *nflist = make_node(NFLIST, p->c->row, p->c->col, SNONE, NULL);
		nflist->left_child = nsdecl;
		match(p, TCOMA);
		nflist->right_child = n_fields(p);
		return nflist;
	} else {
		return nsdecl;
	}
}

ASTNode *n_sdecl(Parser *p) {
	Symbol *symbol = astree_add_symbol(p->ast, p->c->val, p->scope);
	ASTNode *nsdecl = make_node(NSDECL, p->c->row, p->c->col, SNONE, symbol);
	match(p, TIDEN);
	match(p, TCOLN);
	switch (p->c->type) {
		case TINTG:
			if (p->scope != 0) // a scope 0 sdecl is a struct field
				// todo: in error recovery symbol definition false positive is flagged
				if (astree_add_attribute(p->ast, symbol, astree_attribute_create(p->ast, SINT, NULL)))
					symbol_redefinition_error(p, p->c->row, p->c->col);
			nsdecl->symbol_type = SINT;
			match(p, TINTG);
			break;
		case TREAL:
			if (p->scope != 0)
				if (astree_add_attribute(p->ast, symbol, astree_attribute_create(p->ast, SREAL, NULL)))
					symbol_redefinition_error(p, p->c->row, p->c->col);
			nsdecl->symbol_type = SREAL;
			match(p, TREAL);
			break;
		case TBOOL:
			if (p->scope != 0)
				if (astree_add_attribute(p->ast, symbol, astree_attribute_create(p->ast, SBOOL, NULL)))
					symbol_redefinition_error(p, p->c->row, p->c->col);
			nsdecl->symbol_type = SBOOL;
			match(p, TBOOL);
			break;
		default:
			p->ast->is_valid = 0;
			lister_syn_error(p->lst, p->c->row, p->c->col, "expected integer, real or boolean");
			error_recovery(p);
			break;
	}
	return nsdecl;
}

ASTNode *n_arrdecls(Parser *p) {
	p->progress = 3;
	int row = p->c->row;
	int col = p->c->col;
	ASTNode *narrd = n_arrdecl(p);
	if (p->c->type == TCOMA) {
		match(p, TCOMA);
		ASTNode *nalist = make_node(NALIST, row, col, SNONE, NULL);
		nalist->left_child = narrd;
		nalist->right_child = n_arrdecls(p);
		return nalist;
	}
	return narrd;
}

ASTNode *n_arrdecl(Parser *p) {
	int row = p->c->row;
	int col = p->c->col;
	Symbol *var_symbol = astree_add_symbol(p->ast, p->c->val, p->scope);
	match(p, TIDEN);
	match(p, TCOLN);
	Symbol *type_symbol = astree_add_symbol(p->ast, p->c->val, p->scope);
	match(p, TIDEN);
	if (!astree_get_attribute(p->ast, var_symbol)) {
		astree_add_attribute(p->ast, var_symbol, astree_attribute_create(p->ast, SARRAY, type_symbol));
	} else {
		p->ast->is_valid = 0;
		lister_sem_error(p->lst, p->c->row, p->c->col, "global array name has a collision");
	}
	return make_node(NARRD, row, col, SARRAY, var_symbol);
}

ASTNode *n_func(Parser *p) {
	p->progress = 5; // synchronise on TLPAR, ; , end
	p->scope++;
	ASTNode *nfund = make_node(NFUND, p->c->row, p->c->col, SNONE, NULL);
	match(p, TFUNC);
	Symbol *fname = astree_add_symbol(p->ast, p->c->val, 0); // functions are global scope
	nfund->symbol_value = fname;
	match(p, TIDEN);
	match(p, TLPAR);
	p->progress = 6;
	nfund->left_child = n_plist(p);
	match(p, TRPAR);
	match(p, TCOLN);
	enum symbol_type ret_type;
	switch (p->c->type) {
		case TINTG:
			ret_type = SINT;
			match(p, TINTG);
			break;
		case TREAL:
			ret_type = SREAL;
			match(p, TREAL);
			break;
		case TBOOL:
			ret_type = SBOOL;
			match(p, TBOOL);
			break;
		case TVOID:
			ret_type = SVOID;
			match(p, TVOID);
			break;
		default:
			p->ast->is_valid = 0;
			lister_syn_error(p->lst, p->c->row, p->c->col, "invalid return type");
			p->progress = 8;
			error_recovery(p);
	}
	p->progress = 8;
	nfund->middle_child = n_locals(p);
	match(p, TBEGN);
	p->progress = 9;
	nfund->right_child = n_stats(p);
	match(p, TTEND);
	if (astree_add_attribute(p->ast, fname, make_func_attribute(p, nfund->left_child, ret_type)))
		symbol_redefinition_error(p, p->c->row, p->c->col);
	return nfund;
}

ASTNode *n_plist(Parser *p) {
	if (p->c->type == TRPAR)
		return NULL;
	return n_params(p);
}

ASTNode *n_params(Parser *p) {
	u16 row = p->c->row;
	u16 col = p->c->col;
	ASTNode *left = n_param(p);
	if (p->c->type != TCOMA)
		return left;
	match(p, TCOMA);
	ASTNode *nplist = make_node(NPLIST, row, col, SNONE, NULL);
	nplist->left_child = left;
	nplist->right_child = n_params(p);
	return nplist;
}

ASTNode *n_param(Parser *p) {
	if (p->c->type == TCNST) {
		ASTNode *narrc = make_node(NARRC, p->c->row, p->c->col, SNONE, NULL);
		match(p, TCNST);
		narrc->left_child = n_arrdecl(p);
		return narrc;
	}
	return n_decl(p);
}

ASTNode *n_locals(Parser *p) {
	if (p->c->type != TIDEN)
		return NULL;
	return n_dlist(p);
}

ASTNode *n_dlist(Parser *p) {
	u16 row = p->c->row;
	u16 col = p->c->col;
	ASTNode *left = n_decl(p);
	if (p->c->type != TCOMA)
		return left;
	match(p, TCOMA);
	ASTNode *ndlist = make_node(NDLIST, row, col, SNONE, NULL);
	ndlist->left_child = left;
	ndlist->right_child = n_dlist(p);
	return ndlist;
}

ASTNode *n_decl(Parser *p) {
	// sadly you can't call n_sdecl or n_arrdecl without left factoring because that'd require a second lookahead
	// although the symbol table operations are different too
	int row = p->c->row;
	int col = p->c->col;
	Symbol *var_symbol = astree_add_symbol(p->ast, p->c->val, p->scope);
	match(p, TIDEN);
	match(p, TCOLN);
	if (p->c->type == TIDEN) { // array
		Symbol *type_symbol = astree_get_symbol(p->ast, p->c->val, 0); // array defs are global scope
		if (astree_add_attribute(p->ast, var_symbol, astree_attribute_create(p->ast, SARRAY, type_symbol)))
			symbol_redefinition_error(p, p->c->row, p->c->col);
		match(p, TIDEN);
		return make_node(NARRD, row, col, SARRAY, var_symbol);
	} else { // primitive
		switch (p->c->type) {
			case TINTG:
				if (astree_add_attribute(p->ast, var_symbol, astree_attribute_create(p->ast, SINT, NULL)))
					symbol_redefinition_error(p, p->c->row, p->c->col);
				match(p, TINTG);
				return make_node(NSDECL, row, col, SINT, var_symbol);
			case TREAL:
				if (astree_add_attribute(p->ast, var_symbol, astree_attribute_create(p->ast, SREAL, NULL)))
					symbol_redefinition_error(p, p->c->row, p->c->col);
				match(p, TREAL);
				return make_node(NSDECL, row, col, SREAL, var_symbol);
			case TBOOL:
				if (astree_add_attribute(p->ast, var_symbol, astree_attribute_create(p->ast, SBOOL, NULL)))
					symbol_redefinition_error(p, p->c->row, p->c->col);
				match(p, TBOOL);
				return make_node(NSDECL, row, col, SBOOL, var_symbol);
			default:
				lister_syn_error(p->lst, p->c->row, p->c->col, "unknown primitive type");
				error_recovery(p);
		}
	}
}

ASTNode *n_stats(Parser *p) {
	ASTNode *nstats = make_node(NSTATS, p->c->row, p->c->col, SNONE, NULL);
	nstats->left_child = n_stat(p);
	switch (p->c->type) {
		case TTFOR: case TIFTH: case TREPT: case TIDEN: case TINPT: case TOUTP: case TRETN:
			nstats->right_child = n_stats(p);
			break;
	}
	return nstats;
}

ASTNode *n_stat(Parser *p) {
	ASTNode *result;
	switch (p->c->type) {
		// strstat
		case TTFOR:
			result = n_forstat(p);
			return result;
		case TIFTH:
			result = n_ifstat(p);
			return result;
		// stat
		case TREPT:
			result = n_repstat(p);
			match(p, TSEMI);
			return result;
		case TINPT:
		case TOUTP:
			result = n_iostat(p);
			match(p, TSEMI);
			return result;
		case TRETN:
			result = n_returnstat(p);
			match(p, TSEMI);
			return result;
		case TIDEN:
			if (p->n->type == TLPAR) {
				result = n_callstat(p);
			} else {
				result = n_asgnstat(p);
			}
			match(p, TSEMI);
			return result;
		default:
			p->ast->is_valid = 0;
			lister_syn_error(p->lst, p->c->row, p->c->col, "expected statement");
			error_recovery(p);
			return result;
	}
}

ASTNode *n_forstat(Parser *p) {
	ASTNode *nforl = make_node(NFORL, p->c->row, p->c->col, SNONE, NULL);
	match(p, TTFOR);
	match(p, TLPAR);
	nforl->left_child = n_asgnlist(p);
	match(p, TSEMI);
	nforl->middle_child = n_bool(p);
	match(p, TRPAR);
	nforl->right_child = n_stats(p);
	match(p, TTEND);
	return nforl;
}

ASTNode* n_repstat(Parser *p) {
	ASTNode *nrept = make_node(NREPT, p->c->row, p->c->col, SNONE, NULL);
	match(p, TREPT);
	match(p, TLPAR);
	nrept->left_child = n_asgnlist(p);
	match(p, TRPAR);
	nrept->middle_child = n_stats(p);
	match(p, TUNTL);
	nrept->right_child = n_bool(p);
	return nrept;
}

ASTNode *n_asgnlist(Parser *p) {
	if (p->c->type != TIDEN)
		return NULL; // ε
	u16 row = p->c->row;
	u16 col = p->c->col;
	ASTNode *asgnstat = n_asgnstat(p);
	if (p->c->type != TCOMA) {
		return asgnstat;
	} else {
		ASTNode *nasgns = make_node(NASGNS, p->c->row, p->c->col, SNONE, NULL);
		nasgns->left_child = asgnstat;
		match(p, TCOMA);
		nasgns->right_child = n_asgnlist(p);
		return nasgns;
	}
}

ASTNode *n_ifstat(Parser *p) {
	u16 row = p->c->row;
	u16 col = p->c->col;
	match(p, TIFTH);
	match(p, TLPAR);
	ASTNode *predicate = n_bool(p);
	match(p, TRPAR);
	ASTNode *stats0 = n_stats(p);
	ASTNode *stats1 = NULL;
	if (p->c->type == TELSE) {
		match(p, TELSE);
		stats1 = n_stats(p);
	}
	match(p, TTEND);
	if (stats1) {
		ASTNode *nifte = make_node(NIFTE, row, col, SNONE, NULL);
		nifte->left_child = predicate;
		nifte->middle_child = stats0;
		nifte->right_child = stats1;
		return nifte;
	} else {
		ASTNode *nifth = make_node(NIFTH, row, col, SNONE, NULL);
		nifth->left_child = predicate;
		nifth->right_child = stats0;
		return nifth;
	}
}

ASTNode *n_asgnstat(Parser *p) {
	u16 row = p->c->row;
	u16 col = p->c->col;
	ASTNode *var = n_var(p);
	enum node_type type;
	switch (p->c->type) {
		case TEQUL:
			type = NASGN;
			break;
		case TPLEQ:
			type = NPLEQ;
			break;
		case TMNEQ:
			type = NMNEQ;
			break;
		case TSTEQ:
			type = NSTEA;
			break;
		case TDVEQ:
			type = NDVEQ;
			break;
		default:
			p->ast->is_valid = 0;
			lister_syn_error(p->lst, p->c->row, p->c->col, "expected asignment operator");
			error_recovery(p);
			break;
	}
	ASTNode *oper = make_node(type, row, col, SNONE, NULL);
	oper->left_child = var;
	next_token(p, 1);
	oper->right_child = n_bool(p);
	return oper;
}

ASTNode *n_iostat(Parser *p) {
	if (p->c->type == TINPT) {
		ASTNode *ninput = make_node(NINPUT, p->c->row, p->c->col, SNONE, NULL);
		match(p, TINPT);
		match(p, TGRGR);
		ninput->left_child = n_vlist(p);
		return ninput;
	} else {
		u16 row = p->c->row;
		u16 col = p->c->col;
		match(p, TOUTP);
		match(p, TLSLS);
		if (p->c->type == TOUTL) {
			match(p, TOUTL);
			return make_node(NOUTL, row, col, SNONE, NULL);
		}
		ASTNode *prlist = n_prlist(p);
		if (p->c->type != TLSLS) {
			ASTNode *noutp = make_node(NOUTP, row, col, SNONE, NULL);
			noutp->left_child = prlist;
			return noutp;
		} else {
			ASTNode *noutl = make_node(NOUTL, row, col, SNONE, NULL);
			noutl->left_child = prlist;
			match(p, TLSLS);
			match(p, TOUTL);
			return noutl;
		}
	}
}

ASTNode *n_callstat(Parser *p) {
	Symbol *iden = astree_add_symbol(p->ast, p->c->val, p->scope);
	ASTNode *ncall = make_node(NCALL, p->c->row, p->c->col, SNONE, iden);
	match(p, TIDEN);
	match(p, TLPAR);
	if (p->c->type != TRPAR) {
		ncall->left_child = n_elist(p);
	}
	match(p, TRPAR);
	return ncall;
}

ASTNode *n_returnstat(Parser *p) {
	ASTNode *nretn = make_node(NRETN, p->c->row, p->c->col, SNONE, NULL);
	match(p, TRETN);
	if (p->c->type == TVOID) {
		match(p, TVOID);
		return nretn;
	} else {
		nretn->left_child = n_expr(p);
		return nretn;
	}
}

ASTNode *n_vlist(Parser *p) {
	u16 row = p->c->row;
	u16 col = p->c->col;
	ASTNode *var = n_var(p);
	if (p->c->type != TCOMA) {
		return var;
	}
	ASTNode *nvlist = make_node(NVLIST, row, col, SNONE, NULL);
	nvlist->left_child = var;
	match(p, TCOMA);
	nvlist->right_child = n_vlist(p);
	return nvlist;
}

ASTNode *n_var(Parser *p) {
	Symbol *varname = astree_add_symbol(p->ast, p->c->val, p->scope);
	ASTNode *nsimv = make_node(NSIMV, p->c->row, p->c->col, SNONE, varname);
	if (p->n->type != TLBRK) {
		match(p, TIDEN);
		return nsimv;
	}
	u16 row = p->c->row;
	u16 col = p->c->col;
	match(p, TIDEN);
	match(p, TLBRK);
	ASTNode *arr_index = n_expr(p);
	match(p, TRBRK);
	if (p->c->type != TDOTT) {
		ASTNode *naelt = make_node(NAELT, row, col, SSTRUCT, NULL);
		naelt->left_child = nsimv;
		naelt->right_child = arr_index;
		return naelt;
	} else {
		ASTNode *narrv = make_node(NARRV, row, col, SNONE, NULL); // the value is later in parsing
		narrv->left_child = nsimv;
		narrv->right_child = arr_index;
		match(p, TDOTT);
		Symbol *field_name = astree_add_symbol(p->ast, p->c->val, p->scope);
		narrv->symbol_value = field_name;
		match(p, TIDEN);
		return narrv;
	}
}

ASTNode *n_elist(Parser *p) {
	ASTNode *nexpl = make_node(NEXPL, p->c->row, p->c->col, SNONE, NULL);
	nexpl->left_child = n_bool(p);
	if (p->c->type == TCOMA) {
		match(p, TCOMA);
		nexpl->right_child = n_elist(p);
	}
	return nexpl;
}

ASTNode *n_bool(Parser *p) {
	u16 row = p->c->row;
	u16 col = p->c->col;
	ASTNode *left = n_rel(p);
	ASTNode *operator;
	switch (p->c->type) {
		case TTAND:
			operator = make_node(NAND, p->c->row, p->c->col, SBOOL, NULL);
			match(p, TTAND);
			break;
		case TTTOR:
			operator = make_node(NOR, p->c->row, p->c->col, SBOOL, NULL);
			match(p, TTTOR);
			break;
		case TTXOR:
			operator = make_node(NXOR, p->c->row, p->c->col, SBOOL, NULL);
			match(p, TTXOR);
			break;
		default:
			return left;
	}
	ASTNode *nbool = make_node(NBOOL, row, col, SBOOL, NULL);
	nbool->left_child = left;
	nbool->middle_child = operator;
	nbool->right_child = n_bool(p);
	return nbool;
}

ASTNode *n_rel(Parser *p) {
	if (p->c->type == TNOTT) {
		// todo: move all such types into semantic analysis
		ASTNode *nnot = make_node(NNOT, p->c->row, p->c->col, SBOOL, NULL);
		match(p, TNOTT);
		nnot->left_child = n_expr(p);
		nnot->middle_child = n_relop(p);
		nnot->right_child = n_expr(p);
		return nnot;
	}
	ASTNode *left = n_expr(p);
	enum token_type ct = p->c->type;
	if (ct == TEQEQ || ct == TNEQL || ct == TGRTR || ct == TLESS || ct == TLEQL || ct == TGEQL) {
		ASTNode *relop = n_relop(p);
		ASTNode *right = n_rel(p);
		relop->left_child = left;
		relop->right_child = right;
		return relop;
	}
	else {
		return left;
	}
}

ASTNode *n_relop(Parser *p) {
	int node_type = -1;
	switch(p->c->type) {
		case TEQEQ:
			node_type = NEQL;
			break;
		case TNEQL:
			node_type = NNEQ;
			break;
		case TGRTR:
			node_type = NGRT;
			break;
		case TLESS:
			node_type = NLSS;
			break;
		case TLEQL:
			node_type = NLEQ;
			break;
		case TGEQL:
			node_type = NGEQ;
			break;
	}
	if (node_type != -1) {
		ASTNode *out = make_node(node_type, p->c->row, p->c->col, SBOOL, NULL);
		next_token(p, 1);
		return out;
	}
	p->ast->is_valid = 0;
	lister_syn_error(p->lst, p->c->row, p->c->col, "unexpected relational operator");
	error_recovery(p);
	return NULL;
}

static ASTNode *expr_helper(Parser *p, int is_root_of_fact);
static ASTNode *term_helper(Parser *p, int is_root_of_fact);
static ASTNode *fact_helper(Parser *p, int is_root_of_fact);
// a fun recursive algorithm to turn a recursive descent expr tree valid
// swaps the right child's left child with the current node, then steps to the former right child
static ASTNode *invert_expr(ASTNode *a) {
	ASTNode *b = a->right_child;
	if (b->type != NADD && b->type != NSUB)
		return a; // terminate when right is leaf
	ASTNode *temp = b->left_child;
	b->left_child = a;
	a->right_child = temp;
	return invert_expr(b);
}

// this used to be n_expr until I needed to differentiate first call for the inversion to be done only once on the final tree
static ASTNode *expr_helper(Parser *p, int is_root_of_expr) {
	ASTNode *left = term_helper(p, 0);
	switch (p->c->type) {
		case TPLUS:
			ASTNode *nadd = make_node(NADD, p->c->row, p->c->col, SNONE, NULL);
			match(p, TPLUS);
			nadd->left_child = left;
			// the grammar says term, I have intentionally disregarded it here to extend for 3-1-1-1 to be valid syntax
			nadd->right_child = expr_helper(p, 0);
			if (is_root_of_expr)
				nadd = invert_expr(nadd);
			return nadd;
		case TMINS:
			ASTNode *nsub = make_node(NSUB, p->c->row, p->c->col, SNONE, NULL);
			match(p, TMINS);
			nsub->left_child = left;
			nsub->right_child = expr_helper(p, 0);
			if (is_root_of_expr)
				nsub = invert_expr(nsub); // note: "nsub" may be a nadd after this
			return nsub;
		default:
			return left;
	}
}

ASTNode *n_expr(Parser *p) {
	return expr_helper(p, 1);
	// return invert_expr(expr_helper(p)); this approach leads to segfault, idk why
}

static ASTNode *invert_term(ASTNode *a) {
	ASTNode *b = a->right_child;
	if (b->type != NMUL && b->type != NDIV && b->type != NMOD)
		return a; // terminate when right is leaf
	ASTNode *temp = b->left_child;
	b->left_child = a;
	a->right_child = temp;
	return invert_term(b);
}

static ASTNode *term_helper(Parser *p, int is_root_of_term) {
	ASTNode *left = fact_helper(p, 0);
	switch (p->c->type) {
		case TSTAR:
			ASTNode *nmul = make_node(NMUL, p->c->row, p->c->col, SNONE, NULL);
			match(p, TSTAR);
			nmul->left_child = left;
			nmul->right_child = term_helper(p, 0);
			if (is_root_of_term)
				nmul = invert_term(nmul);
			return nmul;
		case TDIVD:
			ASTNode *ndiv = make_node(NDIV, p->c->row, p->c->col, SNONE, NULL);
			match(p, TDIVD);
			ndiv->left_child = left;
			ndiv->right_child = term_helper(p, 0);
			if (is_root_of_term)
				ndiv = invert_term(ndiv);
			return ndiv;
		case TPERC:
			ASTNode *nmod = make_node(NMOD, p->c->row, p->c->col, SNONE, NULL);
			match(p, TPERC);
			nmod->left_child = left;
			nmod->right_child = term_helper(p, 0);
			if (is_root_of_term)
				nmod = invert_term(nmod);
			return nmod;
		default: //ε
			return left;
	}
}

ASTNode *n_term(Parser *p) {
	return term_helper(p, 1);
}

static ASTNode *invert_fact(ASTNode *a) {
	ASTNode *b = a->right_child;
	if (b->type != NPOW)
		return a; // terminate when right is leaf
	ASTNode *temp = b->left_child;
	b->left_child = a;
	a->right_child = temp;
	return invert_fact(b);
}

static ASTNode *fact_helper(Parser *p, int is_root_of_fact) {
	ASTNode *left = n_exponent(p);
	switch (p->c->type) {
		case TCART:
			ASTNode *npow = make_node(NPOW, p->c->row, p->c->col, SNONE, NULL);
			match(p, TCART);
			npow->left_child = left;
			npow->right_child = fact_helper(p, 0);
			if (is_root_of_fact)
				npow = invert_fact(npow);
			return npow;
		default:
			return left;
	}
}

ASTNode *n_fact(Parser *p) {
	return fact_helper(p, 1);
}

ASTNode *n_exponent(Parser *p) {
	Symbol *symbol;
	switch (p->c->type) {
		case TILIT:
			symbol = astree_add_symbol(p->ast, p->c->val, p->scope);
			ASTNode *nilit = make_node(NILIT, p->c->row, p->c->col, SINT, symbol);
			match(p, TILIT);
			return nilit;
		case TFLIT:
			symbol = astree_add_symbol(p->ast, p->c->val, p->scope);
			ASTNode *nflit = make_node(NFLIT, p->c->row, p->c->col, SREAL, symbol);
			match(p, TFLIT);
			return nflit;
		case TTRUE:
			ASTNode *ntrue = make_node(NTRUE, p->c->row, p->c->col, SBOOL, NULL);
			match(p, TTRUE);
			return ntrue;
		case TFALS:
			ASTNode *nfals = make_node(NFALS, p->c->row, p->c->col, SBOOL, NULL);
			match(p, TFALS);
			return nfals;
		case TLPAR:
			match(p, TLPAR);
			ASTNode *nbool = n_bool(p); // this might need to be bool_helper, but it seems to work
			match(p, TRPAR);
			return nbool;
		case TIDEN:
			if (p->n->type == TLPAR) { // function
				return n_fncall(p);
			}
			else { // id[
				return n_var(p);
			}
		default:
			p->ast->is_valid = 0;
			lister_syn_error(p->lst, p->c->row, p->c->col, "unexpected exponent value");
			next_token(p, 1);
			error_recovery(p);
			return NULL;
	}
}

ASTNode *n_fncall(Parser *p) {
	u16 row = p->c->row;
	u16 col = p->c->col;
	Symbol *funcname = astree_add_symbol(p->ast, p->c->val, p->scope);
	ASTNode *nfcall = make_node(NFCALL, p->c->row, p->c->col, SNONE, funcname);
	match(p, TIDEN);
	match(p, TLPAR);
	if (p->c->type != TRPAR)
		nfcall->left_child = n_elist(p);
	match(p, TRPAR);
	return nfcall;
}

ASTNode *n_prlist(Parser *p) {
	u16 row = p->c->row;
	u16 col = p->c->col;
	ASTNode *pritem = n_printitem(p);
	if (p->c->type != TCOMA) {
		return pritem;
	}
	ASTNode *nprlst = make_node(NPRLST, row, col, SNONE, NULL);
	nprlst->left_child = pritem;
	match(p, TCOMA);
	nprlst->right_child = n_prlist(p);
	return nprlst;
}

 ASTNode *n_printitem(Parser *p) {
	if (p->c->type == TSTRG) {
		Symbol *str_val = astree_add_symbol(p->ast, p->c->val, p->scope);
		ASTNode *nstrg = make_node(NSTRG, p->c->row, p->c->col, SSTRING, str_val);
		match(p, TSTRG);
		return nstrg;
	} else {
		return n_expr(p);
	}
}

