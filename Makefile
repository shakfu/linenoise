CC ?= cc
CFLAGS = -Wall -W -Os -g

# Source files
UTF8_SRC = src/utf8.c
TERMINAL_SRC = src/terminal_posix.c
HISTORY_SRC = src/history.c
COMPLETION_SRC = src/completion.c
KEYPARSER_SRC = src/keyparser.c
RENDER_SRC = src/render.c
LINENOISE_SRC = linenoise.c

# Object files
UTF8_OBJ = src/utf8.o
TERMINAL_OBJ = src/terminal_posix.o
HISTORY_OBJ = src/history.o
COMPLETION_OBJ = src/completion.o
KEYPARSER_OBJ = src/keyparser.o
RENDER_OBJ = src/render.o

# All module objects (for linking)
MODULE_OBJS = $(UTF8_OBJ) $(HISTORY_OBJ) $(COMPLETION_OBJ) $(KEYPARSER_OBJ) $(RENDER_OBJ)

all: linenoise-example linenoise-test

# Compile UTF-8 module
$(UTF8_OBJ): $(UTF8_SRC) internal/utf8.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Compile terminal module
$(TERMINAL_OBJ): $(TERMINAL_SRC) internal/terminal.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Compile history module
$(HISTORY_OBJ): $(HISTORY_SRC) internal/history.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Compile completion module
$(COMPLETION_OBJ): $(COMPLETION_SRC) internal/completion.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Compile key parser module
$(KEYPARSER_OBJ): $(KEYPARSER_SRC) internal/keyparser.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Compile render module
$(RENDER_OBJ): $(RENDER_SRC) internal/render.h internal/utf8.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Main example application
linenoise-example: linenoise.h linenoise.c example.c $(UTF8_OBJ) internal/utf8.h
	$(CC) $(CFLAGS) -o linenoise-example linenoise.c example.c $(UTF8_OBJ)

# Test suite (uses shared UTF-8 module)
linenoise-test: linenoise-test.c linenoise-example $(UTF8_OBJ) internal/utf8.h
	$(CC) $(CFLAGS) -o linenoise-test linenoise-test.c $(UTF8_OBJ)

# Build all modules (for verification)
modules: $(MODULE_OBJS)

test: linenoise-test linenoise-example
	./linenoise-test

clean:
	rm -f linenoise-example linenoise-test $(UTF8_OBJ) $(TERMINAL_OBJ) $(HISTORY_OBJ) $(COMPLETION_OBJ) $(KEYPARSER_OBJ) $(RENDER_OBJ)

.PHONY: all test clean modules
