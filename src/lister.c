// implements the listing file (source with line#s, and a block of warning_queue/error_queue beneath)

#include <stdlib.h>
#include "lib/defs.h"
#include "lister.h"

void lister_print_to_terminal(Lister *lister) {
	char *msg;
	linkedlist_start(lister->warning_queue);
	while (msg = (char *)linkedlist_get_current(lister->warning_queue)) {
		printf("%s", msg);
		linkedlist_forward(lister->warning_queue);
	}
	linkedlist_start(lister->error_queue);
	while (msg = (char *)linkedlist_get_current(lister->error_queue)) {
		printf("%s", msg);
		linkedlist_forward(lister->error_queue);
	}
}

Lister *lister_create(const char *filename) {
	Lister *temp = malloc(sizeof(Lister));
	temp->out_file = fopen(filename, "w");
	if (!temp->out_file) {
		fprintf(stderr, "could not create listing file\n");
		abort();
	}
	fprintf(temp->out_file, "1 "); // line number
	temp->line_num = 1;
	temp->warning_queue = linkedlist_create();
	temp->error_queue = linkedlist_create();
	return temp;
}

void lister_close(Lister *lstr) {
	fprintf(lstr->out_file, "\n");
	char *msg;
	while ((msg = (char *)linkedlist_pop_head(lstr->warning_queue)) != NULL) {
		fprintf(lstr->out_file, msg);
		free(msg);
	}
	while ((msg = (char *)linkedlist_pop_head(lstr->error_queue)) != NULL) {
		fprintf(lstr->out_file, msg);
		free(msg);
	}
	// todo: memory leak on the error messages when there are errors
	fclose(lstr->out_file);
	linkedlist_free(lstr->warning_queue);
	linkedlist_free(lstr->error_queue);
	free(lstr);
}

void lister_write(Lister *lstr, char ch) {
	fputc((unsigned char) ch, (*lstr).out_file);
	if (ch == '\n') {
		fprintf(lstr->out_file, "%d ", ++(lstr->line_num));
	}
	// if you wanted listing file to have inline errors, here is where they would be printed
}

int int_len(u16 num) {
    if (num == 0) return 1;

    int length = 0;
    int n = num;

    do {
        length++;
        n /= 10;
    } while (n != 0);

    return length;
}

void lister_lex_warn(Lister *lstr, u16 row, u16 col, char *msg) {
	char *warning = malloc(23 + int_len(row) + int_len(col) + strlen(msg));
	sprintf(warning, "Lexical warning (%d:%d): %s\n", row, col, msg);
	linkedlist_push_tail(lstr->warning_queue, warning);
}

void lister_lex_error(Lister *lstr, u16 row, u16 col, char *msg) {
	char *error = malloc(21 + int_len(row) + int_len(col) + strlen(msg));
	sprintf(error, "Lexical error (%d:%d): %s\n", row, col, msg);
	linkedlist_push_tail(lstr->error_queue, error);
}

void lister_syn_warn(Lister *lstr, u16 row, u16 col, char *msg) {
	char *warning = malloc(22 + int_len(row) + int_len(col) + strlen(msg));
	sprintf(warning, "Syntax warning (%d:%d): %s\n", row, col, msg);
	linkedlist_push_tail(lstr->warning_queue, warning);
}

void lister_syn_error(Lister *lstr, u16 row, u16 col, char *msg) {
	char *error = malloc(20 + int_len(row) + int_len(col) + strlen(msg));
	sprintf(error, "Syntax error (%d:%d): %s\n", row, col, msg);
	linkedlist_push_tail(lstr->error_queue, error);
}

void lister_sem_error(Lister *lstr, u16 row, u16 col, char *msg) {
	char *error = malloc(22 + int_len(row) + int_len(col) + strlen(msg));
	sprintf(error, "Semantic error (%d:%d): %s\n", row, col, msg);
	linkedlist_push_tail(lstr->error_queue, error);
}

