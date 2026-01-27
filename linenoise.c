/* linenoise.c -- guerrilla line editing library against the idea that a
 * line editing lib needs to be 20,000 lines of C code.
 *
 * You can find the latest source code at:
 *
 *   http://github.com/antirez/linenoise
 *
 * Does a number of crazy assumptions that happen to be true in 99.9999% of
 * the 2010 UNIX computers around.
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
 *
 * ------------------------------------------------------------------------
 *
 * References:
 * - http://invisible-island.net/xterm/ctlseqs/ctlseqs.html
 * - http://www.3waylabs.com/nw/WWW/products/wizcon/vt220.html
 *
 * Todo list:
 * - Filter bogus Ctrl+<char> combinations.
 * - Win32 support
 *
 * Bloat:
 * - History search like Ctrl+r in readline?
 *
 * List of escape sequences used by this program, we do everything just
 * with three sequences. In order to be so cheap we may have some
 * flickering effect with some slow terminal, but the lesser sequences
 * the more compatible.
 *
 * EL (Erase Line)
 *    Sequence: ESC [ n K
 *    Effect: if n is 0 or missing, clear from cursor to end of line
 *    Effect: if n is 1, clear from beginning of line to cursor
 *    Effect: if n is 2, clear entire line
 *
 * CUF (CUrsor Forward)
 *    Sequence: ESC [ n C
 *    Effect: moves cursor forward n chars
 *
 * CUB (CUrsor Backward)
 *    Sequence: ESC [ n D
 *    Effect: moves cursor backward n chars
 *
 * The following is used to get the terminal width if getting
 * the width with the TIOCGWINSZ ioctl fails
 *
 * DSR (Device Status Report)
 *    Sequence: ESC [ 6 n
 *    Effect: reports the current cusor position as ESC [ n ; m R
 *            where n is the row and m is the column
 *
 * When multi line mode is enabled, we also use an additional escape
 * sequence. However multi line editing is disabled by default.
 *
 * CUU (Cursor Up)
 *    Sequence: ESC [ n A
 *    Effect: moves cursor up of n chars.
 *
 * CUD (Cursor Down)
 *    Sequence: ESC [ n B
 *    Effect: moves cursor down of n chars.
 *
 * When linenoiseClearScreen() is called, two additional escape sequences
 * are used in order to clear the screen and position the cursor at home
 * position.
 *
 * CUP (Cursor position)
 *    Sequence: ESC [ H
 *    Effect: moves the cursor to upper left corner
 *
 * ED (Erase display)
 *    Sequence: ESC [ 2 J
 *    Effect: clear the whole screen
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

#ifdef _WIN32
/* Windows-specific includes. */
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#define isatty _isatty
#define strcasecmp _stricmp
#define snprintf _snprintf
/* Windows doesn't have these, stub them out. */
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#else
/* POSIX-specific includes. */
#include <termios.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <fcntl.h>
#endif

#include "linenoise.h"
#include "internal/utf8.h"

#ifdef _WIN32
/* Windows I/O wrappers using console handles. */
static HANDLE win_get_output_handle(int fd) {
    (void)fd;
    return GetStdHandle(STD_OUTPUT_HANDLE);
}

static int win_write(int fd, const void *buf, size_t count) {
    HANDLE h = win_get_output_handle(fd);
    DWORD written;
    if (!WriteConsoleA(h, buf, (DWORD)count, &written, NULL)) {
        /* Fallback to WriteFile for redirected output. */
        if (!WriteFile(h, buf, (DWORD)count, &written, NULL)) {
            return -1;
        }
    }
    return (int)written;
}

static int win_read(int fd, void *buf, size_t count) {
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode, read_count;
    (void)fd;

    /* Check if we're reading from a console or redirected input. */
    if (GetConsoleMode(h, &mode)) {
        if (!ReadConsoleA(h, buf, (DWORD)count, &read_count, NULL)) {
            return -1;
        }
    } else {
        if (!ReadFile(h, buf, (DWORD)count, &read_count, NULL)) {
            return -1;
        }
    }
    return (int)read_count;
}

#define write(fd, buf, count) win_write(fd, buf, count)
#define read(fd, buf, count) win_read(fd, buf, count)
#endif

/* Compatibility macros mapping old function names to new utf8 module. */
#define utf8ByteLen         utf8_byte_len
#define utf8DecodeChar      utf8_decode
#define utf8DecodePrev      utf8_decode_prev
#define isVariationSelector utf8_is_variation_selector
#define isSkinToneModifier  utf8_is_skin_tone_modifier
#define isZWJ               utf8_is_zwj
#define isRegionalIndicator utf8_is_regional_indicator
#define isCombiningMark     utf8_is_combining_mark
#define isGraphemeExtend    utf8_is_grapheme_extend
#define utf8PrevCharLen     utf8_prev_grapheme_len
#define utf8NextCharLen     utf8_next_grapheme_len
#define utf8CharWidth       utf8_codepoint_width
#define utf8StrWidth        utf8_str_width
#define utf8SingleCharWidth utf8_single_char_width

#define LINENOISE_DEFAULT_HISTORY_MAX_LEN 100
#define LINENOISE_MAX_LINE 4096
static char *unsupported_term[] = {"dumb","cons25","emacs",NULL};

/* Forward declarations. */
static char *linenoise_no_tty(void);
static void refresh_line_with_completion(linenoise_state_t *ls, linenoise_completions_t *lc, int flags);
static void refresh_line_with_flags(linenoise_state_t *l, int flags);
static int history_add(const char *line);

/* ======================= Context Structure ================================= */

/* The linenoise_context structure encapsulates all state for one linenoise
 * instance. This enables thread-safe usage and multiple independent instances.
 * The old global API uses a default context for backward compatibility. */
struct linenoise_context {
    /* Terminal state */
#ifdef _WIN32
    DWORD orig_console_mode;
#else
    struct termios orig_termios;
#endif
    int rawmode;
    int atexit_registered;

    /* Configuration */
    int maskmode;
    int mlmode;

    /* History */
    int history_max_len;
    int history_len;
    char **history;

    /* Callbacks */
    linenoise_completion_cb_t *completionCallback;
    linenoise_hints_cb_t *hintsCallback;
    linenoise_free_hints_cb_t *freeHintsCallback;
};

/* Internal global variables used by the editing functions. */
static linenoise_completion_cb_t *completionCallback = NULL;
static linenoise_hints_cb_t *hintsCallback = NULL;
static linenoise_free_hints_cb_t *freeHintsCallback = NULL;
#ifdef _WIN32
static DWORD orig_console_mode; /* In order to restore at exit.*/
#else
static struct termios orig_termios; /* In order to restore at exit.*/
#endif
static int maskmode = 0; /* Show "***" instead of input. For passwords. */
static int rawmode = 0; /* For atexit() function to check if restore is needed*/
static int mlmode = 0;  /* Multi line mode. Default is single line. */
static int atexit_registered = 0; /* Register atexit just 1 time. */
static int history_max_len = LINENOISE_DEFAULT_HISTORY_MAX_LEN;
static int history_len = 0;
static char **history = NULL;

/* UTF-8 support is now provided by src/utf8.c via internal/utf8.h.
 * The compatibility macros above map old function names to the new module. */

enum KEY_ACTION{
	KEY_NULL = 0,	    /* NULL */
	CTRL_A = 1,         /* Ctrl+a */
	CTRL_B = 2,         /* Ctrl-b */
	CTRL_C = 3,         /* Ctrl-c */
	CTRL_D = 4,         /* Ctrl-d */
	CTRL_E = 5,         /* Ctrl-e */
	CTRL_F = 6,         /* Ctrl-f */
	CTRL_H = 8,         /* Ctrl-h */
	TAB = 9,            /* Tab */
	CTRL_K = 11,        /* Ctrl+k */
	CTRL_L = 12,        /* Ctrl+l */
	ENTER = 13,         /* Enter */
	CTRL_N = 14,        /* Ctrl-n */
	CTRL_P = 16,        /* Ctrl-p */
	CTRL_T = 20,        /* Ctrl-t */
	CTRL_U = 21,        /* Ctrl+u */
	CTRL_W = 23,        /* Ctrl+w */
	ESC = 27,           /* Escape */
	BACKSPACE =  127    /* Backspace */
};

