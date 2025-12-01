// provides an interface to the listing file

#ifndef LISTER_H
#define LISTER_H

#include <stdio.h>
#include <string.h>
#include "lib/defs.h"
#include "lib/linkedlist.h"

typedef struct lister {
	FILE *out_file;
	u16 line_num;
	LinkedList *warning_queue;
	LinkedList *error_queue;
} Lister;

Lister *lister_create(const char *filename);
void lister_close(Lister *lst);

void lister_write(Lister *lst, char ch);
void lister_lex_warn(Lister *lst, u16 row, u16 col, char *msg);
void lister_lex_error(Lister *lst, u16 row, u16 col, char *msg);

void lister_syn_warn(Lister *lst, u16 row, u16 col, char *msg);
void lister_syn_error(Lister *lst, u16 row, u16 col, char *msg);

void lister_sem_error(Lister *lst, u16 row, u16 col, char *msg);

void lister_print_to_terminal(Lister *lister);

#endif

