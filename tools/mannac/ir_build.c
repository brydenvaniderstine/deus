#include "ir.h"
#include <assert.h>
#include <stdalign.h>

/* AST -> typed IR, the skeleton (typed-IR step 3): the parse-diagnostics gate,
 * the module, and functions whose signature and body are empty. Everything the
 * later steps add (docs/TYPED_IR.md section 7) is refused with IR_UNIMPLEMENTED
 * rather than half-built, so no golden can record a half-checked IR. */

static IrStatus unimplemented(IrState *st, const char *what) {
    st->unimplemented = what;
    return IR_UNIMPLEMENTED;
}

static IrStatus build_function(IrState *st, const Decl *d, IrModule *m) {
    assert(d->kind == DECL_FUNCTION);
    const NodeList *attrs = &d->as.function_decl.attributes;
    const Stmt *body = d->as.function_decl.body;
    assert(body && body->kind == STMT_BLOCK); /* a clean parse guarantees a block body */
    if (attrs->len > 0) return unimplemented(st, "attributes (step 5)");
    if (d->as.function_decl.params.len > 0) return unimplemented(st, "parameters (step 5)");
    if (d->as.function_decl.return_type) return unimplemented(st, "return types (step 5)");
    if (body->as.block.stmts.len > 0) return unimplemented(st, "statements (steps 7-13)");

    NodeList no_stmts, no_locals;
    node_list_init(&no_stmts);
    node_list_init(&no_locals);
    IrStmt *ir_body = ir_stmt_block(st, body->span, no_stmts);
    IrFunc *f = ir_func_new(st, d->span, d->as.function_decl.name, false, no_locals, 0, ir_type_unit(st), ir_body);
    node_list_push(st->arena, &m->funcs, f);
    return IR_BUILT;
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
    for (size_t i = 0; i < items->len; i++) {
        const Decl *d = items->items[i];
        st->progress = d->span;
        if (d->kind == DECL_STRUCT) return unimplemented(st, "struct layout (step 4b)");
        IrStatus s = build_function(st, d, m);
        if (s != IR_BUILT) return s;
    }
    if (st->diags.len > base) return IR_ERRORS;
    *out = m;
    return IR_BUILT;
}