static void linenoise_at_exit(void);
#define REFRESH_CLEAN (1<<0)    // Clean the old prompt from the screen
#define REFRESH_WRITE (1<<1)    // Rewrite the prompt on the screen.
#define REFRESH_ALL (REFRESH_CLEAN|REFRESH_WRITE) // Do both.
static void refresh_line(linenoise_state_t *l);

/* Debugging macro. */
#if 0
FILE *lndebug_fp = NULL;
#define lndebug(...) \
    do { \
        if (lndebug_fp == NULL) { \
            lndebug_fp = fopen("/tmp/lndebug.txt","a"); \
            fprintf(lndebug_fp, \
            "[%d %d %d] p: %d, rows: %d, rpos: %d, max: %d, oldmax: %d\n", \
            (int)l->len,(int)l->pos,(int)l->oldpos,plen,rows,rpos, \
            (int)l->oldrows,old_rows); \
        } \
        fprintf(lndebug_fp, ", " __VA_ARGS__); \
        fflush(lndebug_fp); \
    } while (0)
#else
#define lndebug(fmt, ...)
#endif

/* ======================= Low level terminal handling ====================== */

/* Return true if the terminal name is in the list of terminals we know are
 * not able to understand basic escape sequences. */
static int isUnsupportedTerm(void) {
#ifdef _WIN32
    /* Windows console with VT mode is always supported. */
    return 0;
#else
    char *term = getenv("TERM");
    int j;

    if (term == NULL) return 0;
    for (j = 0; unsupported_term[j]; j++)
        if (!strcasecmp(term,unsupported_term[j])) return 1;
    return 0;
#endif
}

#ifdef _WIN32
/* Windows VT mode flags (may not be defined in older SDKs). */
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif

static HANDLE hConsoleInput = INVALID_HANDLE_VALUE;
static HANDLE hConsoleOutput = INVALID_HANDLE_VALUE;
static DWORD orig_input_mode = 0;
static DWORD orig_output_mode = 0;

/* Raw mode for Windows using VT100 emulation (Windows 10+). */
static int enable_raw_mode(int fd) {
    DWORD input_mode, output_mode;
    (void)fd;  /* Unused on Windows. */

    /* Test mode: when LINENOISE_ASSUME_TTY is set, skip terminal setup. */
    if (getenv("LINENOISE_ASSUME_TTY")) {
        rawmode = 1;
        return 0;
    }

    hConsoleInput = GetStdHandle(STD_INPUT_HANDLE);
    hConsoleOutput = GetStdHandle(STD_OUTPUT_HANDLE);

    if (hConsoleInput == INVALID_HANDLE_VALUE ||
        hConsoleOutput == INVALID_HANDLE_VALUE) {
        return -1;
    }

    if (!GetConsoleMode(hConsoleInput, &orig_input_mode)) {
        return -1;
    }
    if (!GetConsoleMode(hConsoleOutput, &orig_output_mode)) {
        return -1;
    }

    if (!atexit_registered) {
        atexit(linenoise_at_exit);
        atexit_registered = 1;
    }

    /* Configure input: disable line input, echo, and processed input.
     * Enable VT input for escape sequence support. */
    input_mode = orig_input_mode;
    input_mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    input_mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;

    if (!SetConsoleMode(hConsoleInput, input_mode)) {
        return -1;
    }

    /* Configure output: enable VT processing for escape sequences. */
    output_mode = orig_output_mode;
    output_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;

    if (!SetConsoleMode(hConsoleOutput, output_mode)) {
        /* VT mode not available, restore input mode. */
        SetConsoleMode(hConsoleInput, orig_input_mode);
        return -1;
    }

    rawmode = 1;
    return 0;
}

static void disable_raw_mode(int fd) {
    (void)fd;

    if (getenv("LINENOISE_ASSUME_TTY")) {
        rawmode = 0;
        return;
    }

    if (rawmode) {
        SetConsoleMode(hConsoleInput, orig_input_mode);
        SetConsoleMode(hConsoleOutput, orig_output_mode);
        rawmode = 0;
    }
}

/* Read a single byte with a timeout on Windows. */
static int read_byte_with_timeout(int fd, char *c, int timeout_ms) {
    DWORD wait_result;
    DWORD read_count;
    (void)fd;

    if (timeout_ms == 0) {
        /* Non-blocking check. */
        DWORD events;
        if (!GetNumberOfConsoleInputEvents(hConsoleInput, &events) || events == 0) {
            return 0;
        }
    } else if (timeout_ms > 0) {
        wait_result = WaitForSingleObject(hConsoleInput, (DWORD)timeout_ms);
        if (wait_result == WAIT_TIMEOUT) return 0;
        if (wait_result != WAIT_OBJECT_0) return -1;
    }

    if (!ReadConsoleA(hConsoleInput, c, 1, &read_count, NULL)) {
        return -1;
    }
    return (read_count > 0) ? 1 : -1;
}

#else /* POSIX */

/* Raw mode: 1960 magic shit. */
static int enable_raw_mode(int fd) {
    struct termios raw;

    /* Test mode: when LINENOISE_ASSUME_TTY is set, skip terminal setup.
     * This allows testing via pipes without a real terminal. */
    if (getenv("LINENOISE_ASSUME_TTY")) {
        rawmode = 1;
        return 0;
    }

    if (!isatty(STDIN_FILENO)) goto fatal;
    if (!atexit_registered) {
        atexit(linenoise_at_exit);
        atexit_registered = 1;
    }
    if (tcgetattr(fd,&orig_termios) == -1) goto fatal;

    raw = orig_termios;  /* modify the original mode */
    /* input modes: no break, no CR to NL, no parity check, no strip char,
     * no start/stop output control. */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* output modes - disable post processing */
    raw.c_oflag &= ~(OPOST);
    /* control modes - set 8 bit chars */
    raw.c_cflag |= (CS8);
    /* local modes - choing off, canonical off, no extended functions,
     * no signal chars (^Z,^C) */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    /* control chars - set return condition: min number of bytes and timer.
     * We want read to return every single byte, without timeout. */
    raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0; /* 1 byte, no timer */

    /* put terminal in raw mode after flushing */
    if (tcsetattr(fd,TCSAFLUSH,&raw) < 0) goto fatal;
    rawmode = 1;
    return 0;

fatal:
    errno = ENOTTY;
    return -1;
}

static void disable_raw_mode(int fd) {
    /* Test mode: nothing to restore. */
    if (getenv("LINENOISE_ASSUME_TTY")) {
        rawmode = 0;
        return;
    }
    /* Don't even check the return value as it's too late. */
    if (rawmode && tcsetattr(fd,TCSAFLUSH,&orig_termios) != -1)
        rawmode = 0;
}

/* Read a single byte with a timeout. Returns 1 on success, 0 on timeout,
 * -1 on error. timeout_ms is the timeout in milliseconds. */
static int read_byte_with_timeout(int fd, char *c, int timeout_ms) {
    fd_set readfds;
    struct timeval tv;

    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(fd + 1, &readfds, NULL, NULL, &tv);
    if (ret <= 0) return ret;  /* 0 = timeout, -1 = error */
    return read(fd, c, 1);
}

#endif /* _WIN32 */

#ifdef _WIN32

/* Get terminal columns on Windows. */
static int get_columns(int ifd, int ofd) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    (void)ifd; (void)ofd;

    /* Test mode: use LINENOISE_COLS env var for fixed width. */
    char *cols_env = getenv("LINENOISE_COLS");
    if (cols_env) return atoi(cols_env);

    if (GetConsoleScreenBufferInfo(hConsoleOutput, &csbi)) {
        return csbi.srWindow.Right - csbi.srWindow.Left + 1;
    }
    return 80;
}

