// vimargs ../programs/valid3_softmax.cd

#include "lister.h"
#include "astree.h"
#include "node.h"
#include "semantic_analysis.h"

/* TODOS:
	semantic warning on redefinition away from global definition
  function array const checking
  non-numerics in math expressions
  logop checking
*/

// advance declarations

// semantic analysis helper functions
// equality for symbols, ignoring scope
static int unscoped_symbol_equals(const Symbol *s1, const Symbol *s2) {
	return s1->index.start == s2->index.start &&
	       s1->index.len   == s2->index.len;
}

// scans the expression for any function calls or array references
int is_compiletime_expr(ASTNode *node) {
	if (!node) return 1;
	if (node->type == NFCALL || node->type == NARRV || node->type == NAELT)
		return 0;
	return (
		is_compiletime_expr(node->left_child) &&
		is_compiletime_expr(node->middle_child) &&
		is_compiletime_expr(node->right_child)
	);
}

// for array size
int consts_ints_only_expr(ASTree *ast, ASTNode *node) {
	if (!node) return 1;
	// if leaf, check if NILIT or integer constant
	if (!(node->left_child || node->middle_child || node->right_child)) {
		if (node->type == NILIT)
			return 1;
		if (node->type == NSIMV) {
			Symbol *name = node->symbol_value;
			name->scope = 0;
			Attribute *atr = astree_get_attribute(ast, name);
			if (!atr || atr->type != SINT)
				return 0;
			else
				return 1;
		}
		return 0;
	}
	if (node->type == NFCALL || node->type == NARRV || node->type == NAELT)
		return 0;
	return (
		consts_ints_only_expr(ast, node->left_child) &&
		consts_ints_only_expr(ast, node->middle_child) &&
		consts_ints_only_expr(ast, node->right_child)
	);
}

// scans function for return calls
int has_return(ASTree *ast, ASTNode *node) {
	if (!node) return 0;
	if (node->type == NRETN)
		return 1;
	return has_return(ast, node->left_child) || has_return(ast, node->middle_child) || has_return(ast, node->right_child);
}

// update_<node> sets the type of <node> using its children as reference
void update_nfcall(ASTree *ast, ASTNode *node) {
	Attribute *atr = astree_get_attribute(ast, node->symbol_value);
	node->symbol_type = atr ? atr->type : SERROR;
}

void update_nsimv(ASTree *ast, ASTNode *node) {
	Attribute *atr = astree_get_attribute(ast, node->symbol_value);
	if (atr) {
		node->symbol_type = atr->type;
		// if (atr->type == SARRAY || atr->type == SSTRUCT) {
		// 	node->symbol_value = atr->data;
		// }
	} else {
		node->symbol_type = SERROR; // correctness and errors are handled elsewhere
	}
}

void update_naelt(ASTree *ast, ASTNode *node) {
	node->symbol_type = SSTRUCT;
	Attribute *arr_atr = astree_get_attribute(ast, node->left_child->symbol_value);
	if (!arr_atr || arr_atr->type != SARRAY) {
		node->symbol_type = SERROR;
		return;
	}
	node->symbol_value = arr_atr->data;
}

void update_narrv(ASTree *ast, ASTNode *node) {
	Symbol *array_name = node->left_child->symbol_value;
	Attribute *array_atr = astree_get_attribute(ast, array_name);
	if (!array_atr || array_atr->type != SARRAY) {
		node->symbol_type = SERROR;
		return;
	}
	Attribute *struct_atr = astree_get_attribute(ast, array_atr->data);
	if (!struct_atr || struct_atr->type != SSTRUCT) {
		node->symbol_type = SERROR;
		return;
	}
	Attribute *struct_atr1 = astree_get_attribute(ast, struct_atr->data);
	if (!struct_atr1 || struct_atr1->type != SFIELDS) {
		node->symbol_type = SERROR;
		return;
	}
	LinkedList *elements = (LinkedList *)struct_atr1->data;
	linkedlist_start(elements);
	Element *cur;
	while (cur = linkedlist_get_current(elements)) {
		if (unscoped_symbol_equals(cur->name, node->symbol_value)) {
			break; // found the relevant field
		}
		linkedlist_forward(elements);
	}
	if (cur) {
		node->symbol_type = cur->type;
	} else {
		// ast->is_valid = 0;
		// lister_sem_error(lst, node->row, node->col, "struct field doesn't exist");
		node->symbol_type = SERROR;
	}
}

void update_addsubmuldiv(ASTree *ast, ASTNode *node) {
	enum symbol_type left = node->left_child->symbol_type;
	enum symbol_type right = node->right_child->symbol_type;
	if ((left != SINT && left != SREAL) || (right != SINT && right != SREAL)) {
		node->symbol_type = SERROR;
		return;
	}
	if (left == SREAL || right == SREAL)
		node->symbol_type = SREAL;
	else
		node->symbol_type = SINT;
}

