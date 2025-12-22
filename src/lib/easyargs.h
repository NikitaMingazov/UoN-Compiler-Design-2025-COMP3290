#ifndef EASYARGS_H
#define EASYARGS_H
/*
 * TODO:
 * enum type
 * bools should be single letter and can be parsed as a block
 * variadic args
*/

/*
	EasyArgs: A simple, single-header argument parser for C
	Version: October 20, 2025
	Author: Xander Gouws

	Provided under an MIT License. See end of file for details.

	See github.com/gouwsxander/easy-args for documentation and examples.
*/

#include <stdio.h>
#include <stdlib.h>  // used for parsing (atoi, atof)


// REQUIRED_ARG(type, name, label, description, parser, index)
// label and description should be strings, e.g. "contrast" and "Contrast applied to image"
#define REQUIRED_STRING_ARG(name, label, description) REQUIRED_ARG(char*, name, label, description, , __COUNTER__)
#define REQUIRED_CHAR_ARG(name, label, description) REQUIRED_ARG(char, name, label, description, parse_char, __COUNTER__)
#define REQUIRED_INT_ARG(name, label, description) REQUIRED_ARG(int, name, label, description, atoi, __COUNTER__)
#define REQUIRED_UINT_ARG(name, label, description) REQUIRED_ARG(unsigned int, name, label, description, atoi, __COUNTER__)
#define REQUIRED_LONG_ARG(name, label, description) REQUIRED_ARG(long, name, label, description, atol, __COUNTER__)
#define REQUIRED_ULONG_ARG(name, label, description) REQUIRED_ARG(unsigned long, name, label, description, parse_ul, __COUNTER__)
#define REQUIRED_LONG_LONG_ARG(name, label, description) REQUIRED_ARG(long long, name, label, description, atoll, __COUNTER__)
#define REQUIRED_ULONG_LONG_ARG(name, label, description) REQUIRED_ARG(unsigned long long, name, label, description, parse_ull, __COUNTER__)
#define REQUIRED_SIZE_ARG(name, label, description) REQUIRED_ARG(size_t, name, label, description, parse_ull, __COUNTER__)
#define REQUIRED_FLOAT_ARG(name, label, description) REQUIRED_ARG(float, name, label, description, atof, __COUNTER__)
#define REQUIRED_DOUBLE_ARG(name, label, description) REQUIRED_ARG(double, name, label, description, atof, __COUNTER__)

// OPTIONAL_ARG(type, name, default, flag, label, description, formatter, parser)
#define OPTIONAL_STRING_ARG(name, default, flag, label, description) OPTIONAL_ARG(char*, name, default, flag, label, description, "%s", )
#define OPTIONAL_CHAR_ARG(name, default, flag, label, description) OPTIONAL_ARG(char, name, default, flag, label, description, "%c", parse_char)
#define OPTIONAL_INT_ARG(name, default, flag, label, description) OPTIONAL_ARG(int, name, default, flag, label, description, "%d", atoi)
#define OPTIONAL_UINT_ARG(name, default, flag, label, description) OPTIONAL_ARG(unsigned int, name, default, flag, label, description, "%u", atoi)
#define OPTIONAL_LONG_ARG(name, default, flag, label, description) OPTIONAL_ARG(long, name, default, flag, label, description, "%ld", atol)
#define OPTIONAL_ULONG_ARG(name, default, flag, label, description) OPTIONAL_ARG(unsigned long, name, default, flag, label, description, "%lu", parse_ul)
#define OPTIONAL_LONG_LONG_ARG(name, default, flag, label, description) OPTIONAL_ARG(long long, name, default, flag, label, description, "%lld", atoll)
#define OPTIONAL_ULONG_LONG_ARG(name, default, flag, label, description) OPTIONAL_ARG(unsigned long long, name, default, flag, label, description, "%llu", parse_ull)
#define OPTIONAL_SIZE_ARG(name, default, flag, label, description) OPTIONAL_ARG(size_t, name, default, flag, label, description, "%zu", parse_ull)
#define OPTIONAL_FLOAT_ARG(name, default, flag, label, description, precision) OPTIONAL_ARG(float, name, default, flag, label, description, "%." #precision "g", atof)
#define OPTIONAL_DOUBLE_ARG(name, default, flag, label, description, precision) OPTIONAL_ARG(double, name, default, flag, label, description, "%." #precision "g", atof)

// BOOLEAN_ARG(name, flag, description)

// PARSERS
static inline char parse_char(const char* text) {
	return text[0];
}

static inline unsigned long parse_ul(const char* text) {
	return strtoul(text, NULL, 10);
}

