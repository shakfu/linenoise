# Callback API redesign (3.0)

Status: implemented, unreleased (3.0.0). Open questions resolved as written:
one hint string, separate free setter, `pos` in the highlight callback.

## Problem

Every callback receives only the buffer. That causes three gaps:

1. Completion cannot see the cursor. Accepting a candidate replaces the whole
   buffer and moves the cursor to the end. Text after the cursor is lost.
2. Hints cannot see the cursor. The hint is drawn after the buffer, and the
   callback cannot tell whether the cursor is mid-line.
3. No callback receives user data or the context. Callers keep their state in
   globals. The bundled highlighters in `src/syntax/*.c` do the same
   (`static TSParser *parser`). Two contexts on two threads that use the same
   highlighter therefore race, despite the header promising separate contexts
   may run on separate threads.

## Proposal

Each callback gets the cursor offset `pos` and a `void *udata`. Completion also
gets a replacement span.

```c
typedef struct linenoise_completions {
    size_t len;
    char **cvec;
    size_t start;   /* Candidates replace buf[start, pos). Preset to 0. */
} linenoise_completions_t;

typedef void (linenoise_completion_cb_t)(const char *buf, size_t pos,
                                         linenoise_completions_t *lc,
                                         void *udata);
typedef char *(linenoise_hints_cb_t)(const char *buf, size_t pos,
                                     int *color, int *bold, void *udata);
typedef void (linenoise_free_hints_cb_t)(void *hint, void *udata);
typedef void (linenoise_highlight_cb_t)(const char *buf, char *colors,
                                        size_t len, size_t pos, void *udata);

void linenoise_set_completion_callback(linenoise_context_t *ctx,
                                       linenoise_completion_cb_t *fn,
                                       void *udata);
void linenoise_set_hints_callback(linenoise_context_t *ctx,
                                  linenoise_hints_cb_t *fn, void *udata);
void linenoise_set_highlight_callback(linenoise_context_t *ctx,
                                      linenoise_highlight_cb_t *fn,
                                      void *udata);
/* Unchanged; the free callback receives the hints callback's udata. */
void linenoise_set_free_hints_callback(linenoise_context_t *ctx,
                                       linenoise_free_hints_cb_t *fn);
```

All offsets are bytes into the UTF-8 buffer, matching `linenoise_state_t.pos`.

Example: complete the word before the cursor.

```c
void complete(const char *buf, size_t pos, linenoise_completions_t *lc,
              void *udata) {
    const struct symtab *tab = udata;
    size_t start = pos;
    while (start > 0 && !isspace((unsigned char)buf[start-1])) start--;
    lc->start = start;
    for (size_t i = 0; i < tab->n; i++)
        if (!strncmp(tab->names[i], buf + start, pos - start))
            linenoise_add_completion(lc, tab->names[i]);
}
```

## Semantics

### Completion

- `buf` is the whole line and `pos` the cursor. The callback can read text after
  the cursor, e.g. to skip a closing quote that is already present.
- Accepting candidate `c` makes the buffer `buf[0,start) + c + buf[pos,len)`.
  The cursor goes to `start + strlen(c)`.
- Validation (`start` comes from caller code):
  - `start > pos` is clamped to `pos`, which inserts at the cursor.
  - A `start` inside a UTF-8 sequence moves back to the sequence start.
  - A span that overlaps a folded paste range is refused with a beep, as
    Ctrl-T does.
- Accepting a candidate saves an undo step. It inserts through the existing edit
  path, so a dynamic buffer grows. A fixed buffer truncates, as it does today.
- The preview renders the composed line. Folds after `pos` stay folded, with
  their offsets shifted by the length change. Today the preview drops every fold.

### Hints

- The hint is still drawn after the end of the buffer.
- With `pos`, the callback can return NULL when the cursor is mid-line, or hint
  the word at the cursor. Showing only the untyped suffix stays the callback's
  job, as now.

### Highlight

- `buf` is the rendered text: folded ranges are replaced by their placeholders.
  `pos` is the cursor translated into that text.
- `pos` enables bracket matching. Nothing uses it yet. It is included because
  adding it later would break the API a second time.

### User data

- Each setter stores its own `udata`. Setting a callback replaces its `udata`.
- The library never dereferences or frees `udata`.

## Migration

The change is mechanical. Add the new parameters and pass `NULL` to the setters.

```c
/* 2.x */
void completion(const char *buf, linenoise_completions_t *lc);
linenoise_set_completion_callback(ctx, completion);

/* 3.0 */
void completion(const char *buf, size_t pos, linenoise_completions_t *lc,
                void *udata);
linenoise_set_completion_callback(ctx, completion, NULL);
```

`start` defaults to 0, so a ported completion callback that returns full-line
candidates behaves as before when the cursor is at the end. With the cursor
mid-line, the text after the cursor is now kept. Before, it was discarded.

Each signature change is a compile error, not a silent mismatch. Every changed
callback adds or reorders parameters of different types.

## Version

This is a 3.0.0 change. `[Unreleased]` already changes the size of
`linenoise_state_t`, so the next release breaks the ABI anyway. Shipping both
breaks in one release costs callers one migration.

## Alternatives rejected

- **Parallel `_ex` callbacks and setters.** These keep 2.x source compatibility,
  but leave two callback sets to document, test and keep in sync. The 2.0 API was
  released 2026-01-28, and the user base is small.
- **One `void *` per context** (`linenoise_set_user_data()`). A context often
  combines independent components: a bundled highlighter with its own parser,
  and an application completer with its own symbol table. Each needs its own
  pointer. This is also replxx's choice.
- **Pass `const linenoise_state_t *`.** It exposes editor internals to callbacks
  and ties them to the struct layout. It also gives the wrong text to
  highlighting and preview, which run on the rendered or composed line, not on
  `state->buf`.
- **A request struct per callback** (`linenoise_completion_req_t`). New fields
  could be added later without a break. That is more ceremony for four
  parameters, so it is not worth it yet.
- **replxx's word-based default span.** replxx presets `contextLen` from word
  break characters. Here, a preset of 0 keeps ported callbacks correct; a
  word-based preset would silently change what they replace. A callback picks
  word scope with one loop, as in the example.
- **Code point offsets, as in replxx.** Every other offset in this API is in
  bytes. Mixing the two units invites off-by-n bugs on non-ASCII input.

## Follow-up, out of scope

- Give each `src/syntax/*.c` highlighter a create/destroy pair that returns its
  parser state, passed as `udata`. This removes the file-scope statics.
- Unique history and Ctrl-R search.

## Open questions

1. Should the hints callback be able to return several hints to cycle through,
   as replxx's `replxx_hints` allows? This proposal keeps a single string.
2. Should `linenoise_set_hints_callback()` take the free function as well,
   so the pair is always set together? That is one less setter, but more churn.
3. Keep `pos` in the highlight callback with no current user, or omit it and
   accept a possible second break later?