/* Internal: Clear the screen on Windows. */
static void clear_screen(void) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    COORD coord = {0, 0};
    DWORD written, console_size;

    if (!GetConsoleScreenBufferInfo(hConsoleOutput, &csbi)) {
        /* Fallback to escape sequence. */
        DWORD dummy;
        WriteConsoleA(hConsoleOutput, "\x1b[H\x1b[2J", 7, &dummy, NULL);
        return;
    }

    console_size = csbi.dwSize.X * csbi.dwSize.Y;
    FillConsoleOutputCharacterA(hConsoleOutput, ' ', console_size, coord, &written);
    FillConsoleOutputAttribute(hConsoleOutput, csbi.wAttributes, console_size, coord, &written);
    SetConsoleCursorPosition(hConsoleOutput, coord);
}

#else /* POSIX */

/* Use the ESC [6n escape sequence to query the horizontal cursor position
 * and return it. On error -1 is returned, on success the position of the
 * cursor. */
static int get_cursor_position(int ifd, int ofd) {
    char buf[32];
    int cols, rows;
    unsigned int i = 0;

    /* Report cursor location */
    if (write(ofd, "\x1b[6n", 4) != 4) return -1;

    /* Read the response: ESC [ rows ; cols R */
    while (i < sizeof(buf)-1) {
        if (read(ifd,buf+i,1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    /* Parse it. */
    if (buf[0] != ESC || buf[1] != '[') return -1;
    if (sscanf(buf+2,"%d;%d",&rows,&cols) != 2) return -1;
    return cols;
}

/* Try to get the number of columns in the current terminal, or assume 80
 * if it fails. */
static int get_columns(int ifd, int ofd) {
    struct winsize ws;

    /* Test mode: use LINENOISE_COLS env var for fixed width. */
    char *cols_env = getenv("LINENOISE_COLS");
    if (cols_env) return atoi(cols_env);

    if (ioctl(1, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        /* ioctl() failed. Try to query the terminal itself. */
        int start, cols;

        /* Get the initial position so we can restore it later. */
        start = get_cursor_position(ifd,ofd);
        if (start == -1) goto failed;

        /* Go to right margin and get position. */
        if (write(ofd,"\x1b[999C",6) != 6) goto failed;
        cols = get_cursor_position(ifd,ofd);
        if (cols == -1) goto failed;

        /* Restore position. */
        if (cols > start) {
            char seq[32];
            snprintf(seq,32,"\x1b[%dD",cols-start);
            if (write(ofd,seq,strlen(seq)) == -1) {
                /* Can't recover... */
            }
        }
        return cols;
    } else {
        return ws.ws_col;
    }

failed:
    return 80;
}

/* Internal: Clear the screen. Used to handle ctrl+l */
static void clear_screen(void) {
    if (write(STDOUT_FILENO,"\x1b[H\x1b[2J",7) <= 0) {
        /* nothing to do, just to avoid warning. */
    }
}

#endif /* _WIN32 */

/* Beep, used for completion when there is nothing to complete or when all
 * the choices were already shown. */
static void linenoise_beep(void) {
    fprintf(stderr, "\x7");
    fflush(stderr);
}

/* ============================== Completion ================================ */

/* Free a list of completion option populated by linenoiseAddCompletion(). */
static void free_completions(linenoise_completions_t *lc) {
    size_t i;
    for (i = 0; i < lc->len; i++)
        free(lc->cvec[i]);
    if (lc->cvec != NULL)
        free(lc->cvec);
}

/* Called by completeLine() and linenoiseShow() to render the current
 * edited line with the proposed completion. If the current completion table
 * is already available, it is passed as second argument, otherwise the
 * function will use the callback to obtain it.
 *
 * Flags are the same as refresh_line*(), that is REFRESH_* macros. */
static void refresh_line_with_completion(linenoise_state_t *ls, linenoise_completions_t *lc, int flags) {
    /* Obtain the table of completions if the caller didn't provide one. */
    linenoise_completions_t ctable = { 0, NULL };
    if (lc == NULL) {
        completionCallback(ls->buf,&ctable);
        lc = &ctable;
    }

    /* Show the edited line with completion if possible, or just refresh. */
    if (ls->completion_idx < lc->len) {
        linenoise_state_t saved = *ls;
        ls->len = ls->pos = strlen(lc->cvec[ls->completion_idx]);
        ls->buf = lc->cvec[ls->completion_idx];
        refresh_line_with_flags(ls,flags);
        ls->len = saved.len;
        ls->pos = saved.pos;
        ls->buf = saved.buf;
    } else {
        refresh_line_with_flags(ls,flags);
    }

    /* Free the completions table if we allocated it locally. */
    if (lc == &ctable) free_completions(&ctable);
}

/* This is an helper function for linenoiseEdit*() and is called when the
 * user types the <tab> key in order to complete the string currently in the
 * input.
 *
 * The state of the editing is encapsulated into the pointed linenoiseState
 * structure as described in the structure definition.
 *
 * If the function returns non-zero, the caller should handle the
 * returned value as a byte read from the standard input, and process
 * it as usually: this basically means that the function may return a byte
 * read from the termianl but not processed. Otherwise, if zero is returned,
 * the input was consumed by the completeLine() function to navigate the
 * possible completions, and the caller should read for the next characters
 * from stdin. */
static int completeLine(linenoise_state_t *ls, int keypressed) {
    linenoise_completions_t lc = { 0, NULL };
    int nwritten;
    char c = keypressed;

    completionCallback(ls->buf,&lc);
    if (lc.len == 0) {
        linenoise_beep();
        ls->in_completion = 0;
    } else {
        switch(c) {
            case 9: /* tab */
                if (ls->in_completion == 0) {
                    ls->in_completion = 1;
                    ls->completion_idx = 0;
                } else {
                    ls->completion_idx = (ls->completion_idx+1) % (lc.len+1);
                    if (ls->completion_idx == lc.len) linenoise_beep();
                }
                c = 0;
                break;
            case 27: /* escape */
                /* Re-show original buffer */
                if (ls->completion_idx < lc.len) refresh_line(ls);
                ls->in_completion = 0;
                c = 0;
                break;
            default:
                /* Update buffer and return */
                if (ls->completion_idx < lc.len) {
                    nwritten = snprintf(ls->buf,ls->buflen,"%s",
                        lc.cvec[ls->completion_idx]);
                    ls->len = ls->pos = nwritten;
                }
                ls->in_completion = 0;
                break;
        }

        /* Show completion or original buffer */
        if (ls->in_completion && ls->completion_idx < lc.len) {
            refresh_line_with_completion(ls,&lc,REFRESH_ALL);
        } else {
            refresh_line(ls);
        }
    }

    free_completions(&lc);
    return c; /* Return last read character */
}

/* This function is used by the callback function registered by the user
 * in order to add completion options given the input string when the
 * user typed <tab>. See the example.c source code for a very easy to
 * understand example. */
void linenoise_add_completion(linenoise_completions_t *lc, const char *str) {
    size_t len = strlen(str);
    char *copy, **cvec;

    copy = malloc(len+1);
    if (copy == NULL) return;
    memcpy(copy,str,len+1);
    cvec = realloc(lc->cvec,sizeof(char*)*(lc->len+1));
    if (cvec == NULL) {
        free(copy);
        return;
    }
    lc->cvec = cvec;
    lc->cvec[lc->len++] = copy;
}

/* =========================== Line editing ================================= */

/* We define a very simple "append buffer" structure, that is an heap
 * allocated string where we can append to. This is useful in order to
 * write all the escape sequences in a buffer and flush them to the standard
 * output in a single call, to avoid flickering effects. */
struct abuf {
    char *b;
    int len;
};

static void ab_init(struct abuf *ab) {
    ab->b = NULL;
    ab->len = 0;
}

static void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b,ab->len+len);

    if (new == NULL) return;
    memcpy(new+ab->len,s,len);
    ab->b = new;
    ab->len += len;
}

static void abFree(struct abuf *ab) {
    free(ab->b);
}

/* Helper of refreshSingleLine() and refreshMultiLine() to show hints
 * to the right of the prompt. Now uses display widths for proper UTF-8. */
void refresh_show_Hints(struct abuf *ab, linenoise_state_t *l, int pwidth) {
    char seq[64];
    size_t bufwidth = utf8StrWidth(l->buf, l->len);
    if (hintsCallback && pwidth + bufwidth < l->cols) {
        int color = -1, bold = 0;
        char *hint = hintsCallback(l->buf,&color,&bold);
        if (hint) {
            size_t hintlen = strlen(hint);
            size_t hintwidth = utf8StrWidth(hint, hintlen);
            size_t hintmaxwidth = l->cols - (pwidth + bufwidth);
            /* Truncate hint to fit, respecting UTF-8 boundaries. */
            if (hintwidth > hintmaxwidth) {
                size_t i = 0, w = 0;
                while (i < hintlen) {
                    size_t clen = utf8NextCharLen(hint, i, hintlen);
                    int cwidth = utf8SingleCharWidth(hint + i, clen);
                    if (w + cwidth > hintmaxwidth) break;
                    w += cwidth;
                    i += clen;
                }
                hintlen = i;
            }
            if (bold == 1 && color == -1) color = 37;
            if (color != -1 || bold != 0)
                snprintf(seq,64,"\033[%d;%d;49m",bold,color);
            else
                seq[0] = '\0';
            abAppend(ab,seq,strlen(seq));
            abAppend(ab,hint,hintlen);
            if (color != -1 || bold != 0)
                abAppend(ab,"\033[0m",4);
            /* Call the function to free the hint returned. */
            if (freeHintsCallback) freeHintsCallback(hint);
        }
    }
}

/* Single line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal.
 *
 * Flags is REFRESH_* macros. The function can just remove the old
 * prompt, just write it, or both.
 *
 * This function is UTF-8 aware and uses display widths (not byte counts)
 * for cursor positioning and horizontal scrolling. */
static void refreshSingleLine(linenoise_state_t *l, int flags) {
    char seq[64];
    size_t pwidth = utf8StrWidth(l->prompt, l->plen); /* Prompt display width */
    int fd = l->ofd;
    char *buf = l->buf;
    size_t len = l->len;    /* Byte length of buffer to display */
    size_t pos = l->pos;    /* Byte position of cursor */
    size_t poscol;          /* Display column of cursor */
    size_t lencol;          /* Display width of buffer */
    struct abuf ab;

    /* Calculate the display width up to cursor and total display width. */
    poscol = utf8StrWidth(buf, pos);
    lencol = utf8StrWidth(buf, len);

    /* Scroll the buffer horizontally if cursor is past the right edge.
     * We need to trim full UTF-8 characters from the left until the
     * cursor position fits within the terminal width. */
    while (pwidth + poscol >= l->cols) {
        size_t clen = utf8NextCharLen(buf, 0, len);
        int cwidth = utf8SingleCharWidth(buf, clen);
        buf += clen;
        len -= clen;
        pos -= clen;
        poscol -= cwidth;
        lencol -= cwidth;
    }

    /* Trim from the right if the line still doesn't fit. */
    while (pwidth + lencol > l->cols) {
        size_t clen = utf8PrevCharLen(buf, len);
        int cwidth = utf8SingleCharWidth(buf + len - clen, clen);
        len -= clen;
        lencol -= cwidth;
    }

    ab_init(&ab);
    /* Cursor to left edge */
    snprintf(seq,sizeof(seq),"\r");
    abAppend(&ab,seq,strlen(seq));

    if (flags & REFRESH_WRITE) {
        /* Write the prompt and the current buffer content */
        abAppend(&ab,l->prompt,l->plen);
        if (maskmode == 1) {
            /* In mask mode, we output one '*' per UTF-8 character, not byte */
            size_t i = 0;
            while (i < len) {
                abAppend(&ab,"*",1);
                i += utf8NextCharLen(buf, i, len);
            }
        } else {
            abAppend(&ab,buf,len);
        }
        /* Show hints if any. */
        refresh_show_Hints(&ab,l,pwidth);
    }

    /* Erase to right */
    snprintf(seq,sizeof(seq),"\x1b[0K");
    abAppend(&ab,seq,strlen(seq));

    if (flags & REFRESH_WRITE) {
        /* Move cursor to original position (using display column, not byte). */
        snprintf(seq,sizeof(seq),"\r\x1b[%dC", (int)(poscol+pwidth));
        abAppend(&ab,seq,strlen(seq));
    }

    if (write(fd,ab.b,ab.len) == -1) {} /* Can't recover from write error. */
    abFree(&ab);
}

/* Multi line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal.
 *
 * Flags is REFRESH_* macros. The function can just remove the old
 * prompt, just write it, or both.
 *
 * This function is UTF-8 aware and uses display widths for positioning. */
static void refreshMultiLine(linenoise_state_t *l, int flags) {
    char seq[64];
    size_t pwidth = utf8StrWidth(l->prompt, l->plen);  /* Prompt display width */
    size_t bufwidth = utf8StrWidth(l->buf, l->len);    /* Buffer display width */
    size_t poswidth = utf8StrWidth(l->buf, l->pos);    /* Cursor display width */
    int rows = (pwidth+bufwidth+l->cols-1)/l->cols;    /* rows used by current buf. */
    int rpos = l->oldrpos;   /* cursor relative row from previous refresh. */
    int rpos2; /* rpos after refresh. */
    int col; /* column position, zero-based. */
    int old_rows = l->oldrows;
    int fd = l->ofd, j;
    struct abuf ab;

    l->oldrows = rows;

    /* First step: clear all the lines used before. To do so start by
     * going to the last row. */
    ab_init(&ab);

    if (flags & REFRESH_CLEAN) {
        if (old_rows-rpos > 0) {
            lndebug("go down %d", old_rows-rpos);
            snprintf(seq,64,"\x1b[%dB", old_rows-rpos);
            abAppend(&ab,seq,strlen(seq));
        }

        /* Now for every row clear it, go up. */
        for (j = 0; j < old_rows-1; j++) {
            lndebug("clear+up");
            snprintf(seq,64,"\r\x1b[0K\x1b[1A");
            abAppend(&ab,seq,strlen(seq));
        }
    }

    if (flags & REFRESH_ALL) {
        /* Clean the top line. */
        lndebug("clear");
        snprintf(seq,64,"\r\x1b[0K");
        abAppend(&ab,seq,strlen(seq));
    }

    if (flags & REFRESH_WRITE) {
        /* Write the prompt and the current buffer content */
        abAppend(&ab,l->prompt,l->plen);
        if (maskmode == 1) {
            /* In mask mode, output one '*' per UTF-8 character, not byte */
            size_t i = 0;
            while (i < l->len) {
                abAppend(&ab,"*",1);
                i += utf8NextCharLen(l->buf, i, l->len);
            }
        } else {
            abAppend(&ab,l->buf,l->len);
        }

        /* Show hints if any. */
        refresh_show_Hints(&ab,l,pwidth);

        /* If we are at the very end of the screen with our prompt, we need to
         * emit a newline and move the prompt to the first column. */
        if (l->pos &&
            l->pos == l->len &&
            (poswidth+pwidth) % l->cols == 0)
        {
            lndebug("<newline>");
            abAppend(&ab,"\n",1);
            snprintf(seq,64,"\r");
            abAppend(&ab,seq,strlen(seq));
            rows++;
            if (rows > (int)l->oldrows) l->oldrows = rows;
        }

        /* Move cursor to right position. */
        rpos2 = (pwidth+poswidth+l->cols)/l->cols; /* Current cursor relative row */
        lndebug("rpos2 %d", rpos2);

        /* Go up till we reach the expected position. */
        if (rows-rpos2 > 0) {
            lndebug("go-up %d", rows-rpos2);
            snprintf(seq,64,"\x1b[%dA", rows-rpos2);
            abAppend(&ab,seq,strlen(seq));
        }

        /* Set column. */
        col = (pwidth+poswidth) % l->cols;
        lndebug("set col %d", 1+col);
        if (col)
            snprintf(seq,64,"\r\x1b[%dC", col);
        else
            snprintf(seq,64,"\r");
        abAppend(&ab,seq,strlen(seq));
    }

    lndebug("\n");
    l->oldpos = l->pos;
    if (flags & REFRESH_WRITE) l->oldrpos = rpos2;

    if (write(fd,ab.b,ab.len) == -1) {} /* Can't recover from write error. */
    abFree(&ab);
}

/* Calls the two low level functions refreshSingleLine() or
 * refreshMultiLine() according to the selected mode. */
static void refresh_line_with_flags(linenoise_state_t *l, int flags) {
    if (mlmode)
        refreshMultiLine(l,flags);
    else
        refreshSingleLine(l,flags);
}

/* Utility function to avoid specifying REFRESH_ALL all the times. */
static void refresh_line(linenoise_state_t *l) {
    refresh_line_with_flags(l,REFRESH_ALL);
}

/* Hide the current line, when using the multiplexing API. */
void linenoise_hide(linenoise_state_t *l) {
    if (mlmode)
        refreshMultiLine(l,REFRESH_CLEAN);
    else
        refreshSingleLine(l,REFRESH_CLEAN);
}

/* Show the current line, when using the multiplexing API. */
void linenoise_show(linenoise_state_t *l) {
    if (l->in_completion) {
        refresh_line_with_completion(l,NULL,REFRESH_WRITE);
    } else {
        refresh_line_with_flags(l,REFRESH_WRITE);
    }
}

/* Insert the character(s) 'c' of length 'clen' at cursor current position.
 * This handles both single-byte ASCII and multi-byte UTF-8 sequences.
 *
 * On error writing to the terminal -1 is returned, otherwise 0. */
int linenoise_edit_insert(linenoise_state_t *l, const char *c, size_t clen) {
    if (l->len + clen <= l->buflen) {
        if (l->len == l->pos) {
            /* Append at end of line. */
            memcpy(l->buf+l->pos, c, clen);
            l->pos += clen;
            l->len += clen;
            l->buf[l->len] = '\0';
            if ((!mlmode &&
                 utf8StrWidth(l->prompt,l->plen)+utf8StrWidth(l->buf,l->len) < l->cols &&
                 !hintsCallback)) {
                /* Avoid a full update of the line in the trivial case:
                 * single-width char, no hints, fits in one line. */
                if (maskmode == 1) {
                    if (write(l->ofd,"*",1) == -1) return -1;
                } else {
                    if (write(l->ofd,c,clen) == -1) return -1;
                }
            } else {
                refresh_line(l);
            }
        } else {
            /* Insert in the middle of the line. */
            memmove(l->buf+l->pos+clen, l->buf+l->pos, l->len-l->pos);
            memcpy(l->buf+l->pos, c, clen);
            l->len += clen;
            l->pos += clen;
            l->buf[l->len] = '\0';
            refresh_line(l);
        }
    }
    return 0;
}

/* Move cursor on the left. Moves by one UTF-8 character, not byte. */
void linenoise_edit_move_left(linenoise_state_t *l) {
    if (l->pos > 0) {
        l->pos -= utf8PrevCharLen(l->buf, l->pos);
        refresh_line(l);
    }
}

/* Move cursor on the right. Moves by one UTF-8 character, not byte. */
void linenoise_edit_move_right(linenoise_state_t *l) {
    if (l->pos != l->len) {
        l->pos += utf8NextCharLen(l->buf, l->pos, l->len);
        refresh_line(l);
    }
}

/* Move cursor to the start of the line. */
void linenoise_edit_move_home(linenoise_state_t *l) {
    if (l->pos != 0) {
        l->pos = 0;
        refresh_line(l);
    }
}

/* Move cursor to the end of the line. */
void linenoiseEditMoveEnd(linenoise_state_t *l) {
    if (l->pos != l->len) {
        l->pos = l->len;
        refresh_line(l);
    }
}

/* Substitute the currently edited line with the next or previous history
 * entry as specified by 'dir'. */
#define LINENOISE_HISTORY_NEXT 0
#define LINENOISE_HISTORY_PREV 1
void linenoiseEditHistoryNext(linenoise_state_t *l, int dir) {
    if (history_len > 1) {
        /* Update the current history entry before to
         * overwrite it with the next one. */
        free(history[history_len - 1 - l->history_index]);
        history[history_len - 1 - l->history_index] = strdup(l->buf);
        /* Show the new entry */
        l->history_index += (dir == LINENOISE_HISTORY_PREV) ? 1 : -1;
        if (l->history_index < 0) {
            l->history_index = 0;
            return;
        } else if (l->history_index >= history_len) {
            l->history_index = history_len-1;
            return;
        }
        strncpy(l->buf,history[history_len - 1 - l->history_index],l->buflen);
        l->buf[l->buflen-1] = '\0';
        l->len = l->pos = strlen(l->buf);
        refresh_line(l);
    }
}

/* Delete the character at the right of the cursor without altering the cursor
 * position. Basically this is what happens with the "Delete" keyboard key.
 * Now handles multi-byte UTF-8 characters. */
void linenoiseEditDelete(linenoise_state_t *l) {
    if (l->len > 0 && l->pos < l->len) {
        size_t clen = utf8NextCharLen(l->buf, l->pos, l->len);
        memmove(l->buf+l->pos, l->buf+l->pos+clen, l->len-l->pos-clen);
        l->len -= clen;
        l->buf[l->len] = '\0';
        refresh_line(l);
    }
}

/* Backspace implementation. Deletes the UTF-8 character before the cursor. */
void linenoiseEditBackspace(linenoise_state_t *l) {
    if (l->pos > 0 && l->len > 0) {
        size_t clen = utf8PrevCharLen(l->buf, l->pos);
        memmove(l->buf+l->pos-clen, l->buf+l->pos, l->len-l->pos);
        l->pos -= clen;
        l->len -= clen;
        l->buf[l->len] = '\0';
        refresh_line(l);
    }
}

/* Delete the previous word, maintaining the cursor at the start of the
 * current word. Handles UTF-8 by moving character-by-character. */
void linenoiseEditDeletePrevWord(linenoise_state_t *l) {
    size_t old_pos = l->pos;
    size_t diff;

    /* Skip spaces before the word (move backwards by UTF-8 chars). */
    while (l->pos > 0 && l->buf[l->pos-1] == ' ')
        l->pos -= utf8PrevCharLen(l->buf, l->pos);
    /* Skip non-space characters (move backwards by UTF-8 chars). */
    while (l->pos > 0 && l->buf[l->pos-1] != ' ')
        l->pos -= utf8PrevCharLen(l->buf, l->pos);
    diff = old_pos - l->pos;
    memmove(l->buf+l->pos, l->buf+old_pos, l->len-old_pos+1);
    l->len -= diff;
    refresh_line(l);
}

/* Saved state for non-blocking API context swapping. */
static struct {
    int active;
    int maskmode;
    int mlmode;
    linenoise_completion_cb_t *completionCallback;
    linenoise_hints_cb_t *hintsCallback;
    linenoise_free_hints_cb_t *freeHintsCallback;
    int history_len;
    int history_max_len;
    char **history;
    linenoise_context_t *ctx;  /* Remember the context to update history */
} edit_saved_state = {0};

/* Internal: Start editing (operates on global state). */
static int edit_start(linenoise_state_t *l, int stdin_fd, int stdout_fd, char *buf, size_t buflen, const char *prompt) {
    /* Populate the linenoise state that we pass to functions implementing
     * specific editing functionalities. */
    l->in_completion = 0;
    l->ifd = stdin_fd != -1 ? stdin_fd : STDIN_FILENO;
    l->ofd = stdout_fd != -1 ? stdout_fd : STDOUT_FILENO;
    l->buf = buf;
    l->buflen = buflen;
    l->prompt = prompt;
    l->plen = strlen(prompt);
    l->oldpos = l->pos = 0;
    l->len = 0;

    /* Enter raw mode. */
    if (enable_raw_mode(l->ifd) == -1) return -1;

    l->cols = get_columns(stdin_fd, stdout_fd);
    l->oldrows = 0;
    l->oldrpos = 1;  /* Cursor starts on row 1. */
    l->history_index = 0;

    /* Buffer starts empty. */
    l->buf[0] = '\0';
    l->buflen--; /* Make sure there is always space for the nulterm */

    /* If stdin is not a tty, stop here with the initialization. We
     * will actually just read a line from standard input in blocking
     * mode later, in linenoiseEditFeed(). */
    if (!isatty(l->ifd) && !getenv("LINENOISE_ASSUME_TTY")) return 0;

    /* The latest history entry is always our current buffer, that
     * initially is just an empty string. */
    history_add("");

    if (write(l->ofd,prompt,l->plen) == -1) return -1;
    return 0;
}

/* Public: Start editing with context support.
 * This is part of the multiplexed API of Linenoise, used for event-driven
 * programs. The context's settings are used for the editing session.
 *
 * Returns 0 on success, -1 on error. */
int linenoise_edit_start(linenoise_context_t *ctx, linenoise_state_t *l,
                         int stdin_fd, int stdout_fd,
                         char *buf, size_t buflen, const char *prompt) {
    if (!ctx) return -1;

    /* Save current global state. */
    edit_saved_state.active = 1;
    edit_saved_state.maskmode = maskmode;
    edit_saved_state.mlmode = mlmode;
    edit_saved_state.completionCallback = completionCallback;
    edit_saved_state.hintsCallback = hintsCallback;
    edit_saved_state.freeHintsCallback = freeHintsCallback;
    edit_saved_state.history_len = history_len;
    edit_saved_state.history_max_len = history_max_len;
    edit_saved_state.history = history;
    edit_saved_state.ctx = ctx;

    /* Set global state from context. */
    maskmode = ctx->maskmode;
    mlmode = ctx->mlmode;
    completionCallback = ctx->completionCallback;
    hintsCallback = ctx->hintsCallback;
    freeHintsCallback = ctx->freeHintsCallback;
    history_len = ctx->history_len;
    history_max_len = ctx->history_max_len;
    history = ctx->history;

    return edit_start(l, stdin_fd, stdout_fd, buf, buflen, prompt);
}

char *linenoise_edit_more = "If you see this, you are misusing the API: when linenoise_edit_feed() is called, if it returns linenoise_edit_more the user is yet editing the line. See the README file for more information.";

/* This function is part of the multiplexed API of linenoise, see the top
 * comment on linenoise_edit_start() for more information. Call this function
 * each time there is some data to read from the standard input file
 * descriptor. In the case of blocking operations, this function can just be
 * called in a loop, and block.
 *
 * The function returns linenoise_edit_more to signal that line editing is still
 * in progress, that is, the user didn't yet pressed enter / CTRL-D. Otherwise
 * the function returns the pointer to the heap-allocated buffer with the
 * edited line, that the user should free with linenoise_free().
 *
 * On special conditions, NULL is returned and errno is populated:
 *
 * EAGAIN if the user pressed Ctrl-C
 * ENOENT if the user pressed Ctrl-D
 *
 * Some other errno: I/O error.
 */
char *linenoise_edit_feed(linenoise_state_t *l) {
    /* Not a TTY, pass control to line reading without character
     * count limits. */
    if (!isatty(l->ifd) && !getenv("LINENOISE_ASSUME_TTY")) return linenoise_no_tty();

    char c;
    int nread;
    char seq[3];

    nread = read(l->ifd,&c,1);
    if (nread < 0) {
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? linenoise_edit_more : NULL;
    } else if (nread == 0) {
        return NULL;
    }

    /* Only autocomplete when the callback is set. It returns < 0 when
     * there was an error reading from fd. Otherwise it will return the
     * character that should be handled next. */
    if ((l->in_completion || c == 9) && completionCallback != NULL) {
        c = completeLine(l,c);
        /* Return on errors */
        if (c < 0) return NULL;
        /* Read next character when 0 */
        if (c == 0) return linenoise_edit_more;
    }

    switch(c) {
    case ENTER:    /* enter */
        history_len--;
        free(history[history_len]);
        if (mlmode) linenoiseEditMoveEnd(l);
        if (hintsCallback) {
            /* Force a refresh without hints to leave the previous
             * line as the user typed it after a newline. */
            linenoise_hints_cb_t *hc = hintsCallback;
            hintsCallback = NULL;
            refresh_line(l);
            hintsCallback = hc;
        }
        return strdup(l->buf);
    case CTRL_C:     /* ctrl-c */
        errno = EAGAIN;
        return NULL;
    case BACKSPACE:   /* backspace */
    case 8:     /* ctrl-h */
        linenoiseEditBackspace(l);
        break;
    case CTRL_D:     /* ctrl-d, remove char at right of cursor, or if the
                        line is empty, act as end-of-file. */
        if (l->len > 0) {
            linenoiseEditDelete(l);
        } else {
            history_len--;
            free(history[history_len]);
            errno = ENOENT;
            return NULL;
        }
        break;
    case CTRL_T:    /* ctrl-t, swaps current character with previous. */
        /* Handle UTF-8: swap the two UTF-8 characters around cursor. */
        if (l->pos > 0 && l->pos < l->len) {
            char tmp[32];
            size_t prevlen = utf8PrevCharLen(l->buf, l->pos);
            size_t currlen = utf8NextCharLen(l->buf, l->pos, l->len);
            size_t prevstart = l->pos - prevlen;
            /* Copy current char to tmp, move previous char right, paste tmp. */
            memcpy(tmp, l->buf + l->pos, currlen);
            memmove(l->buf + prevstart + currlen, l->buf + prevstart, prevlen);
            memcpy(l->buf + prevstart, tmp, currlen);
            if (l->pos + currlen <= l->len) l->pos += currlen;
            refresh_line(l);
        }
        break;
    case CTRL_B:     /* ctrl-b */
        linenoise_edit_move_left(l);
        break;
    case CTRL_F:     /* ctrl-f */
        linenoise_edit_move_right(l);
        break;
    case CTRL_P:    /* ctrl-p */
        linenoiseEditHistoryNext(l, LINENOISE_HISTORY_PREV);
        break;
    case CTRL_N:    /* ctrl-n */
        linenoiseEditHistoryNext(l, LINENOISE_HISTORY_NEXT);
        break;
    case ESC:    /* escape sequence */
        /* Read the next two bytes representing the escape sequence.
         * Use timeout to avoid hanging on partial sequences (e.g., user
         * pressing ESC alone). 100ms is enough for terminal responses. */
        if (read_byte_with_timeout(l->ifd,seq,100) != 1) break;
        if (read_byte_with_timeout(l->ifd,seq+1,100) != 1) break;

        /* ESC [ sequences. */
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                /* Extended escape, read additional byte. */
                if (read_byte_with_timeout(l->ifd,seq+2,100) != 1) break;
                if (seq[2] == '~') {
                    switch(seq[1]) {
                    case '3': /* Delete key. */
                        linenoiseEditDelete(l);
                        break;
                    }
                }
            } else {
                switch(seq[1]) {
                case 'A': /* Up */
                    linenoiseEditHistoryNext(l, LINENOISE_HISTORY_PREV);
                    break;
                case 'B': /* Down */
                    linenoiseEditHistoryNext(l, LINENOISE_HISTORY_NEXT);
                    break;
                case 'C': /* Right */
                    linenoise_edit_move_right(l);
                    break;
                case 'D': /* Left */
                    linenoise_edit_move_left(l);
                    break;
                case 'H': /* Home */
                    linenoise_edit_move_home(l);
                    break;
                case 'F': /* End*/
                    linenoiseEditMoveEnd(l);
                    break;
                }
            }
        }

        /* ESC O sequences. */
        else if (seq[0] == 'O') {
            switch(seq[1]) {
            case 'H': /* Home */
                linenoise_edit_move_home(l);
                break;
            case 'F': /* End*/
                linenoiseEditMoveEnd(l);
                break;
            }
        }
        break;
    default:
        /* Handle UTF-8 multi-byte sequences. When we receive the first byte
         * of a multi-byte UTF-8 character, read the remaining bytes to
         * complete the sequence before inserting. */
        {
            char utf8[4];
            int utf8len = utf8ByteLen(c);
            utf8[0] = c;
            if (utf8len > 1) {
                /* Read remaining bytes of the UTF-8 sequence. */
                int i;
                for (i = 1; i < utf8len; i++) {
                    if (read(l->ifd, utf8+i, 1) != 1) break;
                }
            }
            if (linenoise_edit_insert(l, utf8, utf8len)) return NULL;
        }
        break;
    case CTRL_U: /* Ctrl+u, delete the whole line. */
        l->buf[0] = '\0';
        l->pos = l->len = 0;
        refresh_line(l);
        break;
    case CTRL_K: /* Ctrl+k, delete from current to end of line. */
        l->buf[l->pos] = '\0';
        l->len = l->pos;
        refresh_line(l);
        break;
    case CTRL_A: /* Ctrl+a, go to the start of the line */
        linenoise_edit_move_home(l);
        break;
    case CTRL_E: /* ctrl+e, go to the end of the line */
        linenoiseEditMoveEnd(l);
        break;
    case CTRL_L: /* ctrl+l, clear screen */
        clear_screen();
        refresh_line(l);
        break;
    case CTRL_W: /* ctrl+w, delete previous word */
        linenoiseEditDeletePrevWord(l);
        break;
    }
    return linenoise_edit_more;
}

