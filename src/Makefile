CC = gcc #-fsanitize=undefined
CFLAGS = -std=c99 -lm -g -fmax-errors=1
WARNINGCONFIG = -Wall -Wextra -pedantic
# WARNINGCONFIG = -Wall -Wextra -Wno-unused-function -Wno-unused-variable -Wno-switch #-pedantic
SOURCES = main.c code_generation.c semantic_analysis.c astree.c parser.c lexer.c lister.c lib/linkedlist.c lib/sds.c lib/hashmap.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = a3

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(WARNINGCONFIG) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	rm -f $(OBJECTS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean
