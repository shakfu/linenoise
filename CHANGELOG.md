# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [2.0.0] - 2026-01-28

### Breaking Changes

This release introduces a completely new API with snake_case naming conventions
and context-based state management. The legacy camelCase global API has been removed.

#### Type Renames
- `linenoiseCompletions` -> `linenoise_completions_t`
- `linenoiseCompletionCallback` -> `linenoise_completion_cb_t`
- `linenoiseHintsCallback` -> `linenoise_hints_cb_t`
- `linenoiseFreeHintsCallback` -> `linenoise_free_hints_cb_t`
- `struct linenoiseState` -> `linenoise_state_t`
- `linenoiseEditMore` -> `linenoise_edit_more`

#### Removed Functions (use context-based replacements)
- `linenoise()` - use `linenoise_read(ctx, prompt)`
- `linenoiseHistoryAdd()` - use `linenoise_history_add(ctx, line)`
- `linenoiseHistorySetMaxLen()` - use `linenoise_history_set_max_len(ctx, len)`
- `linenoiseHistorySave()` - use `linenoise_history_save(ctx, filename)`
- `linenoiseHistoryLoad()` - use `linenoise_history_load(ctx, filename)`
- `linenoiseSetMultiLine()` - use `linenoise_set_multiline(ctx, enable)`
- `linenoiseMaskModeEnable()` - use `linenoise_set_mask_mode(ctx, 1)`
- `linenoiseMaskModeDisable()` - use `linenoise_set_mask_mode(ctx, 0)`
- `linenoiseSetCompletionCallback()` - use `linenoise_set_completion_callback(ctx, fn)`
- `linenoiseSetHintsCallback()` - use `linenoise_set_hints_callback(ctx, fn)`
- `linenoiseSetFreeHintsCallback()` - use `linenoise_set_free_hints_callback(ctx, fn)`
- `linenoiseAddCompletion()` - use `linenoise_add_completion(lc, text)`
- `linenoiseClearScreen()` - use `linenoise_clear_screen(ctx)`
- `linenoiseFree()` - use `linenoise_free(ptr)`
- `linenoisePrintKeyCodes()` - use `linenoise_print_key_codes()`

#### Non-blocking API Changes
- `linenoiseEditStart()` - use `linenoise_edit_start(ctx, state, ...)`
- `linenoiseEditFeed()` - use `linenoise_edit_feed(state)`
- `linenoiseEditStop()` - use `linenoise_edit_stop(state)`
- `linenoiseHide()` - use `linenoise_hide(state)`
- `linenoiseShow()` - use `linenoise_show(state)`

### Added

#### Context-Based API
- `linenoise_context_create()` - Create a new linenoise context
- `linenoise_context_destroy()` - Destroy a context and free resources
- All configuration, history, and editing functions now take a context parameter
- Multiple independent linenoise instances can coexist (thread-safe when using separate contexts)

#### Modular Architecture
- Extracted UTF-8 handling into `src/utf8.c` and `internal/utf8.h`
  - Full grapheme cluster support (ZWJ sequences, skin tone modifiers, regional indicators)
  - Proper display width calculation for CJK and other wide characters
- Created terminal abstraction layer in `internal/terminal.h` and `src/terminal_posix.c`
  - Prepares codebase for Windows support
  - Clean separation of terminal I/O from editing logic
- Created history module in `src/history.c` and `internal/history.h`
  - Opaque `history_t` struct for encapsulation
  - FIFO rotation, duplicate prevention, secure file I/O
- Created completion module in `src/completion.c` and `internal/completion.h`
  - Reusable completion list management
  - Dynamic array growth for candidates
- Created key parser module in `src/keyparser.c` and `internal/keyparser.h`
  - State machine for escape sequence parsing
  - Support for arrows, function keys, modifiers (Ctrl/Alt/Shift)
  - Configurable escape timeout for standalone ESC key
- Created rendering module in `src/render.c` and `internal/render.h`
  - Pure functions for escape sequence generation
  - Separated from I/O for testability
  - Single-line and multi-line rendering support

#### Windows Support
- Added Windows 10+ support using Virtual Terminal (VT) mode
  - `SetConsoleMode()` with `ENABLE_VIRTUAL_TERMINAL_PROCESSING`
  - `SetConsoleMode()` with `ENABLE_VIRTUAL_TERMINAL_INPUT`
  - Existing ANSI escape sequences work unchanged on Windows 10+
- Created `src/terminal_windows.c` implementing terminal abstraction for Windows
- Added Windows compatibility in `linenoise.c`:
  - Platform-specific includes and macros
  - Windows read/write wrapper functions
  - Conditional compilation for POSIX vs Windows code
- Legacy console support for pre-Windows 10 (basic functionality)
- Runtime VT mode detection with automatic fallback

#### Build System
- Added CMakeLists.txt for cross-platform building
  - Automatic platform detection
  - Builds static library, example application, and tests
  - Optional shared library build (`-DBUILD_SHARED_LIBS=ON`)
  - CMake package config for `find_package(linenoise)` support
- Added GitHub Actions CI workflow (`.github/workflows/ci.yml`)
  - Linux builds with GCC and Clang
  - macOS builds
  - Windows builds with MSVC and MinGW

### Fixed

#### Critical Bug Fixes (P0)
- **Memory leak in completion handling**: Fixed `free_completions()` logic that was
  incorrectly checking `lc != &ctable` instead of `lc == &ctable`, causing memory
  leaks when using the internal completion table.

- **Hanging on partial escape sequences**: Added timeout to escape sequence reads
  using `select()` with 100ms timeout. Previously, pressing ESC alone would cause
  the program to hang waiting for additional escape sequence bytes.

- **File permission race condition**: Fixed race condition in `linenoiseHistorySave()`
  where file permissions could be incorrect between `fopen()` and `fchmod()`. Now
  uses `open()` with `O_CREAT|O_TRUNC` and explicit mode 0600, then `fdopen()`.

### Migration Guide

To migrate from 1.x to 2.0:

1. Create a context at startup:
   ```c
   linenoise_context_t *ctx = linenoise_context_create();
   ```

2. Update function calls to use new names and pass context:
   ```c
   // Old:
   linenoiseSetMultiLine(1);
   linenoiseSetCompletionCallback(completion);
   char *line = linenoise("prompt> ");
   linenoiseHistoryAdd(line);
   free(line);

   // New:
   linenoise_set_multiline(ctx, 1);
   linenoise_set_completion_callback(ctx, completion);
   char *line = linenoise_read(ctx, "prompt> ");
   linenoise_history_add(ctx, line);
   linenoise_free(line);
   ```

3. Update completion callbacks to use new type names:
   ```c
   // Old:
   void completion(const char *buf, linenoiseCompletions *lc) {
       linenoiseAddCompletion(lc, "option");
   }

   // New:
   void completion(const char *buf, linenoise_completions_t *lc) {
       linenoise_add_completion(lc, "option");
   }
   ```

4. Destroy context at cleanup:
   ```c
   linenoise_context_destroy(ctx);
   ```

## [1.0.0] - Previous

The original linenoise library with camelCase API and global state.