/* Internal: Stop editing (restores terminal). */
static void edit_stop(linenoise_state_t *l) {
    if (!isatty(l->ifd) && !getenv("LINENOISE_ASSUME_TTY")) return;
    disable_raw_mode(l->ifd);
    printf("\n");
}

/* Public: Stop editing and restore terminal.
 * This is part of the multiplexed linenoise API. Call this when
 * linenoise_edit_feed() returns something other than linenoise_edit_more. */
void linenoise_edit_stop(linenoise_state_t *l) {
    edit_stop(l);

    /* Restore global state if we're in an active edit session. */
    if (edit_saved_state.active) {
        /* Update context history from global state. */
        if (edit_saved_state.ctx) {
            edit_saved_state.ctx->history_len = history_len;
            edit_saved_state.ctx->history = history;
        }

        /* Restore previous global state. */
        maskmode = edit_saved_state.maskmode;
        mlmode = edit_saved_state.mlmode;
        completionCallback = edit_saved_state.completionCallback;
        hintsCallback = edit_saved_state.hintsCallback;
        freeHintsCallback = edit_saved_state.freeHintsCallback;
        history_len = edit_saved_state.history_len;
        history_max_len = edit_saved_state.history_max_len;
        history = edit_saved_state.history;
        edit_saved_state.active = 0;
        edit_saved_state.ctx = NULL;
    }
}

