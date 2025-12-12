/*
  Takes a CD25 file and provides a token iterator
*/
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
static char* strdup(const char* s) {
	size_t len = strlen(s) + 1;
	char* dup = malloc(len);
	if (dup) {
		memcpy(dup, s, len);
	}
	return dup;
}

#include "lexer.h"
#include "token.h"
#include "lister.h"
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>

// std::unordered_map<std::pair<int, char>, int> transitions = std::unordered_map<>();
// TODO: refactor my FSM to use the above
// this'll slim down transitions, after which it should be easier to further minimise logic (e.g. buffer += ch, lex->buffer = "" branches)

/* hash:  form hash value for string s */
static unsigned hash_str(const void *ptr) {
	char *s = (char *) ptr;
	unsigned hashval;
	for (hashval = 0; *s != '\0'; s++)
		hashval = *s + 31 * hashval;
	return hashval;
}
static int equal_str(const void *a, const void *b) {
	return strcmp((char *)a, (char *)b) == 0;
}

void *heap_wrap(int type) {
    int *ptr = malloc(sizeof(int));
    *ptr = type;
    return ptr;
}

void populate_keywords(HashMap *keywords) {
	// initialising the keyword table (converting to lowercase is done upon access)
	hashmap_add(keywords, strdup("cd25"), heap_wrap(TCD25)); //CD25
	hashmap_add(keywords, strdup("constants"), heap_wrap(TCONS));
	hashmap_add(keywords, strdup("types"), heap_wrap(TTYPS));
	hashmap_add(keywords, strdup("is"), heap_wrap(TTTIS));
	hashmap_add(keywords, strdup("arrays"), heap_wrap(TARRS));
	hashmap_add(keywords, strdup("main"), heap_wrap(TMAIN));
	hashmap_add(keywords, strdup("begin"), heap_wrap(TBEGN));
	hashmap_add(keywords, strdup("end"), heap_wrap(TTEND));
	hashmap_add(keywords, strdup("array"), heap_wrap(TARAY));
	hashmap_add(keywords, strdup("of"), heap_wrap(TTTOF));
	hashmap_add(keywords, strdup("func"), heap_wrap(TFUNC));
	hashmap_add(keywords, strdup("void"), heap_wrap(TVOID));
	hashmap_add(keywords, strdup("const"), heap_wrap(TCNST));
	hashmap_add(keywords, strdup("integer"), heap_wrap(TINTG));
	hashmap_add(keywords, strdup("real"), heap_wrap(TREAL));
	hashmap_add(keywords, strdup("boolean"), heap_wrap(TBOOL));
	hashmap_add(keywords, strdup("for"), heap_wrap(TTFOR));
	hashmap_add(keywords, strdup("repeat"), heap_wrap(TREPT));
	hashmap_add(keywords, strdup("until"), heap_wrap(TUNTL));
	hashmap_add(keywords, strdup("if"), heap_wrap(TIFTH));
	hashmap_add(keywords, strdup("else"), heap_wrap(TELSE));
	hashmap_add(keywords, strdup("in"), heap_wrap(TINPT)); //In
	hashmap_add(keywords, strdup("out"), heap_wrap(TOUTP)); //Out
	hashmap_add(keywords, strdup("line"), heap_wrap(TOUTL)); //Line
	hashmap_add(keywords, strdup("return"), heap_wrap(TRETN));
	hashmap_add(keywords, strdup("not"), heap_wrap(TNOTT));
	hashmap_add(keywords, strdup("and"), heap_wrap(TTAND));
	hashmap_add(keywords, strdup("or"), heap_wrap(TTTOR));
	hashmap_add(keywords, strdup("xor"), heap_wrap(TTXOR));
	hashmap_add(keywords, strdup("true"), heap_wrap(TTRUE));
	hashmap_add(keywords, strdup("false"), heap_wrap(TFALS));
}

