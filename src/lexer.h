#ifndef Lexer_H
#define Lexer_H

#include "lib/sds.h"
#include "lib/linkedlist.h"
#include "lib/hashmap.h"
#include "token.h"
#include "lister.h"

enum fsm_state {
	START, NUM, ALPHANUM, NUMDOT, FLOAT,
	PRE_EQUAL, LESS, GRTR, EXCLM, STRING,
	SLASH, SLASHSTAR, SLASHMINUS,
	ML_COM, ML_COMSTR, ML_COMSTRSTR,
	SL_COM,
	ERROR
};

typedef struct lexer {
	Lister *lister;
	LinkedList *tokens;
	FILE *source_file;
	enum fsm_state state;
	int row, col;
	sds buffer;
	HashMap *keyword_map;
} Lexer;

Lexer *lexer_create(const char *source_path, Lister *lister);
void lexer_free(Lexer *lex);

Token *lexer_get_token(Lexer *lex);

#endif

