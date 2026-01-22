/*
   aompiles a CD25 AST to SM25 bytecode
   does not use TAC, because it was out of scope of the uni project
*/
#include "x86_code_generation.h"
#include "../threeaddresscode.h"
#include "../lib/linkedlist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// to avoid relying on glibc extensions
static int asprintf(char **strp, const char *fmt, ...)
{
	va_list ap;
	va_list ap_copy;
	int len;
	char *buf;

	va_start(ap, fmt);

	va_copy(ap_copy, ap);
	len = vsnprintf(NULL, 0, fmt, ap_copy);
	va_end(ap_copy);

	if (len < 0) {
		va_end(ap);
		return -1;
	}

	buf = malloc((size_t)len + 1);
	if (!buf) {
		va_end(ap);
		return -1;
	}

	vsnprintf(buf, (size_t)len + 1, fmt, ap);
	va_end(ap);

	*strp = buf;
	return len;
}

static char *strdup(const char *s)
{
	size_t len = strlen(s) + 1;
	char *p = malloc(len);
	if (p)
		memcpy(p, s, len);
	return p;
}

typedef struct x86_codegen {
	TAC *tac;
	FILE *out_file;
	u16 num_in_params;
	u16 num_vars;
	u16 num_tmp_regs;
	u16 source_line;
	char *source_name;
	u16 num_generated_labels;
} Codegen;

enum x86_register {
	glob_array,
	x_intlit,
	x_fltlit,
	x_strlit,
	is_stack,
	rax,
	rbx,
	rcx,
	rdx,
	rsp,
	rbp,
	rsi,
	rdi,
	r8,
	r9,
	r10,
	r11,
	r12,
	r13,
	r14,
	r15,
	xmm0,
	xmm1,
	xmm15,
	al,
	dl,
};

const char reg_print[26][6] = {
	"", "", "", "", "",
	"rax", "rbx", "rcx", "rdx", "rsp", "rbp", "rsi", "rdi",
	"r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
	"xmm0", "xmm1", "xmm15",
	"al", "dl",
};

typedef struct x86_adr {
	enum x86_register reg;
	u16 offset;
} xadr;

xadr get_reg(Codegen *cdg, Adr adr) {
	u16 offset;
	switch (adr.type) {
		case A_PARAM:
			offset = adr.adr;
			break;
		case A_VAR:
			offset = cdg->num_in_params + adr.adr;
			break;
		case A_TMP:
			offset = cdg->num_in_params + cdg->num_vars + adr.adr;
			break;
		case A_ARRAY:
			return (xadr) { glob_array, adr.adr };
		case A_ILIT:
			return (xadr) { x_intlit, adr.adr };
		case A_FLIT:
			return (xadr) { x_fltlit, adr.adr };
		case A_STR:
			return (xadr) { x_strlit, adr.adr };
		default: abort();
	}
	xadr reg = (xadr) {is_stack, offset};
	return reg;
}

xadr mkreg(enum x86_register reg) {
	return (xadr) { reg, 0 };
}

char *adrstr(Codegen *cdg, xadr adr) {
	char *buffer;
	switch (adr.reg) {
		case is_stack:
			asprintf(&buffer, "qword [rbp-%d]", (adr.offset+1)*8);
			return buffer;
		case glob_array:
			asprintf(&buffer, "A%d", adr.offset);
			return buffer;
		case x_intlit:
			asprintf(&buffer, "%ld", *(long*)tac_data(cdg->tac, (Adr){A_ILIT, adr.offset}));
			return buffer;
		case x_fltlit:
			asprintf(&buffer, "[rel F%d]", adr.offset);
			return buffer;
		case x_strlit:
			asprintf(&buffer, "S%d", adr.offset);
			return buffer;
		default:
			return strdup(reg_print[adr.reg]);
	}
}

void print_binary(Codegen *cdg, const char *op, xadr first, xadr second) {
	char *a1 = adrstr(cdg, first);
	char *a2 = adrstr(cdg, second);
	fprintf(cdg->out_file, "    %s %s, %s\n", op, a1, a2);
	free(a1);
	free(a2);
}

void mov_wrapper(Codegen *cdg, xadr first, xadr second) {
	if (second.reg == x_fltlit) {
		fprintf(cdg->out_file, "    movq xmm0, [rel F%d]\n", second.offset);
		fprintf(cdg->out_file, "    movq %s, xmm0\n", adrstr(cdg, first));
	} else if (first.reg != is_stack || second.reg != is_stack) {
		print_binary(cdg, "mov", first, second);
	} else {
		print_binary(cdg, "mov", mkreg(rax), second);
		print_binary(cdg, "mov", first, mkreg(rax));
	}
}

