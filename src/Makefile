CC = gcc #-fsanitize=undefined
CFLAGS = -std=c99 -lm -g -fmax-errors=1
WARNINGCONFIG = -Wall -Wextra -pedantic -Wno-switch
SM25_TARGET = sm25_codegen/sm25_code_generation.c
X86_TARGET = x86_codegen/x86_code_generation.c
FRONTEND = threeaddresscode.c semantic_analysis.c astree.c parser.c lexer.c lister.c
INCLUDES = lib/linkedlist.c lib/sds.c lib/hashmap.c
SOURCES = $(SM25_TARGET) $(X86_TARGET) main.c $(FRONTEND) $(INCLUDES)
OBJECTS = $(SOURCES:.c=.o)
TARGET = cd25c

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(WARNINGCONFIG) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	rm -f $(OBJECTS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean
