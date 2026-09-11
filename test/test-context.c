/* test-context.c -- In-process tests for context isolation.
 *
 * Drives the non-blocking API through pipes (LINENOISE_ASSUME_TTY) to check
 * that callbacks, history, undo and settings stay with their own context
 * when several sessions are active at once, or on separate threads.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
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
static void complete_a(const char *buf, linenoise_completions_t *lc) {
    (void)buf; (void)lc; calls_a++;
}
static void complete_b(const char *buf, linenoise_completions_t *lc) {
    (void)buf; (void)lc; calls_b++;
}

/* ========================= Tests ========================= */

static void test_interleaved_completion(void) {
    linenoise_context_t *a = linenoise_context_create();
    linenoise_context_t *b = linenoise_context_create();
    session_t sa, sb;
    calls_a = calls_b = 0;
    linenoise_set_completion_callback(a, complete_a);
    linenoise_set_completion_callback(b, complete_b);

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
    linenoise_set_completion_callback(c, complete_a);
    session_feed(&s, "x\t");
    CHECK(calls_a == 1, "callback set mid-session called %d times", calls_a);
    session_stop(&s);
    linenoise_context_destroy(c);
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
static void complete_t0(const char *buf, linenoise_completions_t *lc) {
    (void)buf; (void)lc; complete_thread(0);
}
static void complete_t1(const char *buf, linenoise_completions_t *lc) {
    (void)buf; (void)lc; complete_thread(1);
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
        linenoise_set_completion_callback(thread_args[i].ctx, i ? complete_t1 : complete_t0);
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
    test_threads_separate_contexts();

    printf("test-context: %d checks, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