void print_unary(Codegen *cdg, const char *op, xadr first) {
	char *a1 = adrstr(cdg, first);
	fprintf(cdg->out_file, "    %s %s\n", op, a1);
	free(a1);
}

void params_of_next_call(Codegen *cdg, int *step_counter, int *paramsbetween) {
	linkedlist_forward(cdg->tac->lines);
	++*step_counter;
	while (linkedlist_get_current(cdg->tac->lines)) {
		enum operation op = ((Line*)linkedlist_get_current(cdg->tac->lines))->op;
		if (op == O_CALL || op == O_CALLVAL) {
			break;
		} else if (op == O_PARAM) {
			++*paramsbetween;
		}
		linkedlist_forward(cdg->tac->lines);
		++*step_counter;
	}
	if (linkedlist_get_current(cdg->tac->lines)) {
		/* return *(long*) tac_data(cdg->tac, ((Line*)linkedlist_get_current(cdg->tac->lines))->right); */
		return;
	} else {
		abort();
	}
}

void arithmetic(Codegen *cdg, const char *op, Line *line) {
	if (strlen(op) == 5) {
		if (line->middle.type == A_FLIT) {
			fprintf(cdg->out_file, "movq xmm0, [rel F%d]\n", line->middle.adr);
		} else {
			print_binary(cdg, "movq", mkreg(xmm0), get_reg(cdg, line->middle));
		}
		print_binary(cdg, op, mkreg(xmm0), get_reg(cdg, line->right));
		print_binary(cdg, "movq", get_reg(cdg, line->left), mkreg(xmm0));
		return;
	}
	print_binary(cdg, "mov", mkreg(rax), get_reg(cdg, line->middle));
	print_binary(cdg, op, mkreg(rax), get_reg(cdg, line->right));
	print_binary(cdg, "mov", get_reg(cdg, line->left), mkreg(rax));
}

void boolean_i(Codegen *cdg, const char *op, Line *line) {
	fprintf(cdg->out_file, "    xor rdx, rdx\n");
	print_binary(cdg, "mov", mkreg(rax), get_reg(cdg, line->middle));
	print_binary(cdg, "cmp", mkreg(rax), get_reg(cdg, line->right));
	print_unary(cdg, op, mkreg(dl)); // setcc
	print_binary(cdg, "mov", get_reg(cdg, line->left), mkreg(rdx));
}

void boolean_f(Codegen *cdg, const char *op, Line *line) {
	fprintf(cdg->out_file, "    xor rdx, rdx\n");
	print_binary(cdg, "movq", mkreg(xmm0), get_reg(cdg, line->middle));
	print_binary(cdg, "ucomisd", mkreg(xmm0), get_reg(cdg, line->right));
	print_unary(cdg, op, mkreg(dl)); // setcc
	print_binary(cdg, "mov", get_reg(cdg, line->left), mkreg(rdx));
}

