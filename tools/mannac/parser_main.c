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
 * Step 4 parses a single expression (parser_parse_expression), not a
 * whole file - full top-level parsing doesn't exist until Step 8. */

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

    Lexer lx;
    lexer_init(&lx, src, len);
    Expr *e = parser_parse_expression(&ps, &lx);

    if (ps.diags.len > 0) {
        printf("ERRORS:\n");
        print_diags(&ps.diags);
        printf("\nTREE:\n");
    }
    if (e) {
        expr_print(stdout, e, 0);
        printf("\n");
    } else {
        printf("(none)\n");
    }

    parser_state_destroy(&ps);
    free(src);
    return 0;
}
