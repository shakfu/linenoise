/* example-syntax.c -- Multi-language syntax highlighting test
 *
 * A single executable to test syntax highlighting for all supported languages.
 * Usage: linenoise-syntax <language> [--theme <theme>]
 *
 * Supported languages: lua, python, scheme, haskell, forth, faust, chuck, markdown
 * Supported themes: monokai, dracula, solarized-dark, solarized-light,
 *                   gruvbox-dark, nord, one-dark, basic16
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "linenoise.h"
#include "syntax/theme.h"

/* Include all syntax highlighters */
#include "syntax/lua.h"
#include "syntax/python.h"
#include "syntax/scheme.h"
#include "syntax/haskell.h"
#include "syntax/forth.h"
#include "syntax/faust.h"
#include "syntax/chuck.h"
#include "syntax/markdown.h"

typedef int (*highlight_init_fn)(void);
typedef void (*highlight_free_fn)(void);
typedef void (*highlight_callback_fn)(const char *, char *, size_t);

typedef struct {
    const char *name;
    const char *prompt;
    const char *example;
    highlight_init_fn init;
    highlight_free_fn cleanup;
    highlight_callback_fn callback;
} language_t;

static const language_t languages[] = {
    {
        "lua",
        "lua> ",
        "local x = 123  -- comment\nfunction foo() return \"hello\" end",
        lua_highlight_init,
        lua_highlight_free,
        lua_highlight_callback
    },
    {
        "python",
        "py> ",
        "def foo(x):\n    return x * 2  # comment\nprint(\"hello\")",
        python_highlight_init,
        python_highlight_free,
        python_highlight_callback
    },
    {
        "scheme",
        "scm> ",
        "(define (factorial n)\n  (if (<= n 1) 1 (* n (factorial (- n 1)))))",
        scheme_highlight_init,
        scheme_highlight_free,
        scheme_highlight_callback
    },
    {
        "haskell",
        "hs> ",
        "factorial :: Int -> Int\nfactorial 0 = 1\nfactorial n = n * factorial (n - 1)",
        haskell_highlight_init,
        haskell_highlight_free,
        haskell_highlight_callback
    },
    {
        "forth",
        "forth> ",
        ": square ( n -- n^2 ) dup * ;\n5 square .",
        forth_highlight_init,
        forth_highlight_free,
        forth_highlight_callback
    },
    {
        "faust",
        "faust> ",
        "import(\"stdfaust.lib\");\nprocess = os.osc(440) * 0.5;",
        faust_highlight_init,
        faust_highlight_free,
        faust_highlight_callback
    },
    {
        "chuck",
        "chuck> ",
        "SinOsc s => dac;\n440 => s.freq;\n1::second => now;",
        chuck_highlight_init,
        chuck_highlight_free,
        chuck_highlight_callback
    },
    {
        "markdown",
        "md> ",
        "# Heading\n\nSome **bold** and *italic* text.\n\n```python\nprint(\"code\")\n```",
        markdown_highlight_init,
        markdown_highlight_free,
        markdown_highlight_callback
    },
    { NULL, NULL, NULL, NULL, NULL, NULL }
};

static void print_usage(const char *progname) {
    const language_t *lang;
    const char **themes;

    printf("Usage: %s <language> [--theme <theme>]\n\n", progname);
    printf("Test syntax highlighting for various languages with theme support.\n\n");

    printf("Supported languages:\n");
    for (lang = languages; lang->name != NULL; lang++) {
        printf("  %-10s - %s\n", lang->name, lang->prompt);
    }

    printf("\nSupported themes:\n");
    themes = theme_list();
    while (*themes != NULL) {
        const syntax_theme_t *t = theme_find(*themes);
        if (t) {
            printf("  %-18s - %s\n", t->name, t->description);
        }
        themes++;
    }

    printf("\nExamples:\n");
    printf("  %s lua\n", progname);
    printf("  %s python --theme dracula\n", progname);
    printf("  %s haskell --theme nord\n", progname);
}

static const language_t *find_language(const char *name) {
    const language_t *lang;
    for (lang = languages; lang->name != NULL; lang++) {
        if (strcmp(lang->name, name) == 0) {
            return lang;
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    const language_t *lang = NULL;
    const syntax_theme_t *selected_theme = NULL;
    linenoise_context_t *ctx;
    char *line;
    int i;

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    /* Parse arguments */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--theme") == 0 || strcmp(argv[i], "-t") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --theme requires an argument\n");
                return 1;
            }
            i++;
            selected_theme = theme_find(argv[i]);
            if (selected_theme == NULL) {
                fprintf(stderr, "Unknown theme: %s\n", argv[i]);
                fprintf(stderr, "Use --help to see available themes.\n");
                return 1;
            }
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else if (lang == NULL) {
            lang = find_language(argv[i]);
            if (lang == NULL) {
                fprintf(stderr, "Unknown language: %s\n\n", argv[i]);
                print_usage(argv[0]);
                return 1;
            }
        } else {
            fprintf(stderr, "Unexpected argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (lang == NULL) {
        fprintf(stderr, "Error: No language specified\n\n");
        print_usage(argv[0]);
        return 1;
    }

    /* Set theme (default is monokai) */
    if (selected_theme != NULL) {
        theme_set(selected_theme);
    }

    /* Initialize the highlighter */
    if (lang->init() != 0) {
        fprintf(stderr, "Failed to initialize %s highlighter\n", lang->name);
        return 1;
    }

    /* Create linenoise context */
    ctx = linenoise_context_create();
    if (!ctx) {
        fprintf(stderr, "Failed to create linenoise context\n");
        lang->cleanup();
        return 1;
    }

    /* Enable multiline mode for proper handling of embedded newlines */
    linenoise_set_multiline(ctx, 1);

    /* Set highlight callback */
    linenoise_set_highlight_callback(ctx, lang->callback);

    /* Print header */
    printf("Syntax highlighting test for: %s\n", lang->name);
    printf("Theme: %s\n", theme_get()->name);
    printf("Press Ctrl+D to exit.\n\n");
    printf("Example code to try:\n%s\n\n", lang->example);

    /* Main REPL loop */
    while ((line = linenoise_read(ctx, lang->prompt)) != NULL) {
        if (line[0] != '\0') {
            printf(">> %s\n", line);
            linenoise_history_add(ctx, line);
        }
        linenoise_free(line);
    }

    printf("\nGoodbye!\n");

    /* Cleanup */
    linenoise_context_destroy(ctx);
    lang->cleanup();

    return 0;
}