void update_mod(ASTree *ast, ASTNode *node) {
	enum symbol_type left = node->left_child->symbol_type;
	enum symbol_type right = node->right_child->symbol_type;
	if (left != SINT || right != SINT) {
		node->symbol_type = SERROR;
		return;
	}
	node->symbol_type = SINT;
}

void update_pow(ASTree *ast, ASTNode *node) {
	enum symbol_type left = node->left_child->symbol_type;
	enum symbol_type right = node->right_child->symbol_type;
	if ((left != SINT && left != SREAL) || (right != SINT && right != SREAL)) {
		node->symbol_type = SERROR;
		return;
	}
	if (left == SINT && right == SINT)
		node->symbol_type = SINT;
	else
		node->symbol_type = SREAL;
}

void update_node_symboltype(ASTree *ast, ASTNode *node, Lister *lst) {
	if (!node)
		return;
	update_node_symboltype(ast, node->left_child, lst);
	update_node_symboltype(ast, node->middle_child, lst);
	update_node_symboltype(ast, node->right_child, lst);
	switch (node->type) {
		case NINIT:
			if (astree_add_attribute(ast, node->symbol_value, astree_attribute_create(ast, node->left_child->symbol_type, NULL))) {
				ast->is_valid = 0;
				lister_sem_error(lst, node->row, node->col, "redefinition of variable already defined in scope");
			}
			break;
		case NADD:
		case NSUB:
		case NMUL:
		case NDIV:
			update_addsubmuldiv(ast, node);
			break;
		case NMOD:
			update_mod(ast, node);
			break;
		case NPOW:
			update_pow(ast, node);
			break;
		case NFCALL:
			update_nfcall(ast, node);
			break;
		case NSIMV:
			update_nsimv(ast, node);
			break;
		case NAELT:
			update_naelt(ast, node);
			break;
		case NARRV:
			update_narrv(ast, node);
			break;
	}
}

void compare_arg(ASTree *ast, Attribute *cur_formal, ASTNode *cur_arg, Lister *lst) {
	switch(cur_formal->type) {
		case SREAL:
			if (cur_arg->symbol_type != SREAL && cur_arg->symbol_type != SINT) {
				if (cur_arg->symbol_type != SERROR) {
					ast->is_valid = 0; // type mismatch
					lister_sem_error(lst, cur_arg->row, cur_arg->col, "expected real argument");
				}
			}
			break;
		case SINT:
			if (cur_arg->symbol_type != SINT) {
				if (cur_arg->symbol_type != SERROR) {
					ast->is_valid = 0; // type mismatch
					lister_sem_error(lst, cur_arg->row, cur_arg->col, "expected integer argument");
				}
			}
			break;
		case SBOOL:
			if (cur_arg->symbol_type != SBOOL) {
				if (cur_arg->symbol_type != SERROR) {
					ast->is_valid = 0; // type mismatch
					lister_sem_error(lst, cur_arg->row, cur_arg->col, "expected boolean argument");
				}
			}
			break;
		case SARRAY:
			if (cur_arg->symbol_type != SARRAY) {
				if (cur_arg->symbol_type != SERROR) {
					ast->is_valid = 0; // type mismatch
					lister_sem_error(lst, cur_arg->row, cur_arg->col, "expected array argument");
				}
				break;
			}
			// todo: this is unscoped, so if redefinition happens, it can mess this up
			Attribute *cur_arg_atr = astree_get_attribute(ast, cur_arg->symbol_value);
			if (!unscoped_symbol_equals(cur_formal->data, cur_arg_atr->data)) {
				ast->is_valid = 0; // type mismatch
				lister_sem_error(lst, cur_arg->row, cur_arg->col, "arrays are different types");
			}
			break;
	}
}
		// if (cur->type != cur_formal->type) {
		// 	ast->is_valid = 0;
		// 	lister_sem_error(lst, node->row, node->col, "struct field is not of expected type");
		// }

void analyse_function_params(ASTree *ast, ASTNode *node, Lister *lst, Attribute *fnatr) {
	LinkedList *formalparams = fnatr->data;
	linkedlist_start(formalparams);
	Attribute *cur_formal;
	int invalid = 0;
	ASTNode *funcargs = node->left_child;
	while (cur_formal = (Attribute *)linkedlist_get_current(formalparams)) {
		if (!funcargs) {
			ast->is_valid = 0;
			lister_sem_error(lst, node->row, node->col, "too few function arguments");
			break;
		}
		if (funcargs->type == NEXPL) {
			compare_arg(ast, cur_formal, funcargs->left_child, lst);
		}
		linkedlist_forward(formalparams);
		funcargs = funcargs->right_child;
	}
	if (funcargs) {
		ast->is_valid = 0;
		lister_sem_error(lst, node->row, node->col, "too many function arguments");
	}
}

