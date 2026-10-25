#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "arena.h"
#include "ast.h"
#include "diag.h"
#include "harness_mode.h"
#include "lexer.h"
#include "parser.h"
#include "parser_state.h"

/* Test-only harness for the parser's golden tests (tests/parser - *.mn in,
 * *.ast out, docs/M2_PLAN.md's "Golden test format for ASTs"). Not the real
 * `manna` CLI, same role for the parser that main.c's lexer dump plays for
 * the lexer - never built into anything a user runs directly.
 *
 * Parses an expression, a block, or a whole file, picked by a one-token
 * peek (harness_mode.h, shared with the fuzz target): 'manna'/'struct'/
 * 'fn' or an attribute prefix means a whole file (Step 8); '{' means a
 * block (Step 6-7: every statement kind); anything else, including an
 * array-literal '[', means an expression (Steps 4-5). */

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
    if (size > 0 && fread(buf, 1, (size_t)size, f) != (size_t)size) {
        perror(path);
        exit(1);
    }
    fclose(f);
    *out_len = (size_t)size;
    return buf;
}

/* parser_parse_expression and parser_parse_block parse ONE construct and
 * stop; neither says what follows it. In those two harness modes the harness
 * must, or a golden like `a: 1`, `1 + 2 )))` or `{ a; } b` only checks a
 * prefix and "passes" while ignoring the rest. Re-lex from the start and take
 * the first token at or after the construct's end (end column is exclusive,
 * and a parenthesized expression's span includes its parens); anything but
 * EOF is leftover input: E0105 "expected end of input, found X". Harness
 * only: the parser API is unchanged. File mode needs none of this - it
 * loops to EOF, so a stray token there is already an error of its own. */
static void report_leftover_input(ParserState *ps, const char *src, size_t len, Span end) {
    Lexer scan;
    lexer_init(&scan, src, len);
    Token t;
    do {
        t = lexer_next(&scan);
    } while (t.kind != TOK_EOF &&
             (t.line < end.end_line || (t.line == end.end_line && t.col < end.end_col)));
    if (t.kind != TOK_EOF) {
        const char *found = (t.kind >= TOK_ERROR_BAD_CHAR && t.error_msg)
                                ? t.error_msg
                                : arena_sprintf(ps->arena, "'%.*s'", (int)t.lexeme_len, t.lexeme);
        diag_add(ps->arena, &ps->diags, diag_expected(ps->arena, token_span(t), "end of input", found, NULL));
    }
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

    /* static: ps is modified after setjmp and read again after the longjmp
     * (the fatal path prints ps.diags). C11 7.13.2.1 makes such an
     * automatic, non-volatile object indeterminate; static storage is not
     * covered by that rule, and unlike `volatile` needs no casts at every
     * call that takes a ParserState *. */
    static ParserState ps;
    parser_state_init(&ps, 16 * 1024 * 1024);
    if (setjmp(ps.fatal_target) != 0) {
        printf("ERRORS:\n");
        print_diags(&ps.diags);
        printf("\nTREE:\n(none)\n");
        parser_state_destroy(&ps);
        free(src);
        return 0;
    }

    HarnessMode mode = harness_pick_mode(src, len); /* shared with fuzz_parser.c */
    bool is_file = (mode == HARNESS_MODE_FILE);
    bool is_block = (mode == HARNESS_MODE_BLOCK);

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
        if (s != NULL) {
            report_leftover_input(&ps, src, len, s->span); /* same gap as expression mode: `{ a; } b` */
        }
    } else {
        e = parser_parse_expression(&ps, &lx);
        if (e != NULL) {
            report_leftover_input(&ps, src, len, e->span);
        }
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
