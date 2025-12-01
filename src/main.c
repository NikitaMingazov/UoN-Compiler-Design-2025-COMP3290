// vimargs ../programs/valid0_helloworld.cd

#define _GNU_SOURCE

#include "lib/sds.h"
#include "parser.h"
#include "semantic_analysis.h"
#include "code_generation.h"
#include "node.h"
#include "lister.h"
#include <stdlib.h>
#include <libgen.h>
#include <string.h>

/* todos:
   destructor for attribute
*/

char *path_of_listing(char **argv) {
	// Get dir of executable
	char *exec_path = strdup(argv[0]);
	char *exec_dir = dirname(exec_path);

	// Get filename of listing file
	char *source_path = strdup(argv[1]);
	char *source_base = basename(source_path);
	char *dot = strrchr(source_base, '.');
	if (dot) *dot = '\0'; // Remove extension

	char *ls_filename = malloc(strlen(source_base) + 5); // .lst + null
	snprintf(ls_filename, strlen(source_base) + 5, "%s.lst", source_base);

	char *full_ls_path = malloc(strlen(exec_dir) + strlen(ls_filename) + 2); // / + null
	snprintf(full_ls_path, strlen(exec_dir) + strlen(ls_filename) + 2, "%s/%s", exec_dir, ls_filename);
	free(ls_filename);
	free(source_path);
	free(exec_path);
	return full_ls_path;
}

char *module_filename(char **argv) {
	// Get dir of executable
	char *exec_path = strdup(argv[0]);
	char *exec_dir = dirname(exec_path);

	// Get filename of listing file
	char *source_path = strdup(argv[1]);
	char *source_base = basename(source_path);
	char *dot = strrchr(source_base, '.');
	if (dot) *dot = '\0'; // Remove extension

	char *mod_filename = malloc(strlen(source_base) + 5); // .lst + null
	snprintf(mod_filename, strlen(source_base) + 5, "%s.mod", source_base);

	char *full_mod_path = malloc(strlen(exec_dir) + strlen(mod_filename) + 2); // / + null
	snprintf(full_mod_path, strlen(exec_dir) + strlen(mod_filename) + 2, "%s/%s", exec_dir, mod_filename);
	free(mod_filename);
	free(source_path);
	free(exec_path);
	return full_mod_path;
}

int main(int argc, char **argv) {
	if (argc == 1) {
		printf("missing source file argument\n");
		return 1;
	}
	char *full_ls_path = path_of_listing(argv);

	Lister *lister = lister_create(full_ls_path);
	free(full_ls_path);

	ASTree *ast = get_AST(argv[1], lister);
	analyse_program(ast, lister);

	lister_print_to_terminal(lister);
	lister_close(lister);
	if (ast->is_valid) {
		printf("No errors found\n");
		char *mod_file = module_filename(argv);
		code_gen(mod_file, ast);
	// print module file to terminal
	// FILE *f = fopen(mod_file, "r"); int c; while ((c = fgetc(f)) != EOF) putchar(c); putchar('n'); fclose(f);
		free(mod_file);
	}

	astree_free(ast);
	return 0;
}

