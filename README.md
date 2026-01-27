# Linenoise 2.0

This is a friendly fork of antirez's [linenoise](https://github.com/antirez/linenoise):  a minimal, zero-config, BSD licensed, readline replacement used in Redis, MongoDB, Android and many other projects.

This version tries to be cross-platform and includes some additional features:

## Features

* Single and multi-line editing mode with the usual key bindings
* History handling with file persistence
* Tab completion
* Hints (suggestions at the right of the prompt as you type)
* Syntax highlighting with callback-based colorization
* Tree-sitter integration for language-aware highlighting (optional)
* Undo/redo support (Ctrl+Z/Ctrl+Y)
* Word movement and deletion (Ctrl+Arrow, Alt+B/F/D)
* Mouse support (click to position cursor)
* Multiplexing mode with prompt hiding/restoring for asynchronous output
* Dynamic buffers for unlimited input length
* Full UTF-8 support (multi-byte characters, emoji, grapheme clusters)
* Custom memory allocator support
* Cross-platform: Linux, macOS, Windows 10+
* Context-based API for multiple independent instances
* About ~1100 lines of BSD license source code (core library)
* Only uses a subset of VT100 escapes (ANSI.SYS compatible)

## What's New in 2.0

Version 2.0 is a major rewrite with a new context-based API:

* **Breaking change**: All functions now use snake_case naming and require a context
* Multiple independent linenoise instances can coexist
* Thread-safe when using separate contexts per thread
* See [CHANGELOG.md](CHANGELOG.md) for migration guide

## Quick Start

```c
#include "linenoise.h"

int main(void) {
    linenoise_context_t *ctx = linenoise_context_create();

    char *line;
    while ((line = linenoise_read(ctx, "prompt> ")) != NULL) {
        printf("You entered: %s\n", line);
        linenoise_history_add(ctx, line);
        linenoise_free(line);
    }

    linenoise_context_destroy(ctx);
    return 0;
}
```

## Building

```bash
mkdir build && cd build
cmake ..
make
```

### Build Options

```bash
cmake -DBUILD_SHARED_LIBS=ON ..   # Build shared library
cmake -DWITH_TREESITTER=OFF ..    # Disable tree-sitter support
cmake -DBUILD_TESTS=OFF ..        # Disable test suite
cmake -DBUILD_EXAMPLES=OFF ..     # Disable examples
```

## API Overview

For detailed API documentation, see [API_GUIDE.md](API_GUIDE.md).

### Context Management

```c
linenoise_context_t *ctx = linenoise_context_create();
// ... use the context ...
linenoise_context_destroy(ctx);
```

### Basic Line Reading

```c
char *line = linenoise_read(ctx, "prompt> ");
if (line == NULL) {
    linenoise_error_t err = linenoise_get_error();
    if (err == LINENOISE_ERR_EOF) {
        // Ctrl+D pressed
    }
} else {
    // Process line
    linenoise_free(line);
}
```

### Multi-line Mode

```c
linenoise_set_multiline(ctx, 1);  // Enable
linenoise_set_multiline(ctx, 0);  // Disable
```

### History

```c
linenoise_history_add(ctx, line);
linenoise_history_set_max_len(ctx, 1000);
linenoise_history_save(ctx, "~/.myapp_history");
linenoise_history_load(ctx, "~/.myapp_history");
```

### Tab Completion

```c
void completion_callback(const char *buf, linenoise_completions_t *lc) {
    if (buf[0] == 'h') {
        linenoise_add_completion(lc, "hello");
        linenoise_add_completion(lc, "help");
    }
}

linenoise_set_completion_callback(ctx, completion_callback);
```

### Hints

```c
char *hints_callback(const char *buf, int *color, int *bold) {
    if (strcmp(buf, "git") == 0) {
        *color = 35;  // Magenta
        *bold = 0;
        return " <command> [options]";
    }
    return NULL;
}

linenoise_set_hints_callback(ctx, hints_callback);
```

### Syntax Highlighting