static inline unsigned long long parse_ull(const char* text) {
	return strtoull(text, NULL, 10);
}

static inline int easyargs_strlen(char *str) {
	int len = 0;
	int i = 0;
	char curr_char = str[i];

	while (curr_char != '\0') {
		len++;
		curr_char = str[++i];
	}

	return len;
}

// Check if two string are equal
static inline int easyargs_streql(char *str1, char *str2) {
	int str1_len = easyargs_strlen(str1);
	int str2_len = easyargs_strlen(str2);

	if (str1_len != str2_len) {
		return 1;
	}

	for (int i=0; i<str1_len; i++) {
		char str1_char = str1[i];
		char str2_char = str2[i];

		if (str1_char != str2_char) {
			return 1;
		}
	}

	return 0;
}

static inline int easyargs_str_starts_with(char *haystack, char *needle) {
	for (int i=0; needle[i] != '\0'; i++) {
		if (haystack[i] == '\0' || haystack[i] != needle[i]) {
			return 0;
		}
	}

	return 1;
}

// COUNT ARGUMENTS
#ifdef REQUIRED_ARGS
#define REQUIRED_ARG(...) + 1
static const int REQUIRED_ARG_COUNT = 0 REQUIRED_ARGS;
#undef REQUIRED_ARG
#else
static const int REQUIRED_ARG_COUNT = 0;
#endif

#ifdef OPTIONAL_ARGS
#define OPTIONAL_ARG(...) + 1
static const int OPTIONAL_ARG_COUNT = 0 OPTIONAL_ARGS;
#undef OPTIONAL_ARG
#else
static const int OPTIONAL_ARG_COUNT = 0;
#endif

#ifdef BOOLEAN_ARGS
#define BOOLEAN_ARG(...) + 1
static const int BOOLEAN_ARG_COUNT = 0 BOOLEAN_ARGS;
#undef BOOLEAN_ARG
#else
static const int BOOLEAN_ARG_COUNT = 0;
#endif


// ARG_T STRUCT
#define REQUIRED_ARG(type, name, ...) type name;
#define OPTIONAL_ARG(type, name, ...) type name;
#define BOOLEAN_ARG(name, ...) int name;
// Stores argument values
typedef struct {
	#ifdef REQUIRED_ARGS
	REQUIRED_ARGS
	#endif
	#ifdef OPTIONAL_ARGS
	OPTIONAL_ARGS
	#endif
	#ifdef BOOLEAN_ARGS
	BOOLEAN_ARGS
	#endif
} args_t;
#undef REQUIRED_ARG
#undef OPTIONAL_ARG
#undef BOOLEAN_ARG


// Build an args_t struct with assigned default values
static inline args_t make_default_args() {
	args_t args = {
		#define REQUIRED_ARG(type, name, ...) .name = (type) 0,
		#define OPTIONAL_ARG(type, name, default, ...) .name = default,
		#define BOOLEAN_ARG(name, ...) .name = 0,

		#ifdef REQUIRED_ARGS
		REQUIRED_ARGS
		#endif

		#ifdef OPTIONAL_ARGS
		OPTIONAL_ARGS
		#endif

		#ifdef BOOLEAN_ARGS
		BOOLEAN_ARGS
		#endif

		#undef REQUIRED_ARG
		#undef OPTIONAL_ARG
		#undef BOOLEAN_ARG
	};

	return args;
}


// Parse arguments. Returns 0 if failed.
static inline int parse_args(int argc, char* argv[], args_t* args) {
	if (!argc || !argv) {
		fprintf(stderr, "Internal error: null args or argv.\n");
		return 1;
	}

	// If not enough required arguments
	if (argc < 1 + REQUIRED_ARG_COUNT) {
		/* fprintf(stderr, "Not all required arguments included.\n"); */
		return 1;
	}

	int required_args_parsed = 0;

	// Parse arguments
	for (int i=1; i<argc; i++) {
		char *arg = argv[i];

		#ifdef OPTIONAL_ARGS
		#define OPTIONAL_ARG(type, name, default, flag, label, description, formatter, parser) \
		if (!easyargs_streql(arg, flag)) { \
			if (i + 1 >= argc) { \
				fprintf(stderr, "Error: option '%s' requires a value.\n", flag); \
				return 0; \
			} \
			args->name = (type) parser(argv[++i]); \
			continue; \
		}

		OPTIONAL_ARGS
   
		#undef OPTIONAL_ARG
		#endif

		#ifdef BOOLEAN_ARGS
		// Get boolean arguments
		#define BOOLEAN_ARG(name, flag, description) \
		if (!easyargs_streql(argv[i], flag)) { \
			args->name = 1; \
			continue; \
		}

		BOOLEAN_ARGS

		#undef BOOLEAN_ARG
		#endif

		// Arg is a required arg
		#ifdef REQUIRED_ARGS
		#define REQUIRED_ARG(type, name, label, description, parser, index) case index: {args->name = (type) parser(arg); break;}
		switch (required_args_parsed++) {
			REQUIRED_ARGS
		}
		#undef REQUIRED_ARG
		#endif
	}

	// If not enough required arguments
	if (required_args_parsed < REQUIRED_ARG_COUNT) {
		/* fprintf(stderr, "Not all required arguments included.\n"); */
		return 1;
	}

	#undef OPTIONAL_ARG
	#undef BOOLEAN_ARG

	return 0;
}