/* This just implements a blocking loop for the multiplexed API.
 * In many applications that are not event-drivern, we can just call
 * the blocking linenoise API, wait for the user to complete the editing
 * and return the buffer. */
static char *blocking_edit(int stdin_fd, int stdout_fd, char *buf, size_t buflen, const char *prompt)
{
    linenoise_state_t l;

    /* Editing without a buffer is invalid. */
    if (buflen == 0) {
        errno = EINVAL;
        return NULL;
    }

    edit_start(&l,stdin_fd,stdout_fd,buf,buflen,prompt);
    char *res;
    while((res = linenoise_edit_feed(&l)) == linenoise_edit_more);
    edit_stop(&l);
    return res;
}

/* This special mode is used by linenoise in order to print scan codes
 * on screen for debugging / development purposes. It is implemented
 * by the linenoise_example program using the --keycodes option. */
void linenoise_print_key_codes(void) {
    char quit[4];

    printf("Linenoise key codes debugging mode.\n"
            "Press keys to see scan codes. Type 'quit' at any time to exit.\n");
    if (enable_raw_mode(STDIN_FILENO) == -1) return;
    memset(quit,' ',4);
    while(1) {
        char c;
        int nread;

        nread = read(STDIN_FILENO,&c,1);
        if (nread <= 0) continue;
        memmove(quit,quit+1,sizeof(quit)-1); /* shift string to left. */
        quit[sizeof(quit)-1] = c; /* Insert current char on the right. */
        if (memcmp(quit,"quit",sizeof(quit)) == 0) break;

        printf("'%c' %02x (%d) (type quit to exit)\n",
            isprint(c) ? c : '?', (int)c, (int)c);
        printf("\r"); /* Go left edge manually, we are in raw mode. */
        fflush(stdout);
    }
    disable_raw_mode(STDIN_FILENO);
}

