# Linenoise TODO

Prioritized task list derived from architectural review. Items ordered by dependency and impact.

---

## P0: Critical Bug Fixes

- [x] **Fix free_completions logic (line 702)**
  - Was: `if (lc != &ctable) free_completions(&ctable);`
  - Fixed: `if (lc == &ctable) free_completions(&ctable);`
  - Impact: Memory leak when using internal completion table

- [x] **Add timeout to escape sequence reads (lines 1420-1431)**
  - Was: Blocking reads for multi-byte escape sequences
  - Fixed: Added `readByteWithTimeout()` helper using `select()` with 100ms timeout
  - Prevents hanging on partial escape sequences (e.g., user pressing ESC alone)

- [x] **Fix umask/fchmod race condition (lines 1744-1763)**
  - Was: `umask()` then `fopen()` then `fchmod()`
  - Fixed: Use `open()` with `O_CREAT|O_TRUNC` and explicit mode 0600, then `fdopen()`
  - Eliminates race window where file could have wrong permissions

---

## P1: Foundation for Cross-Platform (Required for Windows)

### 1.1 Create Terminal Abstraction Layer

- [x] **Define terminal interface (`internal/terminal.h`)**
  - Created opaque `linenoise_terminal_t` type
  - Functions: `terminal_create`, `terminal_destroy`, `terminal_enable_raw`,
    `terminal_disable_raw`, `terminal_get_size`, `terminal_read_byte`,
    `terminal_write`, `terminal_is_tty`, `terminal_clear_screen`, `terminal_beep`

- [x] **Extract POSIX terminal code (`src/terminal_posix.c`)**
  - Implemented full terminal abstraction for POSIX
  - Encapsulates termios, raw mode, terminal size queries
  - Note: linenoise.c still uses direct calls (migration pending for P2)

- [x] **Verify all tests still pass** - 72/72 tests passing

### 1.2 Extract UTF-8 Module

- [x] **Create `internal/utf8.h` with public interface**
  - Full grapheme cluster support API
  - Codepoint width calculation
  - Classification functions (ZWJ, skin tone, regional indicators, etc.)

- [x] **Create `src/utf8.c`**
  - Moved all UTF-8 code from linenoise.c
  - ~280 lines of UTF-8 implementation

- [x] **Update linenoise.c to include utf8.h**
  - Added include and compatibility macros
  - Removed ~310 lines of duplicated UTF-8 code

- [x] **Updated Makefile for modular build**
  - Compiles utf8.o separately
  - Links with linenoise-example and linenoise-test

- [ ] **Update linenoise-test.c to use shared utf8 module** (deferred)
  - Test file keeps its own UTF-8 copy for now (static functions, no conflict)

- [ ] **Add unit tests for UTF-8 module (`test/test_utf8.c`)** (deferred)

### 1.3 Encapsulate Global State

- [x] **Create `linenoiseContext` structure**
  - Defined in linenoise.c with all former global state
  - Includes: termios, rawmode, maskmode, mlmode, history, callbacks

- [x] **Add context-aware API (new functions, snake_case)**
  - `linenoise_context_create()` / `linenoise_context_destroy()`
  - `linenoise_read(ctx, prompt)`
  - `linenoise_set_multiline()`, `linenoise_set_mask_mode()`
  - `linenoise_set_completion_callback()`, etc.
  - `linenoise_history_add()`, `linenoise_history_set_max_len()`
  - `linenoise_history_save()`, `linenoise_history_load()`
  - Non-blocking API: `linenoise_edit_start()`, `linenoise_edit_feed()`, `linenoise_edit_stop()`
  - Utilities: `linenoise_hide()`, `linenoise_show()`, `linenoise_free()`, `linenoise_clear_screen()`

- [x] **Remove legacy camelCase API (breaking change)**
  - Removed all legacy global API functions (linenoise(), linenoiseHistoryAdd(), etc.)
  - All public API now uses snake_case naming convention
  - Header version updated to VERSION 2.0
  - Context API temporarily swaps globals for internal calls (transitional)

---

## P2: Windows Support

### 2.1 Windows VT Mode (Windows 10+)

