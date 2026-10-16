/* example-lua.c -- Lua REPL example with tree-sitter syntax highlighting
 *
 * This example demonstrates the linenoise library with tree-sitter based
 * Lua syntax highlighting. It provides a simple Lua-like REPL interface.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "linenoise.h"
#include "syntax/lua.h"

/* Lua keyword completions */
static const char *lua_keywords[] = {
    "and", "break", "do", "else", "elseif", "end", "false", "for",
    "function", "goto", "if", "in", "local", "nil", "not", "or",
    "repeat", "return", "then", "true", "until", "while", NULL
};

/* Lua built-in functions */
static const char *lua_builtins[] = {
    "assert", "collectgarbage", "dofile", "error", "getmetatable",
    "ipairs", "load", "loadfile", "next", "pairs", "pcall", "print",
    "rawequal", "rawget", "rawlen", "rawset", "require", "select",
    "setmetatable", "tonumber", "tostring", "type", "xpcall", NULL
};

/* Complete the identifier before the cursor. Only that word is replaced. */
void completion(const char *buf, size_t pos, linenoise_completions_t *lc,
                void *udata) {
    const char **lists[] = { lua_keywords, lua_builtins };
    size_t start = pos;
    (void)udata;

    while (start > 0 && (buf[start-1] == '_' ||
           isalnum((unsigned char)buf[start-1]))) {
        start--;
    }
    if (start == pos) return;
    lc->start = start;

    for (size_t i = 0; i < sizeof(lists)/sizeof(lists[0]); i++) {
        for (const char **w = lists[i]; *w; w++) {
            if (strncmp(buf + start, *w, pos - start) == 0)
                linenoise_add_completion(lc, *w);
        }
    }
}

char *hints(const char *buf, size_t pos, int *color, int *bold, void *udata) {
    (void)pos;
    (void)udata;
    /* Show hints for common Lua constructs */
    if (strcmp(buf, "function") == 0) {
        *color = 90; /* Dark gray */
        *bold = 0;
        return " name(args) ... end";
    }
    if (strcmp(buf, "if") == 0) {
        *color = 90;
        *bold = 0;
        return " condition then ... end";
    }
    if (strcmp(buf, "for") == 0) {
        *color = 90;
        *bold = 0;
        return " var = start, end do ... end";
    }
    if (strcmp(buf, "while") == 0) {
        *color = 90;
        *bold = 0;
        return " condition do ... end";
    }
    if (strcmp(buf, "local") == 0) {
        *color = 90;
        *bold = 0;
        return " name = value";
    }
    if (strcmp(buf, "print") == 0) {
        *color = 90;
        *bold = 0;
        return "(...)";
    }
    return NULL;
}

int main(int argc, char **argv) {
    char *line;
    char *prgname = argv[0];
    int multiline = 0;

    /* Parse options */
    while (argc > 1) {
        argc--;
        argv++;
        if (strcmp(*argv, "--multiline") == 0) {
            multiline = 1;
            printf("Multi-line mode enabled.\n");
        } else if (strcmp(*argv, "--help") == 0) {
            printf("Usage: %s [--multiline] [--help]\n", prgname);
            printf("\nA Lua REPL with tree-sitter syntax highlighting.\n");
            printf("\nOptions:\n");
            printf("  --multiline  Enable multi-line editing mode\n");
            printf("  --help       Show this help message\n");
            printf("\nColors:\n");
            printf("  Keywords     - Magenta (bold)\n");
            printf("  Strings      - Green\n");
            printf("  Numbers      - Yellow\n");
            printf("  Comments     - Cyan\n");
            printf("  Functions    - Blue (bold)\n");
            printf("  Booleans/nil - Yellow (bold)\n");
            exit(0);
        } else {
            fprintf(stderr, "Usage: %s [--multiline] [--help]\n", prgname);
            exit(1);
        }
    }

    /* Initialize the Lua highlighter */
    if (lua_highlight_init() != 0) {
        fprintf(stderr, "Failed to initialize Lua highlighter\n");
        exit(1);
    }

    /* Create linenoise context */
    linenoise_context_t *ctx = linenoise_context_create();
    if (!ctx) {
        fprintf(stderr, "Failed to create linenoise context\n");
        lua_highlight_free();
        exit(1);
    }

    /* Configure the context */
    if (multiline) {
        linenoise_set_multiline(ctx, 1);
    }

    /* Set callbacks */
    linenoise_set_completion_callback(ctx, completion, NULL);
    linenoise_set_hints_callback(ctx, hints, NULL);
    linenoise_set_highlight_callback(ctx, lua_highlight_callback, NULL);

    /* Load history */
    linenoise_history_load(ctx, ".lua_history");

    printf("Lua REPL with tree-sitter syntax highlighting\n");
    printf("Type Lua code to see syntax highlighting. Press Ctrl+D to exit.\n");
    printf("Try: local x = 123  -- or --  function foo() return \"hello\" end\n\n");

    /* Main loop */
    while ((line = linenoise_read(ctx, "lua> ")) != NULL) {
        if (line[0] != '\0') {
            printf(">> %s\n", line);
            linenoise_history_add(ctx, line);
            linenoise_history_save(ctx, ".lua_history");
        }
        linenoise_free(line);
    }

    printf("\nGoodbye!\n");

    /* Cleanup */
    linenoise_context_destroy(ctx);
    lua_highlight_free();

    return 0;
}