/* This function is called when linenoise() is called with the standard
 * input file descriptor not attached to a TTY. So for example when the
 * program using linenoise is called in pipe or with a file redirected
 * to its standard input. In this case, we want to be able to return the
 * line regardless of its length (by default we are limited to 4k). */
static char *linenoise_no_tty(void) {
    char *line = NULL;
    size_t len = 0, maxlen = 0;

    while(1) {
        if (len == maxlen) {
            if (maxlen == 0) maxlen = 16;
            maxlen *= 2;
            char *oldval = line;
            line = realloc(line,maxlen);
            if (line == NULL) {
                if (oldval) free(oldval);
                return NULL;
            }
        }
        int c = fgetc(stdin);
        if (c == EOF || c == '\n') {
            if (c == EOF && len == 0) {
                free(line);
                return NULL;
            } else {
                line[len] = '\0';
                return line;
            }
        } else {
            line[len] = c;
            len++;
        }
    }
}

/* Internal: The high level line reading function using global state.
 * This function checks if the terminal has basic capabilities, just checking
 * for a blacklist of stupid terminals, and later either calls the line
 * editing function or uses dummy fgets() so that you will be able to type
 * something even in the most desperate of the conditions. */
static char *read_line(const char *prompt) {
    char buf[LINENOISE_MAX_LINE];

    if (!isatty(STDIN_FILENO) && !getenv("LINENOISE_ASSUME_TTY")) {
        /* Not a tty: read from file / pipe. In this mode we don't want any
         * limit to the line size, so we call a function to handle that. */
        return linenoise_no_tty();
    } else if (isUnsupportedTerm()) {
        size_t len;

        printf("%s",prompt);
        fflush(stdout);
        if (fgets(buf,LINENOISE_MAX_LINE,stdin) == NULL) return NULL;
        len = strlen(buf);
        while(len && (buf[len-1] == '\n' || buf[len-1] == '\r')) {
            len--;
            buf[len] = '\0';
        }
        return strdup(buf);
    } else {
        char *retval = blocking_edit(STDIN_FILENO,STDOUT_FILENO,buf,LINENOISE_MAX_LINE,prompt);
        return retval;
    }
}