```c
void highlight_callback(const char *buf, char *colors, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (isdigit(buf[i])) {
            colors[i] = 3;  // Yellow for numbers
        }
    }
}

linenoise_set_highlight_callback(ctx, highlight_callback);
```

Color values: 0=default, 1=red, 2=green, 3=yellow, 4=blue, 5=magenta, 6=cyan, 7=white. Add 8 for bold.

### Mask Mode (Passwords)

```c
linenoise_set_mask_mode(ctx, 1);  // Show asterisks
linenoise_set_mask_mode(ctx, 0);  // Normal display
```

### Mouse Support

```c
linenoise_set_mouse_mode(ctx, 1);  // Enable click-to-position
```

### Non-blocking API

```c
linenoise_state_t state;
char buf[4096];

linenoise_edit_start(ctx, &state, STDIN_FILENO, STDOUT_FILENO,
                     buf, sizeof(buf), "prompt> ");

// In your event loop:
char *line = linenoise_edit_feed(&state);
if (line == linenoise_edit_more) {
    // Still editing, wait for more input
} else if (line == NULL) {
    // Ctrl+C or Ctrl+D
    linenoise_edit_stop(&state);
} else {
    // Got a complete line
    linenoise_edit_stop(&state);
    process_line(line);
    linenoise_free(line);
}

// To print while editing:
linenoise_hide(&state);
printf("Status update!\n");
linenoise_show(&state);
```

## Tree-sitter Syntax Highlighting

The library includes optional tree-sitter based syntax highlighting. A Lua example is provided:

```bash
./build/linenoise-lua
```

Type Lua code to see real-time syntax highlighting:
- Keywords - bold magenta
- Strings - green
- Numbers - yellow
- Comments - cyan
- Functions - bold blue

See [API_GUIDE.md](API_GUIDE.md#tree-sitter-syntax-highlighting) for adding support for other languages.

## Running the Tests

```bash
make test
```

The test suite includes a VT100 terminal emulator that visually displays linenoise output in real-time. Tests cover:

* Basic typing and cursor movement
* Backspace and delete operations
* UTF-8 multi-byte characters (accented letters, CJK)
* Emoji and grapheme clusters (skin tones, ZWJ sequences)
* Horizontal scrolling for long lines
* Multi-line mode editing and navigation
* History navigation
* Word and line deletion (Ctrl-W, Ctrl-U)

## Key Bindings

| Key | Action |
|-----|--------|
| Left, Ctrl+B | Move cursor left |
| Right, Ctrl+F | Move cursor right |
| Home, Ctrl+A | Move to start of line |
| End, Ctrl+E | Move to end of line |
| Ctrl+Left, Alt+B | Move word left |
| Ctrl+Right, Alt+F | Move word right |
| Backspace, Ctrl+H | Delete character left |
| Delete, Ctrl+D | Delete character right (or EOF if empty) |
| Ctrl+W | Delete word left |
| Alt+D | Delete word right |
| Ctrl+U | Delete entire line |
| Ctrl+K | Delete to end of line |
| Ctrl+T | Transpose characters |
| Ctrl+Z | Undo |
| Ctrl+Y | Redo |
| Up, Ctrl+P | Previous history entry |
| Down, Ctrl+N | Next history entry |
| Tab | Trigger completion |
| Ctrl+C | Cancel line |
| Ctrl+D | EOF on empty line |
| Ctrl+L | Clear screen |
| Enter | Accept line |

## Tested Platforms

* Linux (text console, xterm, KDE terminal, Buildroot vt100)
* macOS (Terminal.app, iTerm)
* Windows 10+ (with Virtual Terminal mode)
* FreeBSD, OpenBSD
* IBM AIX 6.1
* Emacs comint mode ($TERM = dumb)

## License

BSD 2-Clause License. See the source files for full license text.

## Related Projects

* [Linenoise NG](https://github.com/arangodb/linenoise-ng) - Fork with additional features, uses C++
* [Linenoise-swift](https://github.com/andybest/linenoise-swift) - Swift reimplementation
