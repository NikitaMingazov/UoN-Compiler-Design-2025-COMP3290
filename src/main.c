#include "parser.h"
#include "semantic_analysis.h"
#include "sm25_codegen/sm25_code_generation.h"
#include "x86_codegen/x86_code_generation.h"
#include "threeaddresscode.h"
#include "lister.h"
#include <stdlib.h>
#include <libgen.h>
#include <string.h>
#include <unistd.h> // getcwd()

static char* strdup(const char* s) {
	size_t len = strlen(s) + 1;
	char* dup = malloc(len);
	if (dup) {
		memcpy(dup, s, len);
	}
	return dup;
}

/* todos:
   destructor for attribute
   correct error on passing in a directory as source file (syntax error currently)
*/

char *path_of_listing(char *out_path, char *source_path) {
	char *source_base = basename(strdup(source_path));
	char *dot = strrchr(source_base, '.');
	if (dot) *dot = '\0'; // Remove extension
	char *ls_filename = malloc(strlen(source_base) + 5); // .lst + null
	snprintf(ls_filename, strlen(source_base) + 5, "%s.lst", source_base);
	char *full_ls_path = malloc(strlen(out_path) + strlen(ls_filename) + 2); // / + null
	snprintf(full_ls_path, strlen(out_path) + strlen(ls_filename) + 2, "%s/%s", out_path, ls_filename);
	free(ls_filename);
	return full_ls_path;
}

char *module_filename(char *out_path, char *source_path) {
	char *source_base = basename(strdup(source_path));
	char *dot = strrchr(source_base, '.');
	if (dot) *dot = '\0'; // Remove extension
	char *mod_filename = malloc(strlen(source_base) + 5); // .mod + null
	snprintf(mod_filename, strlen(source_base) + 5, "%s.mod", source_base);
	char *full_mod_path = malloc(strlen(out_path) + strlen(mod_filename) + 2); // / + null
	snprintf(full_mod_path, strlen(out_path) + strlen(mod_filename) + 2, "%s/%s", out_path, mod_filename);
	free(mod_filename);
	return full_mod_path;
}

char *asm_filename(char *out_path, char *source_path) {
	char *source_base = basename(strdup(source_path));
	char *dot = strrchr(source_base, '.');
	if (dot) *dot = '\0'; // Remove extension
	char *asm_filename = malloc(strlen(source_base) + 5); // .asm + null
	snprintf(asm_filename, strlen(source_base) + 5, "%s.asm", source_base);
	char *full_asm_path = malloc(strlen(out_path) + strlen(asm_filename) + 2); // / + null
	snprintf(full_asm_path, strlen(out_path) + strlen(asm_filename) + 2, "%s/%s", out_path, asm_filename);
	free(asm_filename);
	return full_asm_path;
}

enum arch {
	X86_LINUX,
	SM25,
};

#define REQUIRED_ARGS \
	REQUIRED_STRING_ARG(in_file, "in_file", "Input filepath") \

#define OPTIONAL_ARGS \
	OPTIONAL_STRING_ARG(out_path, "", "-o", "out_file", "Output filepath") \
	OPTIONAL_STRING_ARG(arch, "x86", "-a", "arch", "Architecture [x86|sm25]")

#define BOOLEAN_ARGS \
	BOOLEAN_ARG(debug, "-g", "Emit debugging symbols in asm (WIP)") \
	BOOLEAN_ARG(print_tac, "-T", "Print TAC to stdout and stop compilation") \
	BOOLEAN_ARG(print_ast, "-A", "Print AST to stdout and stop compilation") \
	BOOLEAN_ARG(readable_sm25, "-S", "Print SM25 opcodes to stdout and stop compilation") \
	BOOLEAN_ARG(make_listing, "-l", "Produce listing file next to output path") \
	BOOLEAN_ARG(help, "-h", "Show help")

#include "lib/easyargs.h"

int main(int argc, char **argv) {
	args_t args = make_default_args();
	if (parse_args(argc, argv, &args) || args.help) {
		print_help(argv[0]);
		if (args.help) {
			return 0;
		} else {
			return 1;
		}
	}

	if (args.debug && strcmp(args.arch, "sm25") == 0) {
		printf("Cannot emit debugging metadata for SM25\n");
		return 1;
	}

	if (args.print_ast && args.print_tac && args.readable_sm25) {
		printf("Can only stop compilation once\n");
		return 1;
	}
	if (args.readable_sm25) {
		args.arch = "sm25";
	}
	if (args.print_tac) {
		args.arch = "x86";
	}

	enum arch architecture;
	if (strcmp(args.arch, "x86") == 0) {
		architecture = X86_LINUX;
	} else if (strcmp(args.arch, "sm25") == 0) {
		architecture = SM25;
	} else {
		printf("unknown architecture, options are x86 and sm25\n"); // TODO move this to easy-args (implement enum)
		return 1;
	}

	char *out_path;
	if (*args.out_path) {
		out_path = args.out_path;
	} else {
		out_path = getcwd(NULL, 0);
	}

	char *full_ls_path = NULL;
	if (args.make_listing) {
		full_ls_path = path_of_listing(out_path, args.in_file);
	}
	Lister *lister = lister_create(full_ls_path);
	free(full_ls_path);

	ASTree *ast = get_AST(args.in_file, lister);
	analyse_program(ast, lister);
	if (args.print_ast && ast->is_valid) {
		astree_printf(ast);
		return 0;
	}

	lister_print_to_terminal(lister);
	lister_close(lister);

	char *filepath = NULL;
	if (ast->is_valid) {
		tac = tac_from_ast(ast);
		if (args.print_tac) {
			tac_printf(tac);
			return 0;
		}
		// if an out filename was given, use it. if not, a per-arch default is used
		if (*args.out_path) {
			filepath = args.out_path;
		}
		// the backend
		switch (architecture) {
			case X86_LINUX:
				if (!filepath) {
					filepath = asm_filename(out_path, args.in_file);
				}
				if (args.debug) {
					x86_code_gen(filepath, tac, basename(args.in_file));
				} else {
					x86_code_gen(filepath, tac, NULL);
				}
				break;
			// this was my uni project, hence commented out output
			// also why no TAC, the project didn't use one
			case SM25:
				// printf("No errors found\n");
				if (!filepath) {
					filepath = module_filename(out_path, args.in_file);
				}
				sm25_code_gen(filepath, ast, args.readable_sm25);
				// print module file to terminal
				// FILE *f = fopen(filepath, "r"); int c; while ((c = fgetc(f)) != EOF) putchar(c); putchar('n'); fclose(f);
				break;
		}
		tac_free(tac);
		if (!args.out_path) {
			free(filepath);
		}
	} else {
		return 1;
	}
	astree_free(ast);
	return 0;
}

