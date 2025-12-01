/*
  Is essentially a read-only tuple the Lexer passes to the Parser
*/

#ifndef TOKEN_H
#define TOKEN_H

#include "lib/sds.h"

enum token_type {
	// Token value for end of file
	T_EOF,
	// The 31 keywords
	TCD25, TCONS, TTYPS, TTTIS, TARRS, TMAIN,
	TBEGN, TTEND, TARAY, TTTOF, TFUNC, TVOID,
	TCNST, TINTG, TREAL, TBOOL, TTFOR, TREPT,
	TUNTL, TIFTH, TELSE, TINPT, TOUTP, TOUTL,
	TRETN, TNOTT, TTAND, TTTOR, TTXOR, TTRUE,
	TFALS,
	// the operators and delimiters
	TCOMA, TLBRK, TRBRK, TLPAR, TRPAR, TEQUL,
	TPLUS, TMINS, TSTAR, TDIVD, TPERC, TCART,
	TLESS, TGRTR, TCOLN, TLEQL, TGEQL, TNEQL,
	TEQEQ, TPLEQ, TMNEQ, TSTEQ, TDVEQ, TSEMI,
	TDOTT, TGRGR, TLSLS,
	// the tokens which need tuple values
	TIDEN, TILIT, TFLIT, TSTRG, TUNDF,
	// to represent the absence of a token [transition]
	NULLTOKEN
};

typedef struct token {
	enum token_type type;
	char* val;
	int row;
	int col;
} Token;

// instead of 64, add a TOKEN_COUNT to the enum?
static char TPRINT[64][7] = {
	"T_EOF ",
	"TCD25 ","TCONS ","TTYPS ","TTTIS ","TARRS ","TMAIN ",
	"TBEGN ","TTEND ","TARAY ","TTTOF ","TFUNC ","TVOID ",
	"TCNST ","TINTG ","TREAL ","TBOOL ","TTFOR ","TREPT ",
	"TUNTL ","TIFTH ","TELSE ","TINPT ","TOUTP ","TOUTL ",
	"TRETN ","TNOTT ","TTAND ","TTTOR ","TTXOR ","TTRUE ",
	"TFALS ",
	"TCOMA ","TLBRK ","TRBRK ","TLPAR ","TRPAR ","TEQUL ",
	"TPLUS ","TMINS ","TSTAR ","TDIVD ","TPERC ","TCART ",
	"TLESS ","TGRTR ","TCOLN ","TLEQL ","TGEQL ","TNEQL ",
	"TEQEQ ","TPLEQ ","TMNEQ ","TSTEQ ","TDVEQ ","TSEMI ",
	"TDOTT ","TGRGR ","TLSLS ",
	"TIDEN ","TILIT ","TFLIT ","TSTRG ","TUNDF "
};

// void token_free(Token *t);

#endif

