/* linenoise.h -- VERSION 2.0
 *
 * Guerrilla line editing library against the idea that a line editing lib
 * needs to be 20,000 lines of C code.
 *
 * See linenoise.c for more information.
 *
 * ------------------------------------------------------------------------
 *
 * Copyright (c) 2010-2023, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010-2013, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  *  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  *  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef LINENOISE_H
#define LINENOISE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/* Sentinel value returned by linenoise_edit_feed() when more input is needed. */
extern char *linenoise_edit_more;

/* Completions structure for tab-completion callback. */
typedef struct linenoise_completions {
    size_t len;
    char **cvec;
} linenoise_completions_t;

/* Callback types. */
typedef void (linenoise_completion_cb_t)(const char *buf, linenoise_completions_t *completions);
typedef char *(linenoise_hints_cb_t)(const char *buf, int *color, int *bold);
typedef void (linenoise_free_hints_cb_t)(void *hint);

/* Opaque context structure. Each context has independent history, callbacks,
 * and settings. Thread-safe when using separate contexts per thread. */
typedef struct linenoise_context linenoise_context_t;

/* Editing state for non-blocking API. */
typedef struct linenoise_state {
    int in_completion;
    size_t completion_idx;
    int ifd;
    int ofd;
    char *buf;
    size_t buflen;
    const char *prompt;
    size_t plen;
    size_t pos;
    size_t oldpos;
    size_t len;
    size_t cols;
    size_t oldrows;
    int oldrpos;
    int history_index;
} linenoise_state_t;

/* ===== Context Management ===== */

linenoise_context_t *linenoise_context_create(void);
void linenoise_context_destroy(linenoise_context_t *ctx);

/* ===== Configuration ===== */

void linenoise_set_multiline(linenoise_context_t *ctx, int enable);
void linenoise_set_mask_mode(linenoise_context_t *ctx, int enable);
void linenoise_set_completion_callback(linenoise_context_t *ctx, linenoise_completion_cb_t *fn);
void linenoise_set_hints_callback(linenoise_context_t *ctx, linenoise_hints_cb_t *fn);
void linenoise_set_free_hints_callback(linenoise_context_t *ctx, linenoise_free_hints_cb_t *fn);

/* ===== Blocking API ===== */

char *linenoise_read(linenoise_context_t *ctx, const char *prompt);

/* ===== Non-blocking API ===== */

int linenoise_edit_start(linenoise_context_t *ctx, linenoise_state_t *state,
                         int stdin_fd, int stdout_fd,
                         char *buf, size_t buflen, const char *prompt);
char *linenoise_edit_feed(linenoise_state_t *state);
void linenoise_edit_stop(linenoise_state_t *state);
void linenoise_hide(linenoise_state_t *state);
void linenoise_show(linenoise_state_t *state);

/* ===== History ===== */

int linenoise_history_add(linenoise_context_t *ctx, const char *line);
int linenoise_history_set_max_len(linenoise_context_t *ctx, int len);
int linenoise_history_save(linenoise_context_t *ctx, const char *filename);
int linenoise_history_load(linenoise_context_t *ctx, const char *filename);

/* ===== Utilities ===== */

void linenoise_free(void *ptr);
void linenoise_clear_screen(linenoise_context_t *ctx);
void linenoise_add_completion(linenoise_completions_t *completions, const char *text);
void linenoise_print_key_codes(void);

#ifdef __cplusplus
}
#endif

#endif /* LINENOISE_H */