void analyse_ncall(ASTree *ast, ASTNode *node, Lister *lst) {
	Symbol *funcname = node->symbol_value;
	Attribute *fnatr = astree_get_attribute(ast, funcname);
	if (fnatr && fnatr->type != SVOID) {
		ast->is_valid = 0;
		lister_sem_error(lst, node->row, node->col, "non-void function is not a statement");
		return;
	}
	analyse_function_params(ast, node, lst, fnatr);
}

void analyse_fncall(ASTree *ast, ASTNode *node, Lister *lst) {
	Symbol *funcname = node->symbol_value;
	Attribute *fnatr = astree_get_attribute(ast, funcname);
	if (fnatr && fnatr->type == SVOID) {
		ast->is_valid = 0;
		lister_sem_error(lst, node->row, node->col, "void function is not an expression");
		return; // todo: recover from this to check parameters? but sounds irrelevant
	}
	if (!fnatr || !fnatr->data || (fnatr->type != SREAL && fnatr->type != SBOOL && fnatr->type != SINT)) { // if S_<rettype> and data it is a function
		ast->is_valid = 0;
		lister_sem_error(lst, node->row, node->col, "called a function that doesn't exist");
		return;
	}
	analyse_function_params(ast, node, lst, fnatr);
}

void analyse_nsimv(ASTree *ast, ASTNode *node, Lister *lst) {
	Attribute *atr = astree_get_attribute(ast, node->symbol_value);
	if (!atr) {
		ast->is_valid = 0;
		lister_sem_error(lst, node->row, node->col, "undeclared variable");
		return;
	}
	if (atr->type != SSTRUCT && atr->type != SARRAY && atr->data) {
		ast->is_valid = 0;
		lister_sem_error(lst, node->row, node->col, "name does not refer to a variable");
	}
}

void analyse_init(ASTree *ast, ASTNode *node, Lister *lst) {
		// todo: get type from the analysed expression and save that to symbol table for this const
	if (!is_compiletime_expr(node->left_child)) {
		ast->is_valid = 0;
		lister_sem_error(lst, node->left_child->row, node->left_child->col, "constant value is not known at compile time");
	}
}

void analyse_natype(ASTree *ast, ASTNode *node, Lister *lst) {
	if (!consts_ints_only_expr(ast, node->left_child)) {
		ast->is_valid = 0;
		lister_sem_error(lst, node->row, node->col, "array size contains variables or non-integers");
	}
}

void analyse_arrdecl(ASTree *ast, ASTNode *node, Lister *lst) {
	Symbol *arrname = node->symbol_value;
	Attribute *arratr = astree_get_attribute(ast, arrname);
	if (!arratr || arratr->type != SARRAY) {
		//if () //todo: clean up a cascading error here
		ast->is_valid = 0;
		lister_sem_error(lst, node->row, node->col, "the array type doesn't exist");
		return;
	}
	Attribute *arrtype = astree_get_attribute(ast, (Symbol *)arratr->data);
	if (!arrtype || arrtype->type != SSTRUCT) {
		ast->is_valid = 0;
		lister_sem_error(lst, node->row, node->col, "the array type doesn't have a struct?");
		return;
	}
}

void analyse_function(ASTree *ast, ASTNode *node, Lister *lst) {
	if (!has_return(ast, node->right_child)) {
		ast->is_valid = 0;
		lister_sem_error(lst, node->row, node->col, "function does not return");
	}
}

void analyse_nprog(ASTree *ast, ASTNode *node, Lister *lst) {
	if (!unscoped_symbol_equals(node->symbol_value, node->right_child->symbol_value)) {
		ast->is_valid = 0;
		lister_sem_error(lst, node->row, 6, "program name mismatch"); // todo: figure out how to get this line number
	}
}

void analyse_naelt(ASTree *ast, ASTNode *node, Lister *lst) {
	Attribute *arr_atr = astree_get_attribute(ast, node->left_child->symbol_value);
	if (!arr_atr || arr_atr->type != SARRAY) {
		ast->is_valid = 0;
		lister_sem_error(lst, node->row, node->col, "variable is not an array");
	}
}

