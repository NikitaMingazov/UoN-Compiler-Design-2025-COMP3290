// vimargs ../cd25_programs/valid0_helloworld.cd

#include "lib/sds.h"
#include "parser.h"
#include "semantic_analysis.h"
#include "sm25_codegen/sm25_code_generation.h"
#include "x86_codegen/x86_code_generation.h"
#include "threeaddresscode.h"
#include "lister.h"
#include <stdlib.h>
#include <libgen.h>
#include <string.h>

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
*/

char *path_of_listing(char *exec_path, char *source_path) {
	char *exec_dir = dirname(exec_path);
	char *source_base = basename(strdup(source_path));
	char *dot = strrchr(source_base, '.');
	if (dot) *dot = '\0'; // Remove extension
	char *ls_filename = malloc(strlen(source_base) + 5); // .lst + null
	snprintf(ls_filename, strlen(source_base) + 5, "%s.lst", source_base);
	char *full_ls_path = malloc(strlen(exec_dir) + strlen(ls_filename) + 2); // / + null
	snprintf(full_ls_path, strlen(exec_dir) + strlen(ls_filename) + 2, "%s/%s", exec_dir, ls_filename);
	free(ls_filename);
	return full_ls_path;
}

char *module_filename(char *exec_path, char *source_path) {
	char *exec_dir = dirname(exec_path);
	char *source_base = basename(strdup(source_path));
	char *dot = strrchr(source_base, '.');
	if (dot) *dot = '\0'; // Remove extension
	char *mod_filename = malloc(strlen(source_base) + 5); // .lst + null
	snprintf(mod_filename, strlen(source_base) + 5, "%s.mod", source_base);
	char *full_mod_path = malloc(strlen(exec_dir) + strlen(mod_filename) + 2); // / + null
	snprintf(full_mod_path, strlen(exec_dir) + strlen(mod_filename) + 2, "%s/%s", exec_dir, mod_filename);
	free(mod_filename);
	return full_mod_path;
}

char *binary_filename(char *exec_path, char *source_path) {
	char *exec_dir = dirname(exec_path);
	char *dup_path = strdup(source_path);
	char *bin_filename = basename(dup_path); // no extension
	char *dot = strrchr(bin_filename, '.');
	if (dot) *dot = '\0'; // Remove extension
	char *full_bin_path = malloc(strlen(exec_dir) + strlen(bin_filename) + 2); // / + null
	snprintf(full_bin_path, strlen(exec_dir) + strlen(bin_filename) + 2, "%s/%s", exec_dir, bin_filename);
	free(dup_path);
	return full_bin_path;
}

enum arch {
	SM25,
	X86_LINUX,
};

#define REQUIRED_ARGS \
	REQUIRED_STRING_ARG(in_file, "i", "Input file path") \

#define OPTIONAL_ARGS \
	OPTIONAL_STRING_ARG(out_path, "", "-o", "out_file", "Output filename (TODO)") \
	OPTIONAL_STRING_ARG(arch, "x86", "-a", "arch", "Architecture [x86|sm25]")

#define BOOLEAN_ARGS \
	BOOLEAN_ARG(print_tac, "-t", "Print TAC to terminal") \
	BOOLEAN_ARG(make_listing, "-l", "Produce listing file at output path") \
	BOOLEAN_ARG(help, "-h", "Show help")

#include "lib/easyargs.h"


int main(int argc, char **argv) {
	args_t args = make_default_args();
	if (parse_args(argc, argv, &args) || args.help) {
		print_help(argv[0]);
		return 1;
	}

	enum arch architecture;
	if (strcmp(args.arch, "x86") == 0) {
		architecture = X86_LINUX;
	} else if (strcmp(args.arch, "sm25") == 0) {
		architecture = SM25;
	} else {
		printf("unknown architecture, options are x86 and sm25"); // TODO move this to easy-args
		return 1;
	}

	char *full_ls_path = NULL;
	if (args.make_listing) {
		full_ls_path = path_of_listing(argv[0], args.in_file);
	}
	Lister *lister = lister_create(full_ls_path);
	free(full_ls_path);

	ASTree *ast = get_AST(args.in_file, lister);
	analyse_program(ast, lister);

	lister_print_to_terminal(lister);
	lister_close(lister);

	char *filepath;
	if (ast->is_valid) {
		TAC *tac = tac_from_ast(ast);
		if (args.print_tac) {
			tac_printf(tac);
		}
		// the backend
		switch (architecture) {
			case X86_LINUX:
				filepath = binary_filename(argv[0], args.in_file);
				x86_code_gen(filepath, tac);
				free(filepath);
				break;
			// this was my uni project, hence commented out output
			// also why no TAC, the project didn't use one
			case SM25:
				// printf("No errors found\n");
				filepath = module_filename(argv[0], args.in_file);
				sm25_code_gen(filepath, ast);
				// print module file to terminal
				// FILE *f = fopen(mod_file, "r"); int c; while ((c = fgetc(f)) != EOF) putchar(c); putchar('n'); fclose(f);
				free(filepath);
		}
		tac_free(tac);
	}
	astree_free(ast);
	return 0;
}

