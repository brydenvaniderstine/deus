#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "lexer.h"

/* Which parser entry point a test input is fed to. Shared by the golden-test
 * harness (parser_main.c) and the libFuzzer target (fuzz_parser.c), so the
 * two cannot drift apart: they did - the fuzz target routed every
 * '['-leading input to whole-file mode, so array literals and `[v; N]` at
 * the start of an input were never fuzzed as expressions (external audit of
 * v0, finding 4). Harness-only; the parser API takes no part in this.
 *
 * Picked by a one-token peek: 'manna'/'struct'/'fn' or an attribute prefix
 * means a whole file (these are the only ways a translation_unit or a
 * top_level_item can start, so this also routes a file that is missing its
 * version_decl - e.g. starting directly with 'struct' - into
 * parser_parse_file, where the missing-version check lives); '{' means a
 * block; anything else, including a '[' that is not an attribute, means an
 * expression. */
typedef enum { HARNESS_MODE_FILE, HARNESS_MODE_BLOCK, HARNESS_MODE_EXPRESSION } HarnessMode;

/* '[' starts either an attribute (whole-file mode) or an array literal
 * (expression mode) - since #4 step 3 it can no longer imply a file by
 * itself. A file-level '[' is one or more `[ IDENT ]` groups followed by
 * 'fn' (the shape parse_function_decl's attribute loop accepts), so look
 * past as many groups as there are; anything else is an expression. Uses
 * the caller's lexer copy (a plain value struct, advanced here), `first`
 * being the already-read '['. */
static inline bool harness_starts_with_attribute(Lexer *lx, Token first) {
    Token t = first;
    while (t.kind == TOK_LBRACKET) {
        if (lexer_next(lx).kind != TOK_IDENT) return false;
        if (lexer_next(lx).kind != TOK_RBRACKET) return false;
        t = lexer_next(lx);
    }
    return t.kind == TOK_KW_FN && first.kind == TOK_LBRACKET;
}

static inline HarnessMode harness_pick_mode(const char *src, size_t len) {
    Lexer peek_lx;
    lexer_init(&peek_lx, src, len);
    Token first = lexer_next(&peek_lx);
    if (first.kind == TOK_KW_MANNA || first.kind == TOK_KW_STRUCT || first.kind == TOK_KW_FN ||
        harness_starts_with_attribute(&peek_lx, first)) {
        return HARNESS_MODE_FILE;
    }
    return first.kind == TOK_LBRACE ? HARNESS_MODE_BLOCK : HARNESS_MODE_EXPRESSION;
}