- [x] **Create `src/terminal_windows.c`**
  - Implements terminal abstraction for Windows
  - Uses `SetConsoleMode()` with `ENABLE_VIRTUAL_TERMINAL_PROCESSING`
  - Uses `SetConsoleMode()` with `ENABLE_VIRTUAL_TERMINAL_INPUT`
  - VT mode auto-detection with fallback to legacy console API
  - Existing escape sequences work as-is on Windows 10+

- [x] **Handle Windows-specific includes in linenoise.c**
  - Added `#ifdef _WIN32` blocks for Windows headers
  - Added compatibility macros (`isatty`, `strcasecmp`, etc.)
  - Added Windows read/write wrapper functions
  - Platform-specific terminal handling code

- [x] **Runtime platform detection**
  - VT mode availability checked at runtime
  - Falls back to native Console API if VT mode unavailable

- [ ] **Test on Windows 10/11** (requires Windows environment)

### 2.2 Windows Legacy Console (Optional, Pre-Win10)

- [x] **Legacy console support in `src/terminal_windows.c`**
  - Uses `ReadConsoleInput()` for keyboard input when VT unavailable
  - Uses native Console API for screen operations
  - Basic support included, full escape sequence translation deferred

- [ ] **Test on Windows 7/8** (requires Windows environment)

### 2.3 Build System

- [x] **Add CMakeLists.txt**
  - Cross-platform CMake build system
  - Automatic platform detection (Windows vs POSIX)
  - Builds static library, example, and tests
  - Optional shared library build
  - Package config for `find_package()` support

- [x] **Add CI/CD for Windows testing**
  - GitHub Actions workflow (`.github/workflows/ci.yml`)
  - Builds on Linux (GCC, Clang), macOS, and Windows (MSVC, MinGW)
  - Runs tests on all platforms

---

## P3: Modularization (Maintainability)

### 3.1 Extract History Module

- [ ] **Create `internal/history.h`**
  ```c
  typedef struct history_t history_t;
  history_t *history_create(int max_len);
  void history_destroy(history_t *h);
  int history_add(history_t *h, const char *line);
  const char *history_get(history_t *h, int index);
  int history_len(history_t *h);
  int history_save(history_t *h, const char *filename);
  int history_load(history_t *h, const char *filename);
  ```

- [ ] **Create `src/history.c`**
  - Move lines 1636-1762 from linenoise.c
  - Encapsulate history array in opaque struct

- [ ] **Add unit tests (`test/test_history.c`)**

### 3.2 Extract Completion Module

- [ ] **Create `internal/completion.h`**

- [ ] **Create `src/completion.c`**
  - Move lines 661-802 from linenoise.c
  - `completeLine()`, `free_completions()`, `refreshLineWithCompletion()`

### 3.3 Implement Key Parser

- [ ] **Create `internal/keyparser.h`**
  ```c
  typedef enum {
      KEY_CHAR, KEY_CTRL, KEY_ARROW_UP, KEY_ARROW_DOWN,
      KEY_ARROW_LEFT, KEY_ARROW_RIGHT, KEY_HOME, KEY_END,
      KEY_DELETE, KEY_BACKSPACE, KEY_TAB, KEY_ENTER,
      KEY_F1, KEY_F2, KEY_F3, KEY_F4, /* ... */
      KEY_UNKNOWN
  } keycode_t;

  typedef struct {
      keycode_t code;
      char utf8[8];
      int utf8_len;
      int modifiers;  // SHIFT, ALT, CTRL flags
  } key_event_t;

  int keyparser_read(terminal_ops_t *term, key_event_t *event);
  ```

- [ ] **Create `src/keyparser.c`**
  - State machine for escape sequence parsing
  - Support for extended sequences (F-keys, etc.)
  - Timeout handling for partial sequences

- [ ] **Update linenoiseEditFeed() to use key parser**

### 3.4 Separate Rendering from I/O

- [ ] **Create pure rendering function**
  ```c
  // Returns length written to outbuf
  int render_line(const linenoiseState *state, char *outbuf, size_t outbuf_size);
  ```

- [ ] **Add unit tests for rendering**
  - Test escape sequence generation without terminal

---

## P4: Code Quality Improvements