/* This is just a wrapper the user may want to call in order to make sure
 * the linenoise returned buffer is freed with the same allocator it was
 * created with. Useful when the main program is using an alternative
 * allocator. */
void linenoise_free(void *ptr) {
    if (ptr == linenoise_edit_more) return; /* Protect from API misuse. */
    free(ptr);
}

/* ================================ History ================================= */

/* Free the history, but does not reset it. Only used when we have to
 * exit() to avoid memory leaks are reported by valgrind & co. */
static void free_history(void) {
    if (history) {
        int j;

        for (j = 0; j < history_len; j++)
            free(history[j]);
        free(history);
    }
}

/* At exit we'll try to fix the terminal to the initial conditions. */
static void linenoise_at_exit(void) {
    disable_raw_mode(STDIN_FILENO);
    free_history();
}

/* Internal: Add a new entry to the global history.
 * It uses a fixed array of char pointers that are shifted (memmoved)
 * when the history max length is reached in order to remove the older
 * entry and make room for the new one, so it is not exactly suitable for huge
 * histories, but will work well for a few hundred of entries.
 *
 * Using a circular buffer is smarter, but a bit more complex to handle. */
static int history_add(const char *line) {
    char *linecopy;

    if (history_max_len == 0) return 0;

    /* Initialization on first call. */
    if (history == NULL) {
        history = malloc(sizeof(char*)*history_max_len);
        if (history == NULL) return 0;
        memset(history,0,(sizeof(char*)*history_max_len));
    }

    /* Don't add duplicated lines. */
    if (history_len && !strcmp(history[history_len-1], line)) return 0;

    /* Add an heap allocated copy of the line in the history.
     * If we reached the max length, remove the older line. */
    linecopy = strdup(line);
    if (!linecopy) return 0;
    if (history_len == history_max_len) {
        free(history[0]);
        memmove(history,history+1,sizeof(char*)*(history_max_len-1));
        history_len--;
    }
    history[history_len] = linecopy;
    history_len++;
    return 1;
}