// the lexer is stack allocated
Lexer *lexer_create(const char *source_path, Lister *lister) {
	Lexer *temp = malloc(sizeof(Lexer));
	temp->lister = lister;
	temp->tokens = linkedlist_create();
	temp->source_file = fopen(source_path, "r");
	if (!temp->source_file) {
		fprintf(stderr, "could not open source file\n");
		free(temp);
		abort();
	}
	temp->state = START;
	temp->row = temp->col = 1;
	temp->buffer = sdsempty(); //todo: figure out how specifying size works
	temp->keyword_map = hashmap_create(100, hash_str, equal_str);
	populate_keywords(temp->keyword_map);
	return temp;
}

void lexer_free(Lexer *lex) {
	fclose(lex->source_file);
	linkedlist_free(lex->tokens);
	sdsfree(lex->buffer);
	hashmap_free(lex->keyword_map, free, free);
	free(lex);
}

// helper function, no other module should be generating tokens
Token *make_token(enum token_type type, char* val, int row, int col) {
	Token *temp = malloc(sizeof(Token));
	temp->type = type;
	temp->val = val;
	temp->row = row;
	temp->col = col;
	return temp;
}

// insert a sds into a cstr
// what does it mean?
char *format_cstr(const char *format, const char *str) {
    // Calculate required size
    int needed = snprintf(NULL, 0, format, str);
    if (needed < 0) return NULL;
    // Allocate and format
    char *msg = malloc(needed + 1);
    if (!msg) return NULL;
    snprintf(msg, needed + 1, format, str);
    return msg;
}
// char* format_cstr(const char *format, const sds str) {
// 	char* msg;
// 	vsprintf(&msg, format, str);
// 	return msg;
// }

void lexer_handle_capitalisation_warnings(Lexer *lex, const sds lexeme) {
	sds lowercase = sdsdup(lexeme);
	sdstolower(lowercase);
	if (hashmap_contains(lex->keyword_map, lowercase)) {
		//TODO: add value to the hashmap and use a switch here
		if (strcmp(lowercase, "cd25") == 0) {
			if (strcmp(lexeme, "CD25") != 0) {
				lister_lex_warn(lex->lister, lex->row, lex->col-sdslen(lexeme), "proper capitalisation is CD25");
			}
		} else if (strcmp(lowercase, "in") == 0) {
			if (strcmp(lexeme, "In") != 0) {
				lister_lex_warn(lex->lister, lex->row, lex->col-sdslen(lexeme), "proper capitalisation is In");
			}
		} else if (strcmp(lowercase, "out") == 0) {
			if (strcmp(lexeme, "Out") != 0) {
				lister_lex_warn(lex->lister, lex->row, lex->col-sdslen(lexeme), "proper capitalisation is Out");
			}
		} else if (strcmp(lowercase, "line") == 0) {
			if (strcmp(lexeme, "Line") != 0) {
				lister_lex_warn(lex->lister, lex->row, lex->col-sdslen(lexeme), "proper capitalisation is Line");
			}
		} else if (strcmp(lexeme, lowercase) != 0) {
			lister_lex_warn(lex->lister, lex->row, lex->col-sdslen(lexeme),
				format_cstr("leave %s in lowercase", lowercase)
			);
		}
	}
	sdsfree(lowercase);
}

// characters that are always exactly one token to themselves
// warning: implict enum values are used here because -1 isn't a token
int lone_operator(char ch) {
	switch (ch) {
		case '.': // this result is invalid within the int state, but fine elsewhere
			return TDOTT;
		case ',':
			return TCOMA;
		case '[':
			return TLBRK;
		case ']':
			return TRBRK;
		case '(':
			return TLPAR;
		case ')':
			return TRPAR;
		case '%':
			return TPERC;
		case '^':
			return TCART;
		case ':':
			return TCOLN;
		case ';':
			return TSEMI;
		default:
			return -1; // is not a lone operator (would not work if the enum moves from int to an alphabetical type)
	}
}
// quickly and concisely check if a char is valid outside of comment/string
int valid_char(char ch) {
	if (isalpha(ch)) { return true; }
	if (isdigit(ch)) { return true; }
	switch (ch) {
		case ',':
		case ';':
		case '[':
		case ']':
		case '(':
		case ')':
		case '=':
		case '+':
		case '-':
		case '*':
		case '/':
		case '%':
		case '^':
		case '<':
		case '>':
		case ':':
		case '.':
		case '!': // future chars are not considered
		case ' ': //space
		case '	': //tab
		case '\r': // carriage return 🤮
		case '\n':
			return true;
		default:
			return false;
	}
}

