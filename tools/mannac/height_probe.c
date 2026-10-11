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

/* Test-only measurement harness for typed-IR step 2b (tools/measure_expr_height.sh).
 * Not part of any golden test and never built into anything a user runs.
 *
 * Parses one input (same mode dispatch as parser_dump), prints ONE summary
 * line to stdout:
 *     diags=<n> first=<code|-> maxh=<tallest expression tree height>
 * then walks the whole tree with the real AST printer into /dev/null - the
 * recursive walker whose host-stack use the bound exists to cap - and prints
 * "printed". A crash (stack overflow) shows up as a missing "printed" and a
 * signal exit; the script classifies on that. The dump itself is quadratic in
 * chain length (indentation per level), which is why it goes to /dev/null
 * and never to a file. */

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)size + 1);
    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    *out_len = n;
    return buf;
}

static unsigned max_h(unsigned a, unsigned b) { return a > b ? a : b; }

/* Heights come straight from Expr.height; only the STATEMENT nesting is
 * walked here (bounded by the depth bound), never an expression tree. */
static unsigned expr_h(const Expr *e) { return e ? e->height : 0; }
static unsigned stmt_h(const Stmt *s) {
    if (!s) return 0;
    unsigned m = 0;
    switch (s->kind) {
        case STMT_LET: return expr_h(s->as.let_stmt.init);
        case STMT_ASSIGN: return max_h(expr_h(s->as.assign_stmt.target), expr_h(s->as.assign_stmt.value));
        case STMT_IF:
            return max_h(expr_h(s->as.if_stmt.cond),
                         max_h(stmt_h(s->as.if_stmt.then_branch), stmt_h(s->as.if_stmt.else_branch)));
        case STMT_WHILE: return max_h(expr_h(s->as.while_stmt.cond), stmt_h(s->as.while_stmt.body));
        case STMT_FOR:
            return max_h(max_h(stmt_h(s->as.for_stmt.init), expr_h(s->as.for_stmt.cond)),
                         max_h(stmt_h(s->as.for_stmt.step), stmt_h(s->as.for_stmt.body)));
        case STMT_RETURN: return expr_h(s->as.return_stmt.value);
        case STMT_EXPR: return expr_h(s->as.expr_stmt.expr);
        case STMT_BLOCK:
            for (size_t i = 0; i < s->as.block.stmts.len; i++)
                m = max_h(m, stmt_h((const Stmt *)s->as.block.stmts.items[i]));
            return m;
        default: return 0;
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: height_probe <file.mn>\n");
        return 2;
    }
    size_t len;
    char *src = read_file(argv[1], &len);
    static ParserState ps; /* static: read after setjmp (C11 7.13.2.1), as parser_main.c */
    parser_state_init(&ps, 16 * 1024 * 1024); /* parser_dump's arena cap */
    if (setjmp(ps.fatal_target) != 0) {
        printf("diags=%zu first=%s maxh=0\n", ps.diags.len, ps.diags.len ? ps.diags.items[0].code : "-");
        return 0;
    }
    HarnessMode mode = harness_pick_mode(src, len);
    Lexer lx;
    lexer_init(&lx, src, len);
    Expr *e = NULL;
    Stmt *s = NULL;
    NodeList items = {0};
    bool have = false;
    if (mode == HARNESS_MODE_FILE) {
        have = parser_parse_file(&ps, &lx, &items);
    } else if (mode == HARNESS_MODE_BLOCK) {
        s = parser_parse_block(&ps, &lx);
        have = s != NULL;
    } else {
        e = parser_parse_expression(&ps, &lx);
        have = e != NULL;
    }
    unsigned maxh = 0;
    if (have) {
        if (mode == HARNESS_MODE_FILE) {
            for (size_t i = 0; i < items.len; i++) {
                const Decl *d = items.items[i];
                if (d->kind == DECL_FUNCTION) maxh = max_h(maxh, stmt_h(d->as.function_decl.body));
            }
        } else if (mode == HARNESS_MODE_BLOCK) {
            maxh = stmt_h(s);
        } else {
            maxh = expr_h(e);
        }
    }
    printf("diags=%zu first=%s maxh=%u\n", ps.diags.len, ps.diags.len ? ps.diags.items[0].code : "-", maxh);
    fflush(stdout);
    if (have) {
        FILE *sink = fopen("/dev/null", "w");
        if (!sink) { perror("/dev/null"); return 2; }
        if (mode == HARNESS_MODE_FILE) {
            for (size_t i = 0; i < items.len; i++) decl_print(sink, items.items[i]);
        } else if (mode == HARNESS_MODE_BLOCK) {
            stmt_print(sink, s, 0);
        } else {
            expr_print(sink, e, 0);
        }
        fclose(sink);
        printf("printed\n");
    }
    return 0;
}
