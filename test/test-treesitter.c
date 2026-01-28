/* Quick test to debug tree-sitter query matching */
#include <stdio.h>
#include <string.h>
#include <tree_sitter/api.h>

extern const TSLanguage *tree_sitter_python(void);

static const char *PYTHON_HIGHLIGHT_QUERY =
    "[\"as\" \"assert\" \"async\" \"await\" \"break\" \"class\" \"continue\"\n"
    " \"def\" \"del\" \"elif\" \"else\" \"except\" \"finally\" \"for\" \"from\"\n"
    " \"global\" \"if\" \"import\" \"lambda\" \"nonlocal\" \"pass\" \"raise\"\n"
    " \"return\" \"try\" \"while\" \"with\" \"yield\" \"match\" \"case\"] @keyword\n"
    "[(integer) (float)] @number\n"
    "(string) @string\n"
    "(comment) @comment\n"
    "(identifier) @variable\n"
;

static void test_query(TSParser *parser, const char *code) {
    printf("\n=== Testing: ===\n%s\n", code);
    printf("=== Length: %zu ===\n", strlen(code));

    TSTree *tree = ts_parser_parse_string(parser, NULL, code, strlen(code));
    if (!tree) {
        printf("Parse failed!\n");
        return;
    }

    uint32_t error_offset;
    TSQueryError error_type;
    TSQuery *query = ts_query_new(
        tree_sitter_python(),
        PYTHON_HIGHLIGHT_QUERY,
        strlen(PYTHON_HIGHLIGHT_QUERY),
        &error_offset,
        &error_type
    );

    if (!query) {
        printf("Query creation failed at offset %u, error type %d\n", error_offset, error_type);
        ts_tree_delete(tree);
        return;
    }

    TSQueryCursor *cursor = ts_query_cursor_new();
    TSNode root = ts_tree_root_node(tree);
    ts_query_cursor_exec(cursor, query, root);

    printf("=== Captures: ===\n");
    TSQueryMatch match;
    uint32_t capture_index;
    int count = 0;

    while (ts_query_cursor_next_capture(cursor, &match, &capture_index)) {
        TSQueryCapture capture = match.captures[capture_index];
        uint32_t start = ts_node_start_byte(capture.node);
        uint32_t end = ts_node_end_byte(capture.node);
        uint32_t name_len;
        const char *capture_name = ts_query_capture_name_for_id(query, capture.index, &name_len);

        printf("  [%u-%u] @%.*s = \"", start, end, name_len, capture_name);
        for (uint32_t i = start; i < end && i < strlen(code); i++) {
            if (code[i] == '\n') printf("\\n");
            else putchar(code[i]);
        }
        printf("\"\n");
        count++;
    }
    printf("Total captures: %d\n", count);

    ts_query_cursor_delete(cursor);
    ts_query_delete(query);
    ts_tree_delete(tree);
}

int main(void) {
    TSParser *parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_python());

    test_query(parser, "def foo(x):");
    test_query(parser, "class Person:\n    def __init__(self, id):");
    test_query(parser, "def foo(x): return x+1");

    ts_parser_delete(parser);
    return 0;
}