static sds buffer_append(sds buf, char c) {
    if (sdsavail(buf) < 1) {
        buf = sdsMakeRoomFor(buf, 1);
    }
	char temp[2] = {c, '\0'};
    buf = sdscatlen(buf, temp, 1);
    return buf;
}

// char *strip_sds_header(sds s) {
//     size_t len = sdslen(s);
//     char *result = malloc(len + 1);  // Allocate new memory
//     if (!result) return NULL;
//     memcpy(result, s, len + 1);     // Copy string content
//     sdsfree(s);                     // Free the original SDS properly
//     return result;
// }
// char *strip_sds_header(sds s) {
//     char *result = realloc(s, sdslen(s) + 1);
//     if (!result) return NULL; //redundant?
//     return result;
// }

sds buffer_substr(sds buf, size_t start, size_t len) {
    if (start >= sdslen(buf)) return sdsempty();
    if (start + len > sdslen(buf)) len = sdslen(buf) - start;
    return sdsnewlen(buf + start, len);
}

Token *lexer_get_token(Lexer *lex) {
	if (!linkedlist_is_empty(lex->tokens)) {
		return linkedlist_pop_tail(lex->tokens);
	}

	int ch_int;
	while (linkedlist_is_empty(lex->tokens) && (ch_int = fgetc(lex->source_file)) != EOF) {
		char ch = (char) ch_int;
		// to not add the final \n before EOF to the listing
		int next = fgetc(lex->source_file);
		if (next != EOF) {
			ungetc(next, lex->source_file);
			lister_write(lex->lister, ch);
		}
		if (ch == '\t') {
			lex->col += 3; // this logic is here because I believe tab is 1 char
		}
		if (ch == '\r') { // dropping carriage returns (🤮)
			continue;
		}
		switch(lex->state) {
			case START: // start (nothing in buffer)
				start_state: // epsilon transitions
				if (ch == ' ' || ch == '	' || ch == '\n') {
					lex->col++;
					if (ch == '\n') {
						lex->row++;
						lex->col = 1;
					}
					continue;
				} else if (isdigit(ch)) { // [0-9]
					lex->state = NUM;
					lex->buffer = buffer_append(lex->buffer, ch);
				} else if (isalpha(ch)) { // [A-Za-z]
					lex->state = ALPHANUM;
					lex->buffer = buffer_append(lex->buffer, ch);
				} else if (lone_operator(ch) != -1) { // [.,[]()%^;:]
					linkedlist_push_head(lex->tokens, make_token(lone_operator(ch), sdsempty(), lex->row, lex->col));
				} else { // operator transitions
					switch (ch) {
						case '+':
						case '-':
						case '*':
						case '=':
							lex->state = PRE_EQUAL;
							lex->buffer = buffer_append(lex->buffer, ch);
							break;
						case '<':
							lex->state = LESS;
							lex->buffer = buffer_append(lex->buffer, ch);
							break;
						case '>':
							lex->state = GRTR;
							lex->buffer = buffer_append(lex->buffer, ch);
							break;
						case '!':
							lex->state = EXCLM;
							lex->buffer = buffer_append(lex->buffer, ch);
							break;
						case '"':
							lex->state = STRING;
							lex->buffer = buffer_append(lex->buffer, ch);
							break;
						case '/':
							lex->state = SLASH;
							lex->buffer = buffer_append(lex->buffer, ch);
							break;
						default:
							lex->state = ERROR; // if this line is reached - error
							lex->buffer = buffer_append(lex->buffer, ch);
							break;
					}
				}
				break;
			case NUM: // numeral
				if (isdigit(ch)) {
					lex->buffer = buffer_append(lex->buffer, ch);
				} else if (ch == '.') {
					lex->buffer = buffer_append(lex->buffer, ch);
					lex->state = NUMDOT;
				} else {
					errno = 0; // bounds check
					long long val = strtoll(lex->buffer, NULL, 10);
					if (errno != ERANGE) {
						linkedlist_push_head(lex->tokens, make_token(TILIT, sdsdup(lex->buffer), lex->row, lex->col-sdslen(lex->buffer)));
					} else {
						lister_lex_error(lex->lister, lex->row, lex->col-sdslen(lex->buffer), "integer literal cannot be converted to a long long");
						linkedlist_push_head(lex->tokens, make_token(TUNDF, sdsdup(lex->buffer), lex->row, lex->col-sdslen(lex->buffer)));
					}
					sdsclear(lex->buffer);
					lex->state = START;
					goto start_state;
				}
				break;
			case ALPHANUM: // alpha
				if (isalpha(ch) || isdigit(ch)) {
					lex->buffer = buffer_append(lex->buffer, ch);
				} else {
					//if != .to_lower(), else if != "CD25", else if != In,Out,Line
					lexer_handle_capitalisation_warnings(lex, lex->buffer);
					int token_type;
					sds token_val;
					char *lowercase_buffer = sdsdup(lex->buffer);
					sdstolower(lowercase_buffer);
					if (hashmap_contains(lex->keyword_map, lowercase_buffer)) {
						token_type = *(int *) hashmap_get(lex->keyword_map, lowercase_buffer);
						token_val = sdsempty();
					} else {
						token_type = TIDEN;
						token_val = sdsdup(lex->buffer);
					}
					linkedlist_push_head(lex->tokens, make_token(token_type, token_val, lex->row, lex->col-sdslen(lex->buffer)));
					sdsfree(lowercase_buffer);
					sdsclear(lex->buffer);
					lex->state = START;
					goto start_state;
				}
				break;
			case NUMDOT: // dot after numeral
				if (isdigit(ch)) { // match for [0-9]
					lex->state = FLOAT;
					lex->buffer = buffer_append(lex->buffer, ch);
				} else {
					// queue string until last char
					sdsrange(lex->buffer, 0, -2);
					errno = 0; // bounds check
					long long val = strtoll(lex->buffer, NULL, 10);
					if (errno != ERANGE) {
						// the extra -1 is due to some dot funny business? TODO: sort out how I interact with the buffer?
						linkedlist_push_head(lex->tokens, make_token(TILIT, sdsdup(lex->buffer), lex->row, lex->col-(sdslen(lex->buffer)+1)));
					} else {
						lister_lex_error(lex->lister, lex->row, lex->col-(sdslen(lex->buffer)+1), "integer literal cannot be converted to a long long");
						linkedlist_push_head(lex->tokens, make_token(TUNDF, sdsdup(lex->buffer), lex->row, lex->col-(sdslen(lex->buffer)+1)));
					}
					sdsclear(lex->buffer);
					linkedlist_push_head(lex->tokens, make_token(TDOTT, sdsempty(), lex->row, lex->col));
					lex->state = START;
					goto start_state;
				}
				break;
			case FLOAT: // real literal
				if (isdigit(ch)) {
					lex->buffer = buffer_append(lex->buffer, ch);
				} else {
					errno = 0; // bounds check
					double val = strtod(lex->buffer, NULL);
					if (errno != ERANGE) {
						linkedlist_push_head(lex->tokens, make_token(TFLIT, sdsdup(lex->buffer), lex->row, lex->col-sdslen(lex->buffer)));
					} else {
						lister_lex_error(lex->lister, lex->row, lex->col-sdslen(lex->buffer), "real literal cannot be converted to a double");
						linkedlist_push_head(lex->tokens, make_token(TUNDF, sdsdup(lex->buffer), lex->row, lex->col-sdslen(lex->buffer)));
					}
					sdsclear(lex->buffer);
					lex->state = START;
					goto start_state;
				}
				break;
			case PRE_EQUAL: // +-*=
				if (ch == '=') {
					switch (lex->buffer[0]) {
						case '+':
							linkedlist_push_head(lex->tokens, make_token(TPLEQ, sdsempty(), lex->row, lex->col-1));
							break;
						case '-':
							linkedlist_push_head(lex->tokens, make_token(TMNEQ, sdsempty(), lex->row, lex->col-1));
							break;
						case '*':
							linkedlist_push_head(lex->tokens, make_token(TSTEQ, sdsempty(), lex->row, lex->col-1));
							break;
						case '=':
							linkedlist_push_head(lex->tokens, make_token(TEQEQ, sdsempty(), lex->row, lex->col-1));
							break;
					}
					sdsclear(lex->buffer);
					lex->state = START;
				} else {
					switch (lex->buffer[0]) {
						case '+':
							linkedlist_push_head(lex->tokens, make_token(TPLUS, sdsempty(), lex->row, lex->col-1));
							break;
						case '-':
							linkedlist_push_head(lex->tokens, make_token(TMINS, sdsempty(), lex->row, lex->col-1));
							break;
						case '*':
							linkedlist_push_head(lex->tokens, make_token(TSTAR, sdsempty(), lex->row, lex->col-1));
							break;
						case '=':
							linkedlist_push_head(lex->tokens, make_token(TEQUL, sdsempty(), lex->row, lex->col-1));
							break;
					}
					sdsclear(lex->buffer);
					lex->state = START;
					goto start_state;
				}
				break;
			case LESS: // <
				if (ch == '<') {
					linkedlist_push_head(lex->tokens, make_token(TLSLS, sdsempty(), lex->row, lex->col-1));
					sdsclear(lex->buffer);
					lex->state = START;
				} else if (ch == '=') {
					linkedlist_push_head(lex->tokens, make_token(TLEQL, sdsempty(), lex->row, lex->col-1));
					sdsclear(lex->buffer);
					lex->state = START;
				} else {
					linkedlist_push_head(lex->tokens, make_token(TLESS, sdsempty(), lex->row, lex->col));
					sdsclear(lex->buffer);
					lex->state = START;
					goto start_state;
				}
				break;
			case GRTR: // >
				if (ch == '>') {
					linkedlist_push_head(lex->tokens, make_token(TGRGR, sdsempty(), lex->row, lex->col-1));
					sdsclear(lex->buffer);
					lex->state = START;
				} else if (ch == '=') {
					linkedlist_push_head(lex->tokens, make_token(TGEQL, sdsempty(), lex->row, lex->col-1));
					sdsclear(lex->buffer);
					lex->state = START;
				} else {
					linkedlist_push_head(lex->tokens, make_token(TGRTR, sdsempty(), lex->row, lex->col));
					sdsclear(lex->buffer);
					lex->state = START;
					goto start_state;
				}
				break;
			case EXCLM: // !
				if (ch == '=') {
					linkedlist_push_head(lex->tokens, make_token(TNEQL, sdsempty(), lex->row, lex->col-1));
					sdsclear(lex->buffer);
					lex->state = START;
				} else {
					if (!valid_char(ch) || ch == '!') {
						lex->state = ERROR;
						lex->buffer = buffer_append(lex->buffer, ch);
					} else {
						lister_lex_error(lex->lister, lex->row, lex->col-1, "! is only valid as part of !=");
						linkedlist_push_head(lex->tokens, make_token(TUNDF, sdsdup(lex->buffer), lex->row, lex->col-1));
						sdsclear(lex->buffer);
						lex->state = START;
						goto start_state;
					}
				}
				break;
			case STRING: // "  (string)
				if (ch == '"') {
					sds str_val = sdsdup(lex->buffer);
					sdsrange(str_val, 1, -1);
					linkedlist_push_head(lex->tokens, make_token(TSTRG, str_val, lex->row, lex->col-sdslen(lex->buffer)));
					sdsclear(lex->buffer);
					lex->state = START;
				} else if (ch == '\n') {
					lister_lex_error(lex->lister, lex->row, lex->col-sdslen(lex->buffer), "non-terminated string");
					linkedlist_push_head(lex->tokens, make_token(TUNDF, sdsdup(lex->buffer), lex->row, lex->col-sdslen(lex->buffer)));
					sdsclear(lex->buffer);
					lex->state = START;
				} else {
					lex->buffer = buffer_append(lex->buffer, ch);
				}
				break;
			case SLASH: // /
				if (ch == '=') {
					linkedlist_push_head(lex->tokens, make_token(TDVEQ, sdsempty(), lex->row, lex->col-1));
					sdsclear(lex->buffer);
					lex->state = START;
				} else if (ch == '*') {
					lex->buffer = buffer_append(lex->buffer, ch);
					lex->state = SLASHSTAR;
				} else if (ch == '-') {
					lex->buffer = buffer_append(lex->buffer, ch);
					lex->state = SLASHMINUS;
				} else {
					linkedlist_push_head(lex->tokens, make_token(TDIVD, sdsempty(), lex->row, lex->col-1));
					sdsclear(lex->buffer);
					lex->state = START;
					goto start_state;
				}
				break;
			case SLASHSTAR: // /*
				if (ch == '*') {
					sdsclear(lex->buffer);
					lex->state = ML_COM;
				} else {
					linkedlist_push_head(lex->tokens, make_token(TDIVD, sdsempty(), lex->row, lex->col-1));
					linkedlist_push_head(lex->tokens, make_token(TSTAR, sdsempty(), lex->row, lex->col));
					sdsclear(lex->buffer);
					lex->state = START;
					goto start_state;
				}
				break;
			case SLASHMINUS: // /-
				if (ch == '-') {
					sdsclear(lex->buffer);
					lex->state = SL_COM;
				} else {
					linkedlist_push_head(lex->tokens, make_token(TDIVD, sdsempty(), lex->row, lex->col-1));
					linkedlist_push_head(lex->tokens, make_token(TMINS, sdsempty(), lex->row, lex->col));
					sdsclear(lex->buffer);
					lex->state = START;
					goto start_state;
				}
				break;
			case ML_COM: // /** ...  multi line comment
				if (ch == '*') {
					lex->state = ML_COMSTR;
				}
				break;
			case ML_COMSTR: // /** ...  *
				if (ch == '*') {
					lex->state = ML_COMSTRSTR;
				} else {
					lex->state = ML_COM;
				}
				break;
			case ML_COMSTRSTR: // /** ... **
				if (ch == '/') {
					lex->state = START;
				} else {
					lex->state = ML_COM;
				}
				break;
			case SL_COM: // single line comment
				if (ch == '\n') {
					lex->state = START;
				}
				break;
			case ERROR: // error state
				if (valid_char(ch)) { // TODO: edgecase where !x would produce an extra TUNDF because ! is valid but only before =
					lister_lex_error(lex->lister, lex->row, lex->col-sdslen(lex->buffer),
							format_cstr("unknown characters (%s)", lex->buffer)
					);
					linkedlist_push_head(lex->tokens, make_token(TUNDF, sdsdup(lex->buffer), lex->row, lex->col - sdslen(lex->buffer)));
					lex->state = START;
					sdsclear(lex->buffer);
					goto start_state;
				} else {
					lex->buffer = buffer_append(lex->buffer, ch);
				}
				break;
		}
		if (ch == '\n') {
			lex->row++;
			lex->col = 1;
		} else {
			lex->col++;
		}
	}
	if (!linkedlist_is_empty(lex->tokens)) {
		return linkedlist_pop_tail(lex->tokens);
	} else {
		if (sdslen(lex->buffer) != 0) { // if the buffer has material, it's the program name
			Token *progname = make_token(TIDEN, sdsdup(lex->buffer), lex->row, lex->col-sdslen(lex->buffer));
			sdsclear(lex->buffer);
			return progname;
		}
		return make_token(T_EOF, sdsempty(), lex->row, lex->col);
	}
}

