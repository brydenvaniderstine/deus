#include "ir.h"
#include <assert.h>
#include <stdalign.h>

/* AST -> typed IR (typed-IR steps 3-5): the parse-diagnostics gate, the module,
 * the top-level declarations and function signatures (ir_decls.c), and function
 * bodies that are empty. Statements, the one thing still to come
 * (docs/TYPED_IR.md section 7), are refused with IR_UNIMPLEMENTED rather than
 * half-built, so no golden can record a half-checked IR. */

/* A function body. Step 6 builds `return expr;` and nothing else: the expression is checked
 * against the function's return type. A `return;`, a return in a unit function (their
 * shape rules are step 9) and every other statement kind (step 7 onward) stop the build
 * with IR_UNIMPLEMENTED. */
static IrStmt *build_body(IrState *st, const IrFunc *f, const Stmt *body) {
    NodeList stmts;
    node_list_init(&stmts);
    for (size_t i = 0; i < body->as.block.stmts.len; i++) {
        const Stmt *s = body->as.block.stmts.items[i];
        if (s->kind != STMT_RETURN) ir_unimplemented(st, "statements other than return (step 7)");
        if (!s->as.return_stmt.value) ir_unimplemented(st, "bare return (step 9)");
        if (f->ret->kind == IR_T_UNIT) ir_unimplemented(st, "return in a unit function (step 9)");
        IrExpr *v = ir_check_expr(st, s->as.return_stmt.value, f->ret);
        node_list_push(st->arena, &stmts, ir_stmt_return(st, s->span, v));
    }
    return ir_stmt_block(st, body->span, stmts);
}

IrStatus ir_build(IrState *st, const DiagList *parse_diags, const NodeList *items, IrModule **out) {
    assert(parse_diags && items && out);
    *out = NULL;
    /* The gate. Policy, not a safety dependency (docs/TYPED_IR.md section 2.1):
     * the fuzz target will compile this line out (MANNA_FUZZ_IR_UNGATED, step
     * 16) to prove the builder survives whatever tree the parser produced. */
    if (parse_diags->len > 0) return IR_SKIPPED;

    ir_state_reset_for_build(st);
    size_t base = st->diags.len; /* diags accumulate across builds; only new ones are this build's */
    if (setjmp(st->fatal_target) != 0) {
        /* E0201 / E0203 (already recorded), or ir_unimplemented. Errors found before the
         * unwind win over IR_UNIMPLEMENTED: they are real whatever the rest of the file holds. */
        return st->unimplemented && st->diags.len == base ? IR_UNIMPLEMENTED : IR_ERRORS;
    }

    IrModule *m = arena_alloc(st->arena, sizeof(IrModule), alignof(IrModule));
    node_list_init(&m->structs);
    node_list_init(&m->funcs);
    if (!ir_declare_items(st, items, m)) return IR_ERRORS;
    /* ir_declare_items built every struct and every function SIGNATURE (module order
     * is declaration order); the bodies are filled in here. */
    size_t fi = 0;
    for (size_t i = 0; i < items->len; i++) {
        const Decl *d = items->items[i];
        if (d->kind == DECL_STRUCT) continue;
        const Stmt *body = d->as.function_decl.body;
        assert(body && body->kind == STMT_BLOCK); /* a clean parse guarantees a block body */
        IrFunc *f = m->funcs.items[fi++];
        st->progress = d->span;
        f->body = build_body(st, f, body);
    }
    if (st->diags.len > base) return IR_ERRORS;
    *out = m;
    return IR_BUILT;
}