void resolve_line(Codegen *cdg, Line *line) {
	FILE *out = cdg->out_file;
	if (cdg->source_name && line->linenum > cdg->source_line) {
		fprintf(out, "%%line %d+1 \"%s\"\n", line->linenum, cdg->source_name);
		cdg->source_line = line->linenum;
	}
	char *s;
	int step_counter;
	int paramsbetween;
	Line *l;
	switch (line->op) {
		case O_STORE:
			fprintf(out, "    mov rdx, %s\n",
			        adrstr(cdg, get_reg(cdg, line->left))
			);
			fprintf(out, "    mov rcx, %s\n",
			        adrstr(cdg, get_reg(cdg, line->right))
			);
			fprintf(out, "    mov [rdx], rcx\n");
			break;
		case O_DEREF:
			fprintf(out, "    mov rdx, %s\n",
			        adrstr(cdg, get_reg(cdg, line->right))
			);
			fprintf(out, "    mov rcx, [rdx]\n");
			fprintf(out, "    mov %s, rcx\n",
			        adrstr(cdg, get_reg(cdg, line->left))
			);
			break;
		case O_PRINTI:
			fprintf(out, "    mov rdi, [rel stdout]\n");
			fprintf(out, "    lea rsi, [rel inttmp]\n");
			print_binary(cdg, "mov", mkreg(rdx), get_reg(cdg, line->left));
			fprintf(out, "    xor eax, eax\n");
			fprintf(out, "    call fprintf\n");
			break;
		case O_PRINTF:
			fprintf(out, "    mov rdi, [rel stdout]\n");
			fprintf(out, "    lea rsi, [rel flttmp]\n");
			print_binary(cdg, "movq", mkreg(xmm0), get_reg(cdg, line->left));
			fprintf(out, "    mov rax, 1\n");
			fprintf(out, "    call fprintf\n");
			break;
		case O_PRINTSTR:
			fprintf(out, "    mov rdi, [rel stdout]\n");
			fprintf(out, "    lea rsi, [rel strtmp]\n");
			print_binary(cdg, "mov", mkreg(rdx), get_reg(cdg, line->left));
			fprintf(out, "    xor eax, eax\n");
			fprintf(out, "    call fprintf\n");
			break;
		case O_PRINTLN:
			fprintf(out, "    mov rdi, [rel stdout]\n");
			fprintf(out, "    lea rsi, [rel strtmp]\n");
			fprintf(out, "    lea rdx, [rel newln]\n");
			fprintf(out, "    xor eax, eax\n");
			fprintf(out, "    call fprintf\n");
			break;
		case O_PRINTSPC:
			fprintf(out, "    mov rdi, [rel stdout]\n");
			fprintf(out, "    lea rsi, [rel strtmp]\n");
			fprintf(out, "    lea rdx, [rel space]\n");
			fprintf(out, "    xor eax, eax\n");
			fprintf(out, "    call fprintf\n");
			break;
		case O_READI:
			fprintf(out, "    mov rdi, 0\n");
			fprintf(out, "    call READINPUT\n");
			print_binary(cdg, "mov", get_reg(cdg, line->left), mkreg(rax));
			break;
		case O_READF:
			fprintf(out, "    mov rdi, 1\n");
			fprintf(out, "    call READINPUT\n");
			print_binary(cdg, "movq", get_reg(cdg, line->left), mkreg(xmm0));
			break;
		case O_ITOF:
			if (line->right.type != A_ILIT) {
				print_binary(cdg, "cvtsi2sd", mkreg(xmm0), get_reg(cdg, line->right));
			} else {
				print_binary(cdg, "mov", mkreg(rax), get_reg(cdg, line->right));
				print_binary(cdg, "cvtsi2sd", mkreg(xmm0), mkreg(rax));
			}
			print_binary(cdg, "movq", get_reg(cdg, line->left), mkreg(xmm0));
			break;
		case O_LABEL:
			fprintf(out, ".L%d:\n", line->left.adr);
			break;
		case O_GOTO:
			fprintf(out, "    jmp .L%d\n", line->left.adr);
			break;
		case O_GOTOT:
			/* problem with is is it doesn't work for stack var */
			/* fprintf(out, "    test %s, %s\n", */
			/*         adrstr(cdg, get_reg(cdg, line->right)), */
			/*         adrstr(cdg, get_reg(cdg, line->right)) */
			/* ); */
			/* fprintf(out, "    jnz .L%d\n", line->left.adr); */
			fprintf(out, "    cmp %s, 0\n", adrstr(cdg, get_reg(cdg, line->right)));
			fprintf(out, "    jne .L%d\n", line->left.adr);
			break;
		case O_GOTOF:
			fprintf(out, "    cmp %s, 0\n", adrstr(cdg, get_reg(cdg, line->right)));
			fprintf(out, "    je .L%d\n", line->left.adr);
			break;
		case O_ALLOC:
			// for function-local arrays, which I don't have
			break;
		case O_ASIGN:
			mov_wrapper(cdg, get_reg(cdg, line->left), get_reg(cdg, line->right));
			break;
		case O_ADDI:
			arithmetic(cdg, "add", line);
			break;
		case O_ADDF:
			arithmetic(cdg, "addsd", line);
			break;
		case O_SUBI:
			arithmetic(cdg, "sub", line);
			break;
		case O_SUBF:
			arithmetic(cdg, "subsd", line);
			break;
		case O_MULI:
			arithmetic(cdg, "imul", line);
			break;
		case O_MULF:
			arithmetic(cdg, "mulsd", line);
			break;
		case O_DIVI:
			print_binary(cdg, "mov", mkreg(rax), get_reg(cdg, line->middle));
			print_unary(cdg, "div", get_reg(cdg, line->right));
			print_binary(cdg, "mov", get_reg(cdg, line->left), mkreg(rax));
			break;
		case O_DIVF:
			arithmetic(cdg, "divsd", line);
			break;
		case O_MOD:
			print_binary(cdg, "mov", mkreg(rax), get_reg(cdg, line->middle));
			print_unary(cdg, "div", get_reg(cdg, line->right));
			print_binary(cdg, "mov", get_reg(cdg, line->left), mkreg(rdx));
			break;
		case O_POWII:
			fprintf(out, "    mov rcx, 1\n");
			fprintf(out, "    mov rdx, 0\n");
			fprintf(out, "    .CL%d:\n", cdg->num_generated_labels);
			print_binary(cdg, "cmp", mkreg(rdx), get_reg(cdg, line->right));
			fprintf(out, "    je .CL%d\n", cdg->num_generated_labels+1);
			print_binary(cdg, "imul", mkreg(rcx), get_reg(cdg, line->middle));
			fprintf(out, "    inc rdx\n");
			fprintf(out, "    jmp .CL%d\n", cdg->num_generated_labels);
			fprintf(out, "    .CL%d:\n", cdg->num_generated_labels+1);
			print_binary(cdg, "mov", get_reg(cdg, line->left), mkreg(rcx));
			cdg->num_generated_labels += 2;
			break;
		case O_POWIF:
			// oops this doesn't exist in SM25!
			abort();
			/*
				fprintf(out, "    mov rax, 1\n");
				fprintf(out, "    cvtsd2si xmm0, rax\n");
				fprintf(out, "    mov rdx, 0\n");
				fprintf(out, "    .CL%d:\n", cdg->num_generated_labels);
				print_binary(cdg, "cmp", mkreg(xmm0), get_reg(cdg, line->right));
				fprintf(out, "    je .CL%d\n", cdg->num_generated_labels+1);
				print_binary(cdg, "mulsd", mkreg(xmm0), get_reg(cdg, line->middle));
				fprintf(out, "    inc rdx\n");
				fprintf(out, "    jmp .CL%d\n", cdg->num_generated_labels);
				fprintf(out, "    .CL%d:\n", cdg->num_generated_labels+1);
				print_binary(cdg, "movq", get_reg(cdg, line->left), mkreg(xmm0));
				cdg->num_generated_labels += 2;
			*/
			break;
		case O_TRUE:
			s = adrstr(cdg, get_reg(cdg, line->left));
			fprintf(out, "    mov %s, 1\n", s);
			free(s);
			break;
		case O_FALSE:
			s = adrstr(cdg, get_reg(cdg, line->left));
			fprintf(out, "    mov %s, 0\n", s);
			free(s);
			break;
		case O_EQI:
			boolean_i(cdg, "sete", line);
			break;
		case O_NEQI:
			boolean_i(cdg, "setne", line);
			break;
		case O_LTI:
			boolean_i(cdg, "setl", line);
			break;
		case O_LTEI:
			boolean_i(cdg, "setle", line);
			break;
		case O_GTI:
			boolean_i(cdg, "setg", line);
			break;
		case O_GTEI:
			boolean_i(cdg, "setge", line);
			break;
		case O_EQF:
			boolean_f(cdg, "sete", line);
			break;
		case O_NEQF:
			boolean_f(cdg, "setne", line);
			break;
		case O_LTF:
			boolean_f(cdg, "setb", line);
			break;
		case O_LTEF:
			boolean_f(cdg, "setbe", line);
			break;
		case O_GTF:
			boolean_f(cdg, "seta", line);
			break;
		case O_GTEF:
			boolean_f(cdg, "setae", line);
			break;
		case O_AND:
			arithmetic(cdg, "and", line);
			break;
		case O_OR:
			arithmetic(cdg, "or", line);
			break;
		case O_XOR:
			arithmetic(cdg, "xor", line);
			break;
		case O_NOT:
			print_unary(cdg, "not", get_reg(cdg, line->left));
			break;
		case O_PARAM:
			// do a lookahead to find the matching function call
			paramsbetween = 0;
			step_counter = 0;
			params_of_next_call(cdg, &step_counter, &paramsbetween);
			// get back to this line
			while (step_counter > 0) {
				linkedlist_back(cdg->tac->lines);
				step_counter--;
			}
			switch (paramsbetween) {
				case 0:
					print_binary(cdg, "mov", mkreg(rdi), get_reg(cdg, line->left));
					break;
				case 1:
					print_binary(cdg, "mov", mkreg(rsi), get_reg(cdg, line->left));
					break;
				case 2:
					print_binary(cdg, "mov", mkreg(rdx), get_reg(cdg, line->left));
					break;
				case 3:
					print_binary(cdg, "mov", mkreg(rcx), get_reg(cdg, line->left));
					break;
				case 4:
					print_binary(cdg, "mov", mkreg(r8), get_reg(cdg, line->left));
					break;
				case 5:
					print_binary(cdg, "mov", mkreg(r9), get_reg(cdg, line->left));
					break;
			}
			break;
		case O_CALL:
			fprintf(out, "    call %s\n", (char*)tac_data(cdg->tac, line->left));
			break;
		case O_CALLVAL:
			fprintf(out, "    call %s\n", (char*)tac_data(cdg->tac, line->middle));
			print_binary(cdg, "mov", get_reg(cdg, line->left), mkreg(rax));
			break;
		case O_RVAL:
			print_binary(cdg, "mov", mkreg(rax), get_reg(cdg, line->left));
		case O_RETN:
			/* fprintf(out, "    add rsp, %d\n", (cdg->num_in_params + cdg->num_tmp_regs + cdg->num_vars)*8); */
			fprintf(out, "    mov rsp, rbp\n");
			fprintf(out, "    pop rbp\n");
			fprintf(out, "    ret\n");
			break;
		case O_FUNC:
			fprintf(out, "    global %s\n", (char*)tac_data(cdg->tac, line->left));
			fprintf(out, "%s:\n", (char*)tac_data(cdg->tac, line->left));
			if (strcmp("main", (char*)tac_data(cdg->tac, line->left)) == 0) {
				fprintf(out, "    mov rdi, FILENAME\n");
				fprintf(out, "    mov rsi, READMODE\n");
				fprintf(out, "    call fopen\n");
				fprintf(out, "    mov [rel fp], rax\n");
				fprintf(out, "    test rax, rax\n");
				fprintf(out, "    jnz .fileopened\n");
				fprintf(out, "    mov rax, 1              ; sys_write\n");
				fprintf(out, "    mov rdi, 1              ; stdout\n");
				fprintf(out, "    mov rsi, EXITERROR\n");
				fprintf(out, "    mov rdx, EXITERRORLEN\n");
				fprintf(out, "    syscall\n");
				fprintf(out, "    mov rax, 60          ; sys_exit\n");
				fprintf(out, "    mov rdi, 1           ; error code\n");
				fprintf(out, "    syscall\n");
				fprintf(out, ".fileopened:\n");
			}
			step_counter = 0;
			// find biggest temp var (on left)
			cdg->num_tmp_regs = 0;
			// find biggest var (on left)
			cdg->num_vars = 0;
			// find biggest param (on right)
			cdg->num_in_params = 0;
			while ((l = linkedlist_get_current(cdg->tac->lines))) {
				if (l->left.type == A_VAR) {
					if (l->left.adr >= cdg->num_vars) {
						cdg->num_vars = l->left.adr + 1;
					}
				} else if (l->left.type == A_TMP) {
					if (l->left.adr >= cdg->num_tmp_regs) {
						cdg->num_tmp_regs = l->left.adr + 1;
					}
				}
				if (l->left.type == A_PARAM) {
					if (l->left.adr >= cdg->num_in_params) {
						cdg->num_in_params = l->left.adr + 1;
					}
				}
				if (l->middle.type == A_PARAM) {
					if (l->middle.adr >= cdg->num_in_params) {
						cdg->num_in_params = l->middle.adr + 1;
					}
				}
				if (l->right.type == A_PARAM) {
					if (l->right.adr >= cdg->num_in_params) {
						cdg->num_in_params = l->right.adr + 1;
					}
				}
				linkedlist_forward(cdg->tac->lines);
				step_counter++;
				if ( !linkedlist_get_current(cdg->tac->lines) ||
				     ((Line*)linkedlist_get_current(cdg->tac->lines))->op == O_FUNC) {
					// go back to the end if the LL is now in the void
					if (!linkedlist_get_current(cdg->tac->lines)) {
						linkedlist_end(cdg->tac->lines);
						step_counter--;
					}
					break;
				}
			}
			if (cdg->num_in_params + cdg->num_tmp_regs + cdg->num_vars % 2 == 1) {
				cdg->num_tmp_regs++;
			}
			fprintf(out, "    push rbp\n");
			fprintf(out, "    mov rbp, rsp\n");
			int alloc_bytes = (cdg->num_in_params + cdg->num_tmp_regs + cdg->num_vars)*8;
			alloc_bytes += alloc_bytes % 16; // align stack for word boundary (call frame added 8, push rbp another 8)
			fprintf(out, "    sub rsp, %d\n", alloc_bytes);
			while (step_counter > 0) {
				linkedlist_back(cdg->tac->lines);
				step_counter--;
			}
			for (step_counter = 0; step_counter < cdg->num_in_params; ++step_counter) {
				switch (step_counter) {
					case 0:
						fprintf(out, "    mov [rbp-%d], rdi\n", (1+step_counter)*8);
						break;
					case 1:
						fprintf(out, "    mov [rbp-%d], rsi\n", (1+step_counter)*8);
						break;
					case 2:
						fprintf(out, "    mov [rbp-%d], rdx\n", (1+step_counter)*8);
						break;
					case 3:
						fprintf(out, "    mov [rbp-%d], rcx\n", (1+step_counter)*8);
						break;
					case 4:
						fprintf(out, "    mov [rbp-%d], r8\n", (1+step_counter)*8);
						break;
					case 5:
						fprintf(out, "    mov [rbp-%d], r9\n", (1+step_counter)*8);
						break;
				}
			}
			break;
	}
}

