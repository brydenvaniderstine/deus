#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "arena.h"
#include "ast.h"
#include "diag.h"
#include "lexer.h"
#include "parser.h"
#include "parser_state.h"

/* Test-only harness for the parser's golden tests (tests/parser - *.mn in,
 * *.ast out, docs/M2_PLAN.md's "Golden test format for ASTs"). Not the real
 * `manna` CLI, same role for the parser that main.c's lexer dump plays for
 * the lexer - never built into anything a user runs directly.
 *
 * Parses an expression, a block, or a whole file, picked by a one-token
 * peek: 'manna'/'struct'/'fn'/'[' means a whole file (Step 8 - these four
 * are the only ways a translation_unit or a top_level_item can start, so
 * this also correctly routes a file that's missing its version_decl -
 * e.g. starting directly with 'struct' - into parser_parse_file, where
 * the missing-version check actually lives); '{' means a block (Step 6-7:
 * every statement kind); anything else means an expression (Steps 4-5).
 * No existing block/expression golden test starts with any of the four
 * file-mode tokens, so this doesn't disturb them. */

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)size);
    if (size > 0) {
        fread(buf, 1, (size_t)size, f);
    }
    fclose(f);
    *out_len = (size_t)size;
    return buf;
}

/* '[' starts either an attribute (whole-file mode) or an array literal
 * (expression mode) - since #4 step 3 it can no longer imply a file by
 * itself. A file-level '[' is one or more `[ IDENT ]` groups followed by
 * 'fn' (the shape parse_function_decl's attribute loop accepts), so look
 * past as many groups as there are; anything else is an expression. Uses
 * the caller's lexer copy (a plain value struct, advanced here), `first`
 * being the already-read '['. */
static bool starts_with_attribute(Lexer *lx, Token first) {
    Token t = first;
    while (t.kind == TOK_LBRACKET) {
        if (lexer_next(lx).kind != TOK_IDENT) return false;
        if (lexer_next(lx).kind != TOK_RBRACKET) return false;
        t = lexer_next(lx);
    }
    return t.kind == TOK_KW_FN && first.kind == TOK_LBRACKET;
}

static void print_diags(const DiagList *diags) {
    for (size_t i = 0; i < diags->len; i++) {
        const Diagnostic *d = &diags->items[i];
        printf("%d:%d %s\n", d->span.start_line, d->span.start_col, d->message);
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: parser_dump <file.mn>\n");
        return 1;
    }

    size_t len;
    char *src = read_file(argv[1], &len);

    ParserState ps;
    parser_state_init(&ps, 16 * 1024 * 1024);
    if (setjmp(ps.fatal_target) != 0) {
        printf("ERRORS:\n");
        print_diags(&ps.diags);
        printf("\nTREE:\n(none)\n");
        parser_state_destroy(&ps);
        free(src);
        return 0;
    }

    Lexer peek_lx;
    lexer_init(&peek_lx, src, len);
    Token first = lexer_next(&peek_lx);
    bool is_file = (first.kind == TOK_KW_MANNA || first.kind == TOK_KW_STRUCT ||
                    first.kind == TOK_KW_FN || starts_with_attribute(&peek_lx, first));
    bool is_block = !is_file && first.kind == TOK_LBRACE;

    Lexer lx;
    lexer_init(&lx, src, len);
    Expr *e = NULL;
    Stmt *s = NULL;
    NodeList items = {0};
    bool file_ok = false;
    if (is_file) {
        file_ok = parser_parse_file(&ps, &lx, &items);
    } else if (is_block) {
        s = parser_parse_block(&ps, &lx);
    } else {
        e = parser_parse_expression(&ps, &lx);
    }

    if (ps.diags.len > 0) {
        printf("ERRORS:\n");
        print_diags(&ps.diags);
        printf("\nTREE:\n");
    }

    bool have_tree = is_file ? file_ok : (is_block ? (s != NULL) : (e != NULL));
    if (have_tree) {
        if (is_file) {
            for (size_t i = 0; i < items.len; i++) {
                decl_print(stdout, (const Decl *)items.items[i]);
                printf("\n");
            }
        } else if (is_block) {
            stmt_print(stdout, s, 0);
            printf("\n");
        } else {
            expr_print(stdout, e, 0);
            printf("\n");
        }
    } else {
        printf("(none)\n");
    }

    parser_state_destroy(&ps);
    free(src);
    return 0;
}