// Display help string, given command used to launch program, e.g., argv[0]
static inline void print_help(char* exec_alias) {
	// USAGE SECTION
	printf("USAGE:\n");
	printf("	%s ", exec_alias);

	#ifdef REQUIRED_ARGS
	if (REQUIRED_ARG_COUNT > 0 && REQUIRED_ARG_COUNT <= 3) {
		#define REQUIRED_ARG(type, name, label, ...) "<" label "> "
		printf(REQUIRED_ARGS);
		#undef REQUIRED_ARG
	} else {
		printf("<ARGUMENTS> ");
	}
	#endif

	if (OPTIONAL_ARG_COUNT + BOOLEAN_ARG_COUNT <= 3) {
		#ifdef OPTIONAL_ARGS
		#define OPTIONAL_ARG(type, name, default, flag, label, ...) "[" flag " <" label ">" "] "
		printf(OPTIONAL_ARGS);
		#undef OPTIONAL_ARG
		#endif

		#ifdef BOOLEAN_ARGS
		#define BOOLEAN_ARG(name, flag, ...) "[" flag "] "
		printf(BOOLEAN_ARGS);
		#undef BOOLEAN_ARG
		#endif
	} else {
		printf("[OPTIONS]");
	}

	printf("\n\n");

	// Get maximum width of labels for spacing
	int max_width = 0;
	(void) max_width; // suppress unused variable warning
	#ifdef REQUIRED_ARGS
	#define REQUIRED_ARG(type, name, label, ...) \
		{ int len = easyargs_strlen(label) + 2; if (len > max_width) max_width = len; }
	REQUIRED_ARGS
	#undef REQUIRED_ARG
	#endif

	#ifdef OPTIONAL_ARGS
	#define OPTIONAL_ARG(type, name, default, flag, label, ...) \
		{ int len = easyargs_strlen(flag) + 1 + easyargs_strlen(label) + 2; if (len > max_width) max_width = len; }
	OPTIONAL_ARGS
	#undef OPTIONAL_ARG
	#endif

	#ifdef BOOLEAN_ARGS
	#define BOOLEAN_ARG(name, flag, ...) \
		{ int len = easyargs_strlen(flag); if (len > max_width) max_width = len; }
	BOOLEAN_ARGS
	#undef BOOLEAN_ARG
	#endif

	// ARGUMENTS SECTION
	#ifdef REQUIRED_ARGS
	printf("ARGUMENTS:\n");

	#define REQUIRED_ARG(type, name, label, description, ...) \
		printf("	<" label ">%*s	" description "\n", max_width - (int)easyargs_strlen(label) - 2, "");
	REQUIRED_ARGS
	#undef REQUIRED_ARG

	printf("\n");
	#endif

	#if defined(OPTIONAL_ARGS) || defined(BOOLEAN_ARGS)
	printf("OPTIONS:\n");

	#ifdef OPTIONAL_ARGS

	#define OPTIONAL_ARG(type, name, default, flag, label, description, formatter, ...) \
		printf("	" flag " <" label ">%*s	" description " (default: " formatter ")\n", max_width - (int)easyargs_strlen(label) - (int)easyargs_strlen(flag) - 3, "", default);
	OPTIONAL_ARGS
	#undef OPTIONAL_ARG
	#endif

	#ifdef BOOLEAN_ARGS
	#define BOOLEAN_ARG(name, flag, description) \
		printf("	" flag "%*s	" description "\n", max_width - (int)easyargs_strlen(flag), "");
	BOOLEAN_ARGS
	#undef BOOLEAN_ARG
	#endif

	#endif
}

#endif

/*
	MIT License

	Copyright (c) 2025 Xander Gouws

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in all
	copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
	SOFTWARE.
*/
