#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "ir.h"
#include "ir_internal.h"
#include "lexer.h"
#include "parser.h"
#include "parser_state.h"

/* Built with -DNDEBUG (make test-ir-ndebug): the two asserts that stand in for the IR's other IR_UNIMPLEMENTED raisers are
 * gone, and the code after them must still stop the build with IR_UNIMPLEMENTED instead of running off the end of a function
 * or falling through. ir_test.c cannot check this: it needs asserts on. The states are made by overwriting a node's kind,
 * because the parser never builds either (parser.c:632, parser.c:1755 and 1765). */

static int check(const char *what, int ok) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    return !ok;
}

static int run(const char *name, int corrupt_stmt, const char *want) {
    static ParserState ps;
    static NodeList items;
    static IrState st;
    const char *src = "manna v0;\nfn f() { let x = 1; }\n";
    parser_state_init(&ps, 1 << 24);
    node_list_init(&items);
    Lexer lx;
    lexer_init(&lx, src, strlen(src));
    (void)parser_parse_file(&ps, &lx, &items);
    Stmt *let = ((Decl *)items.items[0])->as.function_decl.body->as.block.stmts.items[0];
    if (corrupt_stmt) let->kind = STMT_BLOCK;
    else ((Expr *)let->as.let_stmt.init)->kind = EXPR_NAMED_ARG;
    ir_state_init(&st, 1 << 20);
    IrModule *m = (IrModule *)0x1;
    IrStatus r = ir_build(&st, &ps.diags, &items, &m);
    int bad = check(name, r == IR_UNIMPLEMENTED && m == NULL && st.diags.len == 0 && st.unimplemented && strcmp(st.unimplemented, want) == 0);
    ir_state_destroy(&st);
    parser_state_destroy(&ps);
    return bad;
}

int main(void) {
#ifndef NDEBUG
    fprintf(stderr, "ir_ndebug_test must be built with -DNDEBUG\n");
    return 2;
#endif
    int bad = 0;
    bad |= run("NDEBUG: a bare block statement stops the build (IR_UNIMPLEMENTED, \"a bare block statement\")", 1, "a bare block statement");
    bad |= run("NDEBUG: a named argument outside a call stops the build (IR_UNIMPLEMENTED, \"this expression\")", 0, "this expression");
    return bad;
}
