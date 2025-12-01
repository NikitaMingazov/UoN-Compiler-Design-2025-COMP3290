#ifndef PARSER_H
#define PARSER_H

#include "astree.h"
#include "lister.h"

// Parser *parser_create(const char *filename);
ASTree *get_AST(const char *filename, Lister *list_file);

#endif

