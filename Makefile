CC ?= cc
CFLAGS = -Wall -W -Os -g

# Source files
UTF8_SRC = src/utf8.c
TERMINAL_SRC = src/terminal_posix.c
LINENOISE_SRC = linenoise.c

# Object files
UTF8_OBJ = src/utf8.o
TERMINAL_OBJ = src/terminal_posix.o

all: linenoise-example linenoise-test

# Compile UTF-8 module
$(UTF8_OBJ): $(UTF8_SRC) internal/utf8.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Compile terminal module
$(TERMINAL_OBJ): $(TERMINAL_SRC) internal/terminal.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Main example application
linenoise-example: linenoise.h linenoise.c example.c $(UTF8_OBJ) internal/utf8.h
	$(CC) $(CFLAGS) -o linenoise-example linenoise.c example.c $(UTF8_OBJ)

# Test suite (uses shared UTF-8 module)
linenoise-test: linenoise-test.c linenoise-example $(UTF8_OBJ) internal/utf8.h
	$(CC) $(CFLAGS) -o linenoise-test linenoise-test.c $(UTF8_OBJ)

test: linenoise-test linenoise-example
	./linenoise-test

clean:
	rm -f linenoise-example linenoise-test $(UTF8_OBJ) $(TERMINAL_OBJ)

.PHONY: all test clean