void print_code(Codegen *cdg) {
	linkedlist_start(cdg->tac->lines);
	while (linkedlist_get_current(cdg->tac->lines)) {
		resolve_line(cdg, linkedlist_get_current(cdg->tac->lines));
		linkedlist_forward(cdg->tac->lines);
	}
}

void print_consts(Codegen *cdg) {
	TAC *tac = cdg->tac;
	int i;
	// because of movq these are still needed
	linkedlist_start(tac->floats);
	i = 0;
	double *x;
	while ((x=(double*)linkedlist_get_current(tac->floats))) {
		fprintf(cdg->out_file, "    F%d dq %f\n", i, *x);
		linkedlist_forward(tac->floats);
		++i;
	}
	linkedlist_start(tac->strings);
	i = 0;
	char *s;
	while ((s=(char*)linkedlist_get_current(tac->strings))) {
		fprintf(cdg->out_file, "    S%d db \"%s\", 0\n", i, s);
		linkedlist_forward(tac->strings);
		++i;
	}
}

void cat_to_file(FILE *out, const char *filename) {
	FILE *in = fopen(filename, "r");
	if (!in) return;

	int c;
	while ((c = fgetc(in)) != EOF) {
    	fputc(c, out);
	}

	fclose(in);
}

void x86_code_gen(char *dest_path, TAC* tac, char *source_name) {
	FILE *out = fopen(dest_path, "w");
	Codegen state = (Codegen) {
		tac,
		out,
		0,
		0,
		0,
		0,
		source_name,
		0,
	};
	fprintf(out, "section .bss\n");
	fprintf(out, "    fp resq 1\n");
	linkedlist_start(tac->arrays);
	int count = 0;
	while (linkedlist_get_current(tac->arrays)) {
		int len = *(int*)linkedlist_get_current(tac->arrays);
	fprintf(out, "    A%d resb %d\n", count++, len);
		linkedlist_forward(tac->arrays);
	}
	fprintf(out, "section .rodata\n");
	print_consts(&state);
	fprintf(out, "    strtmp db \"%%s\", 0\n");
	fprintf(out, "    inttmp db \"%%ld\", 0\n");
	fprintf(out, "    flttmp db \"%%lf\", 0\n");
	fprintf(out, "    space db \" \", 0\n");
	fprintf(out, "    newln db 10, 0\n");
	fprintf(out, "    extern exit, stdout, fprintf, fopen, fgetc, atol, atof\n");
	fprintf(out, "section .data\n");
	fprintf(out, "    FILENAME db \"sm25stdin.txt\", 0\n"); // todo: take as an arg
    fprintf(out, "    READMODE db \"r\", 0\n");
    fprintf(out, "    EXITERROR db \"could not open in file, aborting\", 10\n");
    fprintf(out, "    EXITERRORLEN equ $ - EXITERROR\n");
	fprintf(out, "section .text\n");
	cat_to_file(out, "./inputlib.asm");
	print_code(&state);
	fprintf(out, "    mov rdi, 0\n");
	fprintf(out, "    call exit\n");
}