void analyse_nasgn(ASTree *ast, ASTNode *node, Lister *lst) {
	enum symbol_type left = node->left_child->symbol_type;
	enum symbol_type right = node->right_child->symbol_type;
	if (left != right) {
		// after having done codegen, this promotion is erroneous IMO
		// if (!(left == SREAL && right == SINT)) {
			if (left != SERROR && right != SERROR) {
				ast->is_valid = 0;
				lister_sem_error(lst, node->row, node->col, "incorrect type assignment");
			}
		// }
		return;
	}
	Attribute *left_atr = astree_get_attribute(ast, node->left_child->symbol_value);
	Attribute *right_atr = astree_get_attribute(ast, node->left_child->symbol_value);
	if (left == SARRAY) {
		// todo: if the user has reassigned array variables in local scope, this won't break when it should
		if (!unscoped_symbol_equals(left_atr->data, right_atr->data)) {
			ast->is_valid = 0;
			lister_sem_error(lst, node->row, node->col, "assignment of a different array type");
		}
	} else if (left == SSTRUCT) {
		if (!unscoped_symbol_equals(left_atr->data, right_atr->data)) {
			ast->is_valid = 0;
			lister_sem_error(lst, node->row, node->col, "assignment of struct between different array types");
		}
	}
}

void analyse_npow(ASTree *ast, ASTNode *node, Lister *lst) {
	if (node->left_child->symbol_type != SINT || node->right_child->symbol_type != SINT) {
		if (node->left_child->symbol_type != SERROR && node->right_child->symbol_type != SERROR) {
			ast->is_valid = 0;
			lister_sem_error(lst, node->row, node->col, "exponentiation is a INT -> INT operation");
		}
	}
}

void analyse_nmod(ASTree *ast, ASTNode *node, Lister *lst) {
	if (node->left_child->symbol_type != SINT || node->right_child->symbol_type != SINT) {
		if (node->left_child->symbol_type != SERROR && node->right_child->symbol_type != SERROR) {
			ast->is_valid = 0;
			lister_sem_error(lst, node->row, node->col, "modulus is only valid for integers");
		}
	}
}

void analyse_printitem(ASTree *ast, ASTNode *node, Lister *lst) {
	switch (node->symbol_type) {
		case SINT:
		case SREAL:
		case SSTRING:
			break;
		default:
			ast->is_valid = 0;
			lister_sem_error(lst, node->left_child->row, node->left_child->col, "only integers, reals or strings can be printed");
			break;
	}
}

void analyse_prlist(ASTree *ast, ASTNode *node, Lister *lst) {
	if (node->type == NPRLST) {
		analyse_printitem(ast, node->left_child, lst);
		analyse_prlist(ast, node->right_child, lst);
	} else {
		analyse_printitem(ast, node, lst);
	}
}

void analyse_relop(ASTree *ast, ASTNode *node, Lister *lst) {
	if ((node->left_child->symbol_type != SINT && node->left_child->symbol_type != SREAL ) &&
		(node->right_child->symbol_type != SINT && node->right_child->symbol_type != SREAL )
	) {
		if (node->left_child->symbol_type != SERROR || node->right_child->symbol_type != SERROR) {
			ast->is_valid = 0;
			lister_sem_error(lst, node->row, node->col, "relational operator requires integer arguments");
		}
	}
}

void analyse_node(ASTree *ast, ASTNode *node, Lister *lst) {
	if (!node) return;
	switch (node->type) {
		case NPROG:
			analyse_nprog(ast, node, lst);
			break;
		case NINIT:
			analyse_init(ast, node, lst);
			break;
		case NATYPE:
			analyse_natype(ast, node, lst);
			break;
		case NFUND:
			analyse_function(ast, node, lst);
			break;
		// case NARRC: (might not be needed)
		case NARRD:
			analyse_arrdecl(ast, node, lst);
			break;
		case NFCALL:
			analyse_fncall(ast, node, lst);
			break;
		case NAELT:
			analyse_naelt(ast, node, lst);
			break;
		case NSIMV:
			analyse_nsimv(ast, node, lst);
			break;
		case NCALL:
			analyse_ncall(ast, node, lst);
			break;
		case NASGN:
			analyse_nasgn(ast, node, lst);
			break;
		case NADD:
		case NSUB:
		case NMUL:
		case NDIV:
			// todo: check for non-numerics
			break;
		case NMOD:
			analyse_nmod(ast, node, lst);
			break;
		case NPOW:
			analyse_npow(ast, node, lst);
			break;
		case NOUTP:
		case NOUTL:
			if (node->left_child)
				analyse_prlist(ast, node->left_child, lst);
			break;
		case NEQL:
		case NNEQ:
		case NGRT:
		case NLSS:
		case NLEQ:
		case NGEQ:
			analyse_relop(ast, node, lst);
			break;
	}
	analyse_node(ast, node->left_child, lst);
	analyse_node(ast, node->middle_child, lst);
	analyse_node(ast, node->right_child, lst);
}

void analyse_program(ASTree *ast, Lister *lst) {
	update_node_symboltype(ast, ast->root, lst);
	analyse_node(ast, ast->root, lst);
}

