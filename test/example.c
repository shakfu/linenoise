#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/select.h>
#endif
#include "linenoise.h"

void completion(const char *buf, linenoise_completions_t *lc) {
    if (buf[0] == 'h') {
        linenoise_add_completion(lc,"hello");
        linenoise_add_completion(lc,"hello there");
    }
}

char *hints(const char *buf, int *color, int *bold) {
    if (!strcasecmp(buf,"hello")) {
        *color = 35;
        *bold = 0;
        return " World";
    }
    return NULL;
}

int main(int argc, char **argv) {
    char *line;
    char *prgname = argv[0];
#ifndef _WIN32
    int async = 0;
#endif
    int multiline = 0;

    /* Parse options, with --multiline we enable multi line editing. */
    while(argc > 1) {
        argc--;
        argv++;
        if (!strcmp(*argv,"--multiline")) {
            multiline = 1;
            printf("Multi-line mode enabled.\n");
        } else if (!strcmp(*argv,"--keycodes")) {
            linenoise_print_key_codes();
            exit(0);
#ifndef _WIN32
        } else if (!strcmp(*argv,"--async")) {
            async = 1;
#endif
        } else {
#ifdef _WIN32
            fprintf(stderr, "Usage: %s [--multiline] [--keycodes]\n", prgname);
#else
            fprintf(stderr, "Usage: %s [--multiline] [--keycodes] [--async]\n", prgname);
#endif
            exit(1);
        }
    }

    /* Create a linenoise context. */
    linenoise_context_t *ctx = linenoise_context_create();
    if (!ctx) {
        fprintf(stderr, "Failed to create linenoise context\n");
        exit(1);
    }

    /* Configure the context. */
    if (multiline) {
        linenoise_set_multiline(ctx, 1);
    }

    /* Set the completion callback. This will be called every time the
     * user uses the <tab> key. */
    linenoise_set_completion_callback(ctx, completion);
    linenoise_set_hints_callback(ctx, hints);

    /* Load history from file. The history file is just a plain text file
     * where entries are separated by newlines. */
    linenoise_history_load(ctx, "history.txt"); /* Load the history at startup */

    /* Now this is the main loop of the typical linenoise-based application.
     * The call to linenoise_read() will block as long as the user types something
     * and presses enter.
     *
     * The typed string is returned as a malloc() allocated string by
     * linenoise, so the user needs to free() it. */

    while(1) {
#ifdef _WIN32
        /* On Windows, only synchronous mode is supported. */
        line = linenoise_read(ctx, "hello> ");
        if (line == NULL) break;
#else
        if (!async) {
            line = linenoise_read(ctx, "hello> ");
            if (line == NULL) break;
        } else {
            /* Asynchronous mode using the multiplexing API: wait for
             * data on stdin, and simulate async data coming from some source
             * using the select(2) timeout. */
            linenoise_state_t ls;
            char buf[1024];
            linenoise_edit_start(ctx, &ls,-1,-1,buf,sizeof(buf),"hello> ");
            while(1) {
		fd_set readfds;
		struct timeval tv;
		int retval;

		FD_ZERO(&readfds);
		FD_SET(ls.ifd, &readfds);
		tv.tv_sec = 1; // 1 sec timeout
		tv.tv_usec = 0;

		retval = select(ls.ifd+1, &readfds, NULL, NULL, &tv);
		if (retval == -1) {
		    perror("select()");
                    exit(1);
		} else if (retval) {
		    line = linenoise_edit_feed(&ls);
                    /* A NULL return means: line editing is continuing.
                     * Otherwise the user hit enter or stopped editing
                     * (CTRL+C/D). */
                    if (line != linenoise_edit_more) break;
		} else {
		    // Timeout occurred
                    static int counter = 0;
                    linenoise_hide(&ls);
		    printf("Async output %d.\n", counter++);
                    linenoise_show(&ls);
		}
            }
            linenoise_edit_stop(&ls);
            if (line == NULL) break; /* Ctrl+D/C. */
        }
#endif

        /* Do something with the string. */
        if (line[0] != '\0' && line[0] != '/') {
            printf("echo: '%s'\n", line);
            linenoise_history_add(ctx, line); /* Add to the history. */
            linenoise_history_save(ctx, "history.txt"); /* Save the history on disk. */
        } else if (!strncmp(line,"/historylen",11)) {
            /* The "/historylen" command will change the history len. */
            int len = atoi(line+11);
            linenoise_history_set_max_len(ctx, len);
        } else if (!strncmp(line, "/mask", 5)) {
            linenoise_set_mask_mode(ctx, 1);
        } else if (!strncmp(line, "/unmask", 7)) {
            linenoise_set_mask_mode(ctx, 0);
        } else if (line[0] == '/') {
            printf("Unreconized command: %s\n", line);
        }
        linenoise_free(line);
    }

    linenoise_context_destroy(ctx);
    return 0;
}