/* ========================= Context-based API ============================== */

/* Create a new linenoise context with default settings. */
linenoise_context_t *linenoise_context_create(void) {
    linenoise_context_t *ctx = malloc(sizeof(linenoise_context_t));
    if (!ctx) return NULL;

    memset(&ctx->orig_termios, 0, sizeof(ctx->orig_termios));
    ctx->rawmode = 0;
    ctx->atexit_registered = 0;
    ctx->maskmode = 0;
    ctx->mlmode = 0;
    ctx->history_max_len = LINENOISE_DEFAULT_HISTORY_MAX_LEN;
    ctx->history_len = 0;
    ctx->history = NULL;
    ctx->completionCallback = NULL;
    ctx->hintsCallback = NULL;
    ctx->freeHintsCallback = NULL;

    return ctx;
}

/* Destroy a linenoise context and free all associated resources. */
void linenoise_context_destroy(linenoise_context_t *ctx) {
    if (!ctx) return;

    /* Free history. */
    if (ctx->history) {
        for (int j = 0; j < ctx->history_len; j++) {
            free(ctx->history[j]);
        }
        free(ctx->history);
    }

    free(ctx);
}

/* Set multi-line mode for a context. */
void linenoise_set_multiline(linenoise_context_t *ctx, int ml) {
    if (ctx) ctx->mlmode = ml;
}

/* Set mask mode for a context (password entry). */
void linenoise_set_mask_mode(linenoise_context_t *ctx, int enable) {
    if (ctx) ctx->maskmode = enable;
}

/* Set completion callback for a context. */
void linenoise_set_completion_callback(linenoise_context_t *ctx, linenoise_completion_cb_t *fn) {
    if (ctx) ctx->completionCallback = fn;
}

/* Set hints callback for a context. */
void linenoise_set_hints_callback(linenoise_context_t *ctx, linenoise_hints_cb_t *fn) {
    if (ctx) ctx->hintsCallback = fn;
}

/* Set free hints callback for a context. */
void linenoise_set_free_hints_callback(linenoise_context_t *ctx, linenoise_free_hints_cb_t *fn) {
    if (ctx) ctx->freeHintsCallback = fn;
}

/* Add a line to history for a context. */
int linenoise_history_add(linenoise_context_t *ctx, const char *line) {
    char *linecopy;

    if (!ctx) return 0;
    if (ctx->history_max_len == 0) return 0;

    /* Don't add duplicates of the previous line. */
    if (ctx->history_len && !strcmp(ctx->history[ctx->history_len-1], line))
        return 0;

    linecopy = strdup(line);
    if (!linecopy) return 0;

    if (ctx->history == NULL) {
        ctx->history = malloc(sizeof(char*) * ctx->history_max_len);
        if (ctx->history == NULL) {
            free(linecopy);
            return 0;
        }
        memset(ctx->history, 0, sizeof(char*) * ctx->history_max_len);
    }

    if (ctx->history_len == ctx->history_max_len) {
        free(ctx->history[0]);
        memmove(ctx->history, ctx->history + 1, sizeof(char*) * (ctx->history_max_len - 1));
        ctx->history_len--;
    }
    ctx->history[ctx->history_len] = linecopy;
    ctx->history_len++;
    return 1;
}

/* Set maximum history length for a context. */
int linenoise_history_set_max_len(linenoise_context_t *ctx, int len) {
    char **new_history;

    if (!ctx) return 0;
    if (len < 1) return 0;

    if (ctx->history) {
        int tocopy = ctx->history_len;
        new_history = malloc(sizeof(char*) * len);
        if (new_history == NULL) return 0;

        if (len < tocopy) {
            int j;
            for (j = 0; j < tocopy - len; j++)
                free(ctx->history[j]);
            tocopy = len;
        }
        memset(new_history, 0, sizeof(char*) * len);
        memcpy(new_history, ctx->history + (ctx->history_len - tocopy),
               sizeof(char*) * tocopy);
        free(ctx->history);
        ctx->history = new_history;
    }
    ctx->history_max_len = len;
    if (ctx->history_len > ctx->history_max_len)
        ctx->history_len = ctx->history_max_len;
    return 1;
}

/* Save history to file for a context. */
int linenoise_history_save(linenoise_context_t *ctx, const char *filename) {
    int fd;
    FILE *fp;

    if (!ctx) return -1;

    fd = open(filename, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
    if (fd == -1) return -1;

    fp = fdopen(fd, "w");
    if (fp == NULL) {
        close(fd);
        return -1;
    }
    for (int j = 0; j < ctx->history_len; j++)
        fprintf(fp, "%s\n", ctx->history[j]);
    fclose(fp);
    return 0;
}

/* Load history from file for a context. */
int linenoise_history_load(linenoise_context_t *ctx, const char *filename) {
    FILE *fp;
    char buf[LINENOISE_MAX_LINE];

    if (!ctx) return -1;

    fp = fopen(filename, "r");
    if (fp == NULL) return -1;

    while (fgets(buf, LINENOISE_MAX_LINE, fp) != NULL) {
        char *p;
        p = strchr(buf, '\r');
        if (!p) p = strchr(buf, '\n');
        if (p) *p = '\0';
        linenoise_history_add(ctx, buf);
    }
    fclose(fp);
    return 0;
}

/* Main line editing function using a context.
 * Note: This is a simplified version that uses the global state internally
 * but respects the context's settings. A full implementation would require
 * threading the context through all internal functions. */
char *linenoise_read(linenoise_context_t *ctx, const char *prompt) {
    if (!ctx) return NULL;

    /* Temporarily set global state from context for backward compatibility
     * with internal functions. This is a transitional approach. */
    int saved_maskmode = maskmode;
    int saved_mlmode = mlmode;
    linenoise_completion_cb_t *saved_completion = completionCallback;
    linenoise_hints_cb_t *saved_hints = hintsCallback;
    linenoise_free_hints_cb_t *saved_freehints = freeHintsCallback;

    maskmode = ctx->maskmode;
    mlmode = ctx->mlmode;
    completionCallback = ctx->completionCallback;
    hintsCallback = ctx->hintsCallback;
    freeHintsCallback = ctx->freeHintsCallback;

    /* Also temporarily swap history. */
    int saved_history_len = history_len;
    int saved_history_max_len = history_max_len;
    char **saved_history = history;
    history_len = ctx->history_len;
    history_max_len = ctx->history_max_len;
    history = ctx->history;

    /* Call the internal line reading function. */
    char *result = read_line(prompt);

    /* Copy back any history changes. */
    ctx->history_len = history_len;
    ctx->history = history;

    /* Restore global state. */
    maskmode = saved_maskmode;
    mlmode = saved_mlmode;
    completionCallback = saved_completion;
    hintsCallback = saved_hints;
    freeHintsCallback = saved_freehints;
    history_len = saved_history_len;
    history_max_len = saved_history_max_len;
    history = saved_history;

    return result;
}

/* Clear the screen using the context's output (currently just uses stdout). */
void linenoise_clear_screen(linenoise_context_t *ctx) {
    (void)ctx;  /* Currently unused, but kept for API consistency. */
    clear_screen();
}
