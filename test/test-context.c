/* test-context.c -- In-process tests for context isolation.
 *
 * Drives the non-blocking API through pipes (LINENOISE_ASSUME_TTY) to check
 * that callbacks, history, undo and settings stay with their own context
 * when several sessions are active at once, or on separate threads.
 * test_typeahead_kept() instead uses a real pty, so raw mode is entered.
 */

#define _XOPEN_SOURCE 600   /* posix_openpt() and friends */
#define _DARWIN_C_SOURCE    /* keep BSD api visible on macOS */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <poll.h>
#include <termios.h>
#include <sys/ioctl.h>
#include "linenoise.h"

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond, ...) do { \
    tests_run++; \
    if (!(cond)) { \
        tests_failed++; \
        printf("FAIL %s:%d: ", __func__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

/* One editing session fed from a pipe. */
typedef struct {
    linenoise_state_t st;
    char buf[256];
    int in[2];      /* in[1] is written by the test, in[0] read by linenoise */
    int out;        /* /dev/null */
} session_t;

static int session_start(session_t *s, linenoise_context_t *ctx, const char *prompt) {
    if (pipe(s->in) == -1) return -1;
    fcntl(s->in[0], F_SETFL, O_NONBLOCK);
    s->out = open("/dev/null", O_WRONLY);
    return linenoise_edit_start(ctx, &s->st, s->in[0], s->out,
                                s->buf, sizeof(s->buf), prompt);
}

/* Write keys and feed them. Returns the line on ENTER, NULL on EOF/error,
 * or linenoise_edit_more if the session is still editing. */
static char *session_feed(session_t *s, const char *keys) {
    char *r = linenoise_edit_more;
    size_t n = strlen(keys);
    if (write(s->in[1], keys, n) != (ssize_t)n) return NULL;
    for (size_t i = 0; i <= n; i++) {
        r = linenoise_edit_feed(&s->st);
        if (r != linenoise_edit_more) break;
    }
    return r;
}

static void session_stop(session_t *s) {
    linenoise_edit_stop(&s->st);
    close(s->in[0]);
    close(s->in[1]);
    close(s->out);
}

/* Save ctx history and return it joined with '|'. Caller frees. */
static char *history_dump(linenoise_context_t *ctx) {
    char path[] = "/tmp/linenoise-test-XXXXXX";
    int fd = mkstemp(path);
    if (fd == -1) return NULL;
    close(fd);
    linenoise_history_save(ctx, path);

    FILE *fp = fopen(path, "r");
    char *res = calloc(1, 4096);
    char line[256];
    while (fp && fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0';
        if (res[0]) strcat(res, "|");
        strcat(res, line);
    }
    if (fp) fclose(fp);
    unlink(path);
    return res;
}

static int calls_a, calls_b;
static void complete_a(const char *buf, size_t pos, linenoise_completions_t *lc,
                        void *udata) {
    (void)buf; (void)pos; (void)lc; (void)udata; calls_a++;
}
static void complete_b(const char *buf, size_t pos, linenoise_completions_t *lc,
                        void *udata) {
    (void)buf; (void)pos; (void)lc; (void)udata; calls_b++;
}

/* ========================= Tests ========================= */

static void test_interleaved_completion(void) {
    linenoise_context_t *a = linenoise_context_create();
    linenoise_context_t *b = linenoise_context_create();
    session_t sa, sb;
    calls_a = calls_b = 0;
    linenoise_set_completion_callback(a, complete_a, NULL);
    linenoise_set_completion_callback(b, complete_b, NULL);

    session_start(&sa, a, "a> ");
    session_start(&sb, b, "b> ");
    session_feed(&sa, "x\t");
    CHECK(calls_a == 1 && calls_b == 0, "TAB in A: calls_a=%d calls_b=%d", calls_a, calls_b);
    session_feed(&sb, "y\t");
    CHECK(calls_a == 1 && calls_b == 1, "TAB in B: calls_a=%d calls_b=%d", calls_a, calls_b);
    session_stop(&sa);
    session_stop(&sb);

    linenoise_context_destroy(a);
    linenoise_context_destroy(b);
}

static void test_interleaved_history(void) {
    linenoise_context_t *a = linenoise_context_create();
    linenoise_context_t *b = linenoise_context_create();
    session_t sa, sb;
    linenoise_history_add(a, "a-hist");
    linenoise_history_add(b, "b-hist");

    session_start(&sa, a, "a> ");
    session_start(&sb, b, "b> ");
    session_feed(&sa, "\x1b[A");
    CHECK(strcmp(sa.st.buf, "a-hist") == 0, "Up in A shows \"%s\"", sa.st.buf);
    session_feed(&sb, "\x1b[A");
    CHECK(strcmp(sb.st.buf, "b-hist") == 0, "Up in B shows \"%s\"", sb.st.buf);

    char *la = session_feed(&sa, "\r");
    char *lb = session_feed(&sb, "\r");
    CHECK(la && strcmp(la, "a-hist") == 0, "A returned \"%s\"", la ? la : "(null)");
    CHECK(lb && strcmp(lb, "b-hist") == 0, "B returned \"%s\"", lb ? lb : "(null)");
    /* Stop in start order, not reverse order. */
    session_stop(&sa);
    session_stop(&sb);
    linenoise_free(la);
    linenoise_free(lb);

    char *ha = history_dump(a), *hb = history_dump(b);
    CHECK(strcmp(ha, "a-hist") == 0, "A history \"%s\"", ha);
    CHECK(strcmp(hb, "b-hist") == 0, "B history \"%s\"", hb);
    free(ha); free(hb);

    linenoise_context_destroy(a);
    linenoise_context_destroy(b);
}

static void test_interleaved_undo(void) {
    linenoise_context_t *a = linenoise_context_create();
    linenoise_context_t *b = linenoise_context_create();
    session_t sa, sb;

    /* Backspace saves an undo snapshot; Ctrl-Z restores it. */
    session_start(&sa, a, "a> ");
    session_feed(&sa, "aa\x7f");
    session_start(&sb, b, "b> ");
    session_feed(&sb, "bb\x7f");
    session_feed(&sa, "\x1a");
    CHECK(strcmp(sa.st.buf, "aa") == 0, "undo in A gives \"%s\"", sa.st.buf);
    session_feed(&sb, "\x1a");
    CHECK(strcmp(sb.st.buf, "bb") == 0, "undo in B gives \"%s\"", sb.st.buf);
    session_stop(&sa);
    session_stop(&sb);

    linenoise_context_destroy(a);
    linenoise_context_destroy(b);
}

static void test_history_add_during_session(void) {
    /* Once with no history array yet, once with an existing one. */
    for (int existing = 0; existing <= 1; existing++) {
        linenoise_context_t *c = linenoise_context_create();
        session_t s;
        if (existing) linenoise_history_add(c, "old");

        session_start(&s, c, "c> ");
        linenoise_history_add(c, "mid");
        char *line = session_feed(&s, "y\r");
        CHECK(line && strcmp(line, "y") == 0, "returned \"%s\"", line ? line : "(null)");
        session_stop(&s);
        linenoise_free(line);

        char *h = history_dump(c);
        const char *want = existing ? "old|mid" : "mid";
        CHECK(strcmp(h, want) == 0, "history \"%s\", want \"%s\"", h, want);
        free(h);
        linenoise_context_destroy(c);
    }
}

static void test_history_nav_after_add_during_session(void) {
    linenoise_context_t *c = linenoise_context_create();
    session_t s;
    linenoise_history_add(c, "one");

    session_start(&s, c, "c> ");
    linenoise_history_add(c, "two");
    session_feed(&s, "\x1b[A");
    CHECK(strcmp(s.st.buf, "two") == 0, "first Up shows \"%s\"", s.st.buf);
    session_feed(&s, "\x1b[A");
    CHECK(strcmp(s.st.buf, "one") == 0, "second Up shows \"%s\"", s.st.buf);
    session_stop(&s);

    char *h = history_dump(c);
    CHECK(strcmp(h, "one|two") == 0, "history \"%s\"", h);
    free(h);
    linenoise_context_destroy(c);
}

static void test_ctrl_c_leaves_history_clean(void) {
    linenoise_context_t *c = linenoise_context_create();
    linenoise_history_add(c, "keep");
    for (int i = 0; i < 3; i++) {
        session_t s;
        session_start(&s, c, "c> ");
        char *line = session_feed(&s, "z\x03");
        CHECK(line == NULL, "Ctrl-C returned a line");
        session_stop(&s);
    }
    char *h = history_dump(c);
    CHECK(strcmp(h, "keep") == 0, "history \"%s\"", h);
    free(h);
    linenoise_context_destroy(c);
}

static void test_setter_during_session(void) {
    linenoise_context_t *c = linenoise_context_create();
    session_t s;
    calls_a = 0;

    session_start(&s, c, "c> ");
    linenoise_set_completion_callback(c, complete_a, NULL);
    session_feed(&s, "x\t");
    CHECK(calls_a == 1, "callback set mid-session called %d times", calls_a);
    session_stop(&s);
    linenoise_context_destroy(c);
}

/* Each callback receives its own setter's udata and the cursor offset. */
static int tag_c, tag_h, tag_hl;
static void *seen_c, *seen_h, *seen_free, *seen_hl;
static size_t pos_c, pos_h, pos_hl;
static void udata_complete(const char *buf, size_t pos, linenoise_completions_t *lc,
                           void *udata) {
    (void)buf; (void)lc;
    seen_c = udata; pos_c = pos;
}
static char *udata_hints(const char *buf, size_t pos, int *color, int *bold, void *udata) {
    (void)buf; (void)color; (void)bold;
    seen_h = udata; pos_h = pos;
    return strdup("hint");
}
static void udata_free_hints(void *hint, void *udata) {
    seen_free = udata;
    free(hint);
}
static void udata_highlight(const char *buf, char *colors, size_t len, size_t pos,
                            void *udata) {
    (void)buf; (void)colors; (void)len;
    seen_hl = udata; pos_hl = pos;
}

static void test_callback_udata(void) {
    linenoise_context_t *ctx = linenoise_context_create();
    session_t s;
    linenoise_set_completion_callback(ctx, udata_complete, &tag_c);
    linenoise_set_hints_callback(ctx, udata_hints, &tag_h);
    linenoise_set_free_hints_callback(ctx, udata_free_hints);
    linenoise_set_highlight_callback(ctx, udata_highlight, &tag_hl);

    session_start(&s, ctx, "> ");
    session_feed(&s, "abc\x02\x02");  /* Cursor after 'a'. */
    CHECK(seen_h == &tag_h && pos_h == 1, "hints: udata ok=%d pos=%zu", seen_h == &tag_h, pos_h);
    CHECK(seen_free == &tag_h, "free hints: udata ok=%d", seen_free == &tag_h);
    CHECK(seen_hl == &tag_hl && pos_hl == 1, "highlight: udata ok=%d pos=%zu", seen_hl == &tag_hl, pos_hl);
    session_feed(&s, "\t");
    CHECK(seen_c == &tag_c && pos_c == 1, "completion: udata ok=%d pos=%zu", seen_c == &tag_c, pos_c);
    session_stop(&s);
    linenoise_context_destroy(ctx);
}

/* Completion replaces buf[start, pos) with the candidate. */
static size_t span_start;
static const char *span_cand;
static void span_complete(const char *buf, size_t pos, linenoise_completions_t *lc,
                          void *udata) {
    (void)buf; (void)pos; (void)udata;
    lc->start = span_start;
    linenoise_add_completion(lc, span_cand);
}

/* Start a session, type 'keys', TAB, then Ctrl-B to accept. */
static void span_run(session_t *s, linenoise_context_t *ctx, const char *keys,
                     size_t start, const char *cand) {
    span_start = start;
    span_cand = cand;
    session_start(s, ctx, "> ");
    session_feed(s, keys);
    session_feed(s, "\t\x02");
}

static void test_completion_span(void) {
    linenoise_context_t *ctx = linenoise_context_create();
    session_t s;
    linenoise_set_completion_callback(ctx, span_complete, NULL);

    span_run(&s, ctx, "abc\x02", 1, "XYZ");  /* Replace "b", keep "c". */
    CHECK(strcmp(s.buf, "aXYZc") == 0, "span: \"%s\"", s.buf);
    CHECK(s.st.pos == 3, "span: pos=%zu after accept and Ctrl-B", s.st.pos);
    session_feed(&s, "\x1a");
    CHECK(strcmp(s.buf, "abc") == 0, "undo after accept: \"%s\"", s.buf);
    session_stop(&s);

    span_run(&s, ctx, "abc\x02", 100, "XYZ");  /* start > pos: insert. */
    CHECK(strcmp(s.buf, "abXYZc") == 0, "start past cursor: \"%s\"", s.buf);
    session_stop(&s);

    span_run(&s, ctx, "a\xc3\xa9", 2, "XYZ");  /* start inside U+00E9. */
    CHECK(strcmp(s.buf, "aXYZ") == 0, "start inside UTF-8: \"%s\"", s.buf);
    session_stop(&s);

    linenoise_context_destroy(ctx);
}

/* A candidate longer than a fixed buffer is truncated, and len matches. */
static void test_completion_fixed_buffer(void) {
    linenoise_context_t *ctx = linenoise_context_create();
    linenoise_state_t st;
    char buf[8];
    int in[2];
    int out = open("/dev/null", O_WRONLY);

    span_start = 0;
    span_cand = "0123456789";
    linenoise_set_completion_callback(ctx, span_complete, NULL);
    if (pipe(in) == -1) return;
    fcntl(in[0], F_SETFL, O_NONBLOCK);
    linenoise_edit_start(ctx, &st, in[0], out, buf, sizeof(buf), "> ");
    if (write(in[1], "h\t\x02", 3) != 3) return;
    for (int i = 0; i < 4; i++) linenoise_edit_feed(&st);
    CHECK(strcmp(buf, "0123456") == 0 && st.len == 7, "fixed buffer: \"%s\" len=%zu", buf, st.len);
    linenoise_edit_stop(&st);
    close(in[0]); close(in[1]); close(out);
    linenoise_context_destroy(ctx);
}

static void test_one_session_per_context(void) {
    linenoise_context_t *c = linenoise_context_create();
    session_t s1, s2;

    CHECK(session_start(&s1, c, "1> ") == 0, "first start failed");
    CHECK(session_start(&s2, c, "2> ") == -1, "second start on same context succeeded");
    CHECK(linenoise_get_error() == LINENOISE_ERR_INVALID, "error %d", (int)linenoise_get_error());
    session_stop(&s1);
    close(s2.in[0]); close(s2.in[1]); close(s2.out);

    /* The context is usable again after stop. */
    CHECK(session_start(&s2, c, "2> ") == 0, "start after stop failed");
    session_stop(&s2);
    linenoise_context_destroy(c);
}

/* A width change is picked up on the next key, or at once by
 * linenoise_resize(). */
static void test_resize(void) {
    linenoise_context_t *ctx = linenoise_context_create();
    session_t s;
    session_start(&s, ctx, "> ");
    CHECK(s.st.cols == 80, "cols at start: %zu", s.st.cols);

    setenv("LINENOISE_COLS", "40", 1);
    session_feed(&s, "a");
    CHECK(s.st.cols == 40, "cols after key: %zu", s.st.cols);

    setenv("LINENOISE_COLS", "30", 1);
    linenoise_resize(&s.st);
    CHECK(s.st.cols == 30, "cols after linenoise_resize(): %zu", s.st.cols);

    setenv("LINENOISE_COLS", "80", 1);
    session_stop(&s);
    linenoise_context_destroy(ctx);
}

/* Ctrl+Left/Right move by words; any whitespace separates words. */
static void test_word_keys(void) {
    linenoise_context_t *ctx = linenoise_context_create();
    session_t s;
    session_start(&s, ctx, "> ");

    session_feed(&s, "foo bar\x1b[1;5D");
    CHECK(s.st.pos == 4, "Ctrl+Left: pos=%zu", s.st.pos);
    session_feed(&s, "\x01\x1b[1;5C");
    CHECK(s.st.pos == 4, "Ctrl+Right: pos=%zu", s.st.pos);
    session_stop(&s);

    /* No completion callback, so TAB is inserted as text. */
    session_start(&s, ctx, "> ");
    session_feed(&s, "foo\tbar\x17");
    CHECK(strcmp(s.buf, "foo\t") == 0, "Ctrl-W after TAB: \"%s\"", s.buf);
    session_stop(&s);

    linenoise_context_destroy(ctx);
}

/* Lines typed while no session runs reach the tty in cooked mode, where
 * ICRNL turns Enter into \n. Each must come back as its own line. */
static void test_typeahead_kept(void) {
    const char *keys = "first\rsecond\r";
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    CHECK(master != -1 && grantpt(master) == 0 && unlockpt(master) == 0,
          "pty setup failed");
    if (master == -1) return;
    int slave = open(ptsname(master), O_RDWR | O_NOCTTY | O_NONBLOCK);
    CHECK(slave != -1, "open pty slave failed");
    if (slave == -1) { close(master); return; }
    fcntl(master, F_SETFL, O_NONBLOCK);

    struct termios t;
    tcgetattr(slave, &t);
    t.c_iflag |= ICRNL;
    t.c_lflag |= ICANON;
    tcsetattr(slave, TCSANOW, &t);
    CHECK(write(master, keys, strlen(keys)) == (ssize_t)strlen(keys), "write keys");

    /* The line discipline queues input asynchronously; wait until it has. */
    int queued = 0;
    for (int i = 0; i < 100 && queued < (int)strlen(keys); i++) {
        ioctl(slave, FIONREAD, &queued);
        if (queued < (int)strlen(keys)) usleep(10000);
    }
    CHECK(queued == (int)strlen(keys), "queued %d bytes", queued);

    unsetenv("LINENOISE_ASSUME_TTY");
    linenoise_context_t *ctx = linenoise_context_create();
    const char *want[] = {"first", "second"};
    for (int n = 0; n < 2; n++) {
        linenoise_state_t st;
        char buf[64], junk[4096];
        char *line = linenoise_edit_more;
        int started = linenoise_edit_start(ctx, &st, slave, slave, buf, sizeof(buf), "> ") == 0;
        CHECK(started, "edit_start %d: errno=%d", n, errno);
        if (!started) break;
        for (int i = 0; i < 64 && line == linenoise_edit_more; i++) {
            line = linenoise_edit_feed(&st);
            while (read(master, junk, sizeof(junk)) > 0) {}  /* drain output */
        }
        linenoise_edit_stop(&st);
        int ok = line && line != linenoise_edit_more && strcmp(line, want[n]) == 0;
        CHECK(ok, "line %d: got \"%s\", want \"%s\"", n,
              line == linenoise_edit_more ? "(more)" : line ? line : "(null)", want[n]);
        if (line && line != linenoise_edit_more) linenoise_free(line);
        if (!ok) break;
    }
    linenoise_context_destroy(ctx);
    setenv("LINENOISE_ASSUME_TTY", "1", 1);
    close(slave);
    close(master);
}

/* Each thread runs many sessions on its own context. */
#define THREAD_ITERS 200

typedef struct {
    linenoise_context_t *ctx;
    pthread_t self;
    int wrong_thread;   /* completion callback ran on another thread */
    int bad_lines;
    char *history;
} thread_arg_t;

static thread_arg_t thread_args[2];

static void complete_thread(int idx) {
    if (!pthread_equal(pthread_self(), thread_args[idx].self))
        thread_args[idx].wrong_thread++;
}
static void complete_t0(const char *buf, size_t pos, linenoise_completions_t *lc,
                        void *udata) {
    (void)buf; (void)pos; (void)lc; (void)udata; complete_thread(0);
}
static void complete_t1(const char *buf, size_t pos, linenoise_completions_t *lc,
                        void *udata) {
    (void)buf; (void)pos; (void)lc; (void)udata; complete_thread(1);
}

static void *thread_main(void *p) {
    thread_arg_t *t = p;
    t->self = pthread_self();
    for (int i = 0; i < THREAD_ITERS; i++) {
        session_t s;
        if (session_start(&s, t->ctx, "t> ") == -1) { t->bad_lines++; continue; }
        /* Type "ab", TAB (no completions), Backspace, undo, ENTER. */
        char *line = session_feed(&s, "ab\t\x7f\x1a\r");
        session_stop(&s);
        if (line == NULL || line == linenoise_edit_more || strcmp(line, "ab") != 0)
            t->bad_lines++;
        if (line && line != linenoise_edit_more) {
            linenoise_history_add(t->ctx, line);
            linenoise_free(line);
        }
    }
    t->history = history_dump(t->ctx);
    return NULL;
}

static void test_threads_separate_contexts(void) {
    pthread_t th[2];
    for (int i = 0; i < 2; i++) {
        thread_args[i].ctx = linenoise_context_create();
        linenoise_set_completion_callback(thread_args[i].ctx, i ? complete_t1 : complete_t0, NULL);
    }
    for (int i = 0; i < 2; i++)
        pthread_create(&th[i], NULL, thread_main, &thread_args[i]);
    for (int i = 0; i < 2; i++) pthread_join(th[i], NULL);

    for (int i = 0; i < 2; i++) {
        thread_arg_t *t = &thread_args[i];
        CHECK(t->wrong_thread == 0, "thread %d: %d callbacks on the wrong thread", i, t->wrong_thread);
        CHECK(t->bad_lines == 0, "thread %d: %d bad lines", i, t->bad_lines);
        CHECK(t->history && strcmp(t->history, "ab") == 0, "thread %d: history \"%s\"",
              i, t->history ? t->history : "(null)");
        free(t->history);
        linenoise_context_destroy(t->ctx);
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);  /* Keep output if a test crashes. */
    setenv("LINENOISE_ASSUME_TTY", "1", 1);
    setenv("LINENOISE_COLS", "80", 1);

    test_interleaved_completion();
    test_interleaved_history();
    test_interleaved_undo();
    test_history_add_during_session();
    test_history_nav_after_add_during_session();
    test_ctrl_c_leaves_history_clean();
    test_setter_during_session();
    test_one_session_per_context();
    test_resize();
    test_callback_udata();
    test_completion_span();
    test_completion_fixed_buffer();
    test_word_keys();
    test_typeahead_kept();
    test_threads_separate_contexts();

    printf("test-context: %d checks, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
