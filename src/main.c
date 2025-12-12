// vimargs ../cd25_programs/valid0_helloworld.cd

#include "lib/sds.h"
#include "parser.h"
#include "semantic_analysis.h"
#include "sm25_codegen/sm25_code_generation.h"
#include "x86_codegen/x86_code_generation.h"
#include "threeaddresscode.h"
#include "node.h"
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

int main(int argc, char **argv) {
	if (argc == 1) {
		printf("missing source file argument\n");
		return 1;
	}
	// TODO: proper arg parsing / split binary releases
	enum arch architecture;
	if (argc == 2) {
		printf("x86_64_linux is WIP\n"); // remove this line later
		architecture = X86_LINUX;
	} else {
		printf("building for SM25\n");
		architecture = SM25;
	}
	char *full_ls_path = path_of_listing(argv[0], argv[1]);

	Lister *lister = lister_create(full_ls_path);
	free(full_ls_path);

	ASTree *ast = get_AST(argv[1], lister);
	analyse_program(ast, lister);

	lister_print_to_terminal(lister);
	lister_close(lister);
	if (ast->is_valid) {
		TAC *tac = tac_from_ast(ast);
		// the backend
		switch (architecture) {
			case X86_LINUX:
				char *bin_filepath = binary_filename(argv[0], argv[1]);
				x86_code_gen(bin_filepath, tac);
				free(bin_filepath);
				break;
			// this was my uni project, hence commented out output
			// also why no TAC, the project didn't use one
			case SM25:
				// printf("No errors found\n");
				char *mod_file = module_filename(argv[0], argv[1]);
				sm25_code_gen(mod_file, ast);
				// print module file to terminal
				// FILE *f = fopen(mod_file, "r"); int c; while ((c = fgetc(f)) != EOF) putchar(c); putchar('n'); fclose(f);
				free(mod_file);
				break;
		}
		tac_free(tac);
	}
	astree_free(ast);
	return 0;
}