### 4.1 Replace Magic Numbers

- [ ] **Define constants for buffer sizes**
  ```c
  #define LINENOISE_MAX_LINE 4096
  #define LINENOISE_SEQ_BUF_SIZE 64
  #define LINENOISE_HISTORY_DEFAULT_LEN 100
  ```

- [ ] **Audit all hardcoded numbers**
  - Lines 122, 587, 603, 632, 836, etc.

### 4.2 Improve Error Handling

- [ ] **Check write() return values**
  - Lines 592, 626, 634, 649-651, 950, 1060, 1115, 1292

- [ ] **Add error reporting mechanism**
  ```c
  typedef enum {
      LINENOISE_OK = 0,
      LINENOISE_ERR_WRITE,
      LINENOISE_ERR_READ,
      LINENOISE_ERR_MEMORY,
      LINENOISE_ERR_NOT_TTY
  } linenoise_error_t;

  linenoise_error_t linenoiseGetLastError(void);
  const char *linenoiseErrorString(linenoise_error_t err);
  ```

### 4.3 Memory Allocation

- [ ] **Add custom allocator support**
  ```c
  typedef void *(*linenoise_malloc_fn)(size_t);
  typedef void (*linenoise_free_fn)(void *);
  typedef void *(*linenoise_realloc_fn)(void *, size_t);

  void linenoiseSetAllocator(
      linenoise_malloc_fn malloc_fn,
      linenoise_free_fn free_fn,
      linenoise_realloc_fn realloc_fn
  );
  ```

---

## P5: Enhancements (Future)

### 5.1 Extended Key Support

- [ ] **F-keys (F1-F12)**
- [ ] **Page Up / Page Down**
- [ ] **Ctrl+Arrow for word movement**
- [ ] **Alt+Backspace for word delete**

### 5.2 Mouse Support

- [ ] **Enable mouse tracking**
- [ ] **Click to position cursor**
- [ ] **Selection support**

### 5.3 Dynamic Buffer

- [ ] **Auto-growing edit buffer**
  ```c
  int linenoiseEditStartDynamic(linenoiseState *l, int ifd, int ofd,
                                size_t initial_size, const char *prompt);
  ```

### 5.4 Syntax Highlighting

- [ ] **Callback for colorizing input**
  ```c
  typedef void (*linenoiseHighlightCallback)(
      const char *buf, char *colors, size_t len);
  void linenoiseSetHighlightCallback(linenoiseHighlightCallback fn);
  ```

### 5.5 Undo/Redo

- [ ] **Maintain edit history stack**
- [ ] **Ctrl+Z for undo, Ctrl+Y for redo**

---

## File Structure (Target State)

```
linenoise/
  include/
    linenoise.h              # Public API
  internal/
    utf8.h                   # UTF-8 utilities
    terminal.h               # Terminal abstraction
    history.h                # History management
    completion.h             # Tab completion
    keyparser.h              # Input parsing
  src/
    linenoise.c              # Main API glue (~200 LOC)
    utf8.c                   # UTF-8 implementation (~350 LOC)
    terminal_posix.c         # POSIX backend (~200 LOC)
    terminal_windows_vt.c    # Windows VT backend (~200 LOC)
    terminal_windows_legacy.c # Windows legacy backend (~300 LOC)
    history.c                # History implementation (~150 LOC)
    completion.c             # Completion implementation (~150 LOC)
    keyparser.c              # Key parser (~200 LOC)
  test/
    linenoise-test.c         # Integration tests
    test_utf8.c              # UTF-8 unit tests
    test_history.c           # History unit tests
  CMakeLists.txt
  Makefile
  README.md
  LICENSE
```

---

## Progress Tracking

| Priority | Category                | Items | Done |
|----------|-------------------------|-------|------|
| P0       | Critical Bug Fixes      | 3     | 3    |
| P1       | Foundation (Windows)    | 12    | 11   |
| P2       | Windows Support         | 8     | 6    |
| P3       | Modularization          | 10    | 0    |
| P4       | Code Quality            | 5     | 0    |
| P5       | Enhancements            | 5     | 0    |
| **Total**|                         | **43**| **20**|
