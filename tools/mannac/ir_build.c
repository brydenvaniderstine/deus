#include "ir.h"
#include <assert.h>
#include <stdalign.h>

/* AST -> typed IR (typed-IR steps 3-5): the parse-diagnostics gate, the module,
 * the top-level declarations and function signatures (ir_decls.c), and function
 * bodies that are empty. Statements, the one thing still to come
 * (docs/TYPED_IR.md section 7), are refused with IR_UNIMPLEMENTED rather than
 * half-built, so no golden can record a half-checked IR. */

static IrStatus unimplemented(IrState *st, const char *what) {
    st->unimplemented = what;
    return IR_UNIMPLEMENTED;
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
    if (setjmp(st->fatal_target) != 0) return IR_ERRORS; /* E0201 / E0203, already recorded */

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
        st->progress = d->span;
        if (body->as.block.stmts.len > 0) return unimplemented(st, "statements (steps 7-13)");
        NodeList no_stmts;
        node_list_init(&no_stmts);
        ((IrFunc *)m->funcs.items[fi++])->body = ir_stmt_block(st, body->span, no_stmts);
    }
    if (st->diags.len > base) return IR_ERRORS;
    *out = m;
    return IR_BUILT;
}
