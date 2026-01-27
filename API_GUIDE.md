# Linenoise 2.0 API Guide

This guide covers the linenoise 2.0 API, which introduces context-based state management, snake_case naming conventions, and numerous new features.

## Table of Contents

1. [Quick Start](#quick-start)
2. [Context Management](#context-management)
3. [Basic Line Reading](#basic-line-reading)
4. [Configuration](#configuration)
5. [History](#history)
6. [Tab Completion](#tab-completion)
7. [Hints](#hints)
8. [Syntax Highlighting](#syntax-highlighting)
9. [Non-blocking API](#non-blocking-api)
10. [Dynamic Buffers](#dynamic-buffers)
11. [Word Movement & Editing](#word-movement--editing)
12. [Undo/Redo](#undoredo)
13. [Mouse Support](#mouse-support)
14. [Error Handling](#error-handling)
15. [Custom Memory Allocators](#custom-memory-allocators)
16. [Key Bindings Reference](#key-bindings-reference)

---

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

---

## Context Management

All linenoise operations require a context. Contexts are independent and thread-safe when used separately.

### Creating a Context

```c
linenoise_context_t *ctx = linenoise_context_create();
if (ctx == NULL) {
    // Handle allocation failure
}
```

### Destroying a Context

```c
linenoise_context_destroy(ctx);
```

This frees all resources associated with the context, including history.

### Multiple Contexts

You can create multiple independent contexts for different purposes:

```c
linenoise_context_t *main_ctx = linenoise_context_create();
linenoise_context_t *password_ctx = linenoise_context_create();

linenoise_set_mask_mode(password_ctx, 1);  // Hide input for passwords

// Each context has its own history, callbacks, and settings
```

---

## Basic Line Reading

### Blocking Read

```c
char *line = linenoise_read(ctx, "prompt> ");
if (line == NULL) {
    // Check what happened
    linenoise_error_t err = linenoise_get_error();
    if (err == LINENOISE_ERR_EOF) {
        printf("User pressed Ctrl+D\n");
    } else if (err == LINENOISE_ERR_INTERRUPTED) {
        printf("User pressed Ctrl+C\n");
    }
} else {
    // Use the line
    printf("Got: %s\n", line);
    linenoise_free(line);  // Always free when done
}
```

### Clearing the Screen

```c
linenoise_clear_screen(ctx);
```

---

## Configuration

### Multi-line Mode

Enable multi-line editing for long inputs that wrap:

```c
linenoise_set_multiline(ctx, 1);  // Enable
linenoise_set_multiline(ctx, 0);  // Disable (default)
```

### Mask Mode (Passwords)

Hide input for password entry:

```c
linenoise_set_mask_mode(ctx, 1);  // Show asterisks
linenoise_set_mask_mode(ctx, 0);  // Normal display (default)
```

### Mouse Mode

Enable click-to-position cursor:

```c
linenoise_set_mouse_mode(ctx, 1);  // Enable mouse support
linenoise_set_mouse_mode(ctx, 0);  // Disable (default)
```

When enabled, clicking within the edit area moves the cursor to that position.

---

## History

### Adding to History

```c
linenoise_history_add(ctx, line);
```

Duplicate consecutive entries are automatically ignored.

### Setting History Size

```c
linenoise_history_set_max_len(ctx, 1000);  // Keep up to 1000 entries
```

Default is 100 entries (`LINENOISE_DEFAULT_HISTORY_MAX_LEN`).

### Saving History

```c
if (linenoise_history_save(ctx, "~/.myapp_history") != 0) {
    perror("Failed to save history");
}
```

History files are created with mode 0600 (user read/write only).

### Loading History

```c
linenoise_history_load(ctx, "~/.myapp_history");
```

---

## Tab Completion

### Setting Up Completion

```c
void completion_callback(const char *buf, linenoise_completions_t *lc) {
    // buf contains what the user has typed so far
    if (buf[0] == 'h') {
        linenoise_add_completion(lc, "hello");
        linenoise_add_completion(lc, "hello world");
        linenoise_add_completion(lc, "help");
    }
}

linenoise_set_completion_callback(ctx, completion_callback);
```

### Completion Behavior

- Press TAB to trigger completion
- If multiple completions exist, cycle through them with TAB
- Press any other key to accept the current completion

---

## Hints

Hints appear to the right of the cursor as the user types, showing suggestions or help text.

### Setting Up Hints

```c
char *hints_callback(const char *buf, int *color, int *bold) {
    if (strcmp(buf, "git") == 0) {
        *color = 35;  // Magenta (ANSI color code)
        *bold = 0;
        return " <command> [options]";
    }
    return NULL;  // No hint
}

void free_hints_callback(void *hint) {
    // Only needed if hints_callback allocates memory
    // For static strings, this can be NULL
}

linenoise_set_hints_callback(ctx, hints_callback);
linenoise_set_free_hints_callback(ctx, free_hints_callback);  // Optional
```

### Color Codes

Use ANSI color codes for the `color` parameter:
- 31 = Red
- 32 = Green
- 33 = Yellow
- 34 = Blue
- 35 = Magenta
- 36 = Cyan
- 37 = White

---

## Syntax Highlighting

Add real-time syntax highlighting to input.

### Setting Up Highlighting

```c
void highlight_callback(const char *buf, char *colors, size_t len) {
    // colors array is pre-zeroed (0 = default color)
    // Fill it with color codes for each byte position

    for (size_t i = 0; i < len; i++) {
        if (isdigit(buf[i])) {
            colors[i] = 2;  // Green for numbers
        } else if (buf[i] == '"') {
            colors[i] = 3;  // Yellow for quotes
        }
    }
}

linenoise_set_highlight_callback(ctx, highlight_callback);
```

### Color Values

| Value | Color   | Bold Variant |
|-------|---------|--------------|
| 0     | Default | -            |
| 1     | Red     | 9            |
| 2     | Green   | 10           |
| 3     | Yellow  | 11           |
| 4     | Blue    | 12           |
| 5     | Magenta | 13           |
| 6     | Cyan    | 14           |
| 7     | White   | 15           |

Add 8 to any color for bold (e.g., 9 = bold red).

### Example: Keyword Highlighting

```c
void highlight_callback(const char *buf, char *colors, size_t len) {
    const char *keywords[] = {"if", "else", "while", "for", "return", NULL};

    for (size_t i = 0; i < len; ) {
        // Skip whitespace
        if (isspace(buf[i])) {
            i++;
            continue;
        }

        // Check for keywords
        for (int k = 0; keywords[k]; k++) {
            size_t klen = strlen(keywords[k]);
            if (i + klen <= len &&
                strncmp(buf + i, keywords[k], klen) == 0 &&
                (i + klen == len || !isalnum(buf[i + klen]))) {
                // Highlight keyword in bold blue
                for (size_t j = 0; j < klen; j++) {
                    colors[i + j] = 12;  // Bold blue
                }
                i += klen;
                goto next;
            }
        }
        i++;
        next:;
    }
}
```

---

## Non-blocking API

For event-driven applications, use the non-blocking API.

### Basic Usage

```c
linenoise_state_t state;
char buf[4096];

// Start editing
if (linenoise_edit_start(ctx, &state, STDIN_FILENO, STDOUT_FILENO,
                         buf, sizeof(buf), "prompt> ") == -1) {
    // Handle error
}

// In your event loop, when stdin is readable:
char *line = linenoise_edit_feed(&state);
if (line == linenoise_edit_more) {
    // User is still editing, continue waiting
} else if (line == NULL) {
    // Ctrl+C or Ctrl+D pressed
    linenoise_edit_stop(&state);
} else {
    // Got a complete line
    linenoise_edit_stop(&state);
    process_line(line);
    linenoise_free(line);
}
```

### Hiding/Showing the Line

Useful when you need to print something while the user is editing:

```c
linenoise_hide(&state);   // Clear the prompt temporarily
printf("Status update!\n");
linenoise_show(&state);   // Redraw the prompt
```

---

## Dynamic Buffers

For input without a fixed size limit:

```c
linenoise_state_t state;

// Start with dynamic buffer (initial 256 bytes, grows as needed)
if (linenoise_edit_start_dynamic(ctx, &state, STDIN_FILENO, STDOUT_FILENO,
                                  0, "prompt> ") == -1) {
    // Handle error
}

// Use normally - buffer grows automatically
char *line;
while ((line = linenoise_edit_feed(&state)) == linenoise_edit_more) {
    // Wait for input...
}

linenoise_edit_stop(&state);  // Frees the dynamic buffer

if (line) {
    // Process line (this is a separate copy, must be freed)
    linenoise_free(line);
}
```

### Initial Size

Pass 0 for default (256 bytes), or specify a custom initial size:

```c
linenoise_edit_start_dynamic(ctx, &state, -1, -1, 1024, "prompt> ");
```

---

## Word Movement & Editing

These functions are available for programmatic cursor control:

```c
// Move by word
linenoise_edit_move_word_left(&state);   // Ctrl+Left, Alt+b
linenoise_edit_move_word_right(&state);  // Ctrl+Right, Alt+f

// Delete word
linenoise_edit_delete_word_right(&state); // Alt+d
```

---

## Undo/Redo

Undo and redo are automatically available:

```c
linenoise_edit_undo(&state);  // Ctrl+Z
linenoise_edit_redo(&state);  // Ctrl+Y
```

- State is automatically saved before destructive operations
- Up to 100 undo levels are maintained
- Undo stack is cleared when starting a new line

---

## Mouse Support

When mouse mode is enabled:

```c
linenoise_set_mouse_mode(ctx, 1);
```

- **Left-click**: Moves cursor to clicked position
- Works with UTF-8 text (respects display widths)
- Automatically enabled/disabled with raw mode

The implementation uses SGR extended mouse mode for accurate coordinate reporting on modern terminals.

---

## Error Handling

### Checking Errors

```c
char *line = linenoise_read(ctx, "prompt> ");
if (line == NULL) {
    linenoise_error_t err = linenoise_get_error();
    switch (err) {
    case LINENOISE_OK:
        // Shouldn't happen if line is NULL
        break;
    case LINENOISE_ERR_EOF:
        printf("End of file (Ctrl+D)\n");
        break;
    case LINENOISE_ERR_INTERRUPTED:
        printf("Interrupted (Ctrl+C)\n");
        break;
    case LINENOISE_ERR_MEMORY:
        printf("Out of memory\n");
        break;
    case LINENOISE_ERR_NOT_TTY:
        printf("Not a terminal\n");
        break;
    default:
        printf("Error: %s\n", linenoise_error_string(err));
    }
}
```

### Error Codes

| Code | Meaning |
|------|---------|
| `LINENOISE_OK` | Success |
| `LINENOISE_ERR_ERRNO` | System error (check `errno`) |
| `LINENOISE_ERR_NOT_TTY` | Input is not a terminal |
| `LINENOISE_ERR_NOT_SUPPORTED` | Terminal not supported |
| `LINENOISE_ERR_READ` | Read error |
| `LINENOISE_ERR_WRITE` | Write error |
| `LINENOISE_ERR_MEMORY` | Memory allocation failed |
| `LINENOISE_ERR_INVALID` | Invalid argument |
| `LINENOISE_ERR_EOF` | End of file (Ctrl+D) |
| `LINENOISE_ERR_INTERRUPTED` | Interrupted (Ctrl+C) |

---

## Custom Memory Allocators

For integration with custom memory management:

```c
void *my_malloc(size_t size) {
    return my_allocator_alloc(size);
}

void my_free(void *ptr) {
    my_allocator_free(ptr);
}

void *my_realloc(void *ptr, size_t size) {
    return my_allocator_realloc(ptr, size);
}

// Call before any other linenoise functions
linenoise_set_allocator(my_malloc, my_free, my_realloc);
```

Pass `NULL` for any function to use the default implementation.

---

## Key Bindings Reference

### Navigation

| Key | Action |
|-----|--------|
| Left, Ctrl+B | Move cursor left |
| Right, Ctrl+F | Move cursor right |
| Home, Ctrl+A | Move to start of line |
| End, Ctrl+E | Move to end of line |
| Ctrl+Left, Alt+B | Move word left |
| Ctrl+Right, Alt+F | Move word right |

### Editing

| Key | Action |
|-----|--------|
| Backspace, Ctrl+H | Delete character left |
| Delete, Ctrl+D | Delete character right (or EOF if empty) |
| Ctrl+T | Transpose characters |
| Ctrl+U | Delete entire line |
| Ctrl+K | Delete to end of line |
| Ctrl+W | Delete word left |
| Alt+D | Delete word right |
| Alt+Backspace | Delete word left |

### History

| Key | Action |
|-----|--------|
| Up, Ctrl+P | Previous history entry |
| Down, Ctrl+N | Next history entry |
| Page Up | Previous history entry |
| Page Down | Next history entry |

### Undo/Redo

| Key | Action |
|-----|--------|
| Ctrl+Z | Undo |
| Ctrl+Y | Redo |

### Other

| Key | Action |
|-----|--------|
| Tab | Trigger completion |
| Ctrl+C | Cancel (returns NULL with INTERRUPTED error) |
| Ctrl+D | EOF on empty line (returns NULL with EOF error) |
| Ctrl+L | Clear screen |
| Enter | Accept line |

---

## Constants

```c
LINENOISE_MAX_LINE              // 4096 - Maximum line length (fixed buffers)
LINENOISE_DEFAULT_HISTORY_MAX_LEN  // 100 - Default history size
LINENOISE_SEQ_SIZE              // 64 - Internal escape sequence buffer
```

---

## Migration from 1.x

See [CHANGELOG.md](CHANGELOG.md) for detailed migration instructions. Key changes:

1. Create a context: `linenoise_context_t *ctx = linenoise_context_create();`
2. Update function names to snake_case
3. Pass context as first argument to most functions
4. Use new type names (`linenoise_completions_t`, etc.)
5. Call `linenoise_context_destroy(ctx)` at cleanup
