#include "ir_internal.h"
#include <assert.h>

/* Function bodies (typed-IR step 7, docs/M2_PLAN.md "Names, scopes, items" and "Operators and
 * statements"): block scopes, `let`, assignment and its lvalue rule, expression statements,
 * `return expr;` and - pulled forward from step 8, because a bare `{ }` is not a statement and
 * `if` is the only way to open an inner block to test scoping with - `if`/`else`. Loops,
 * `break` and `continue` stop the build as IR_UNIMPLEMENTED. */

/* ---- visible names ---- */

static void tab_grow(BodyCtx *bc) {
    size_t cap = bc->cap ? bc->cap * 2 : 16;
    VisEntry *tab = arena_alloc(bc->st->arena, cap * sizeof(VisEntry), alignof(VisEntry));
    memset(tab, 0, cap * sizeof(VisEntry));
    for (size_t i = 0; i < bc->cap; i++) {
        if (!bc->tab[i].name) continue;
        size_t j = (size_t)(name_hash(bc->tab[i].name) & (cap - 1));
        while (tab[j].name) j = (j + 1) & (cap - 1);
        tab[j] = bc->tab[i];
    }
    bc->tab = tab; /* installed only after every allocation succeeded */
    bc->cap = cap;
}

static VisEntry *tab_find(const BodyCtx *bc, const char *name) {
    if (bc->cap == 0) return NULL;
    size_t mask = bc->cap - 1;
    for (size_t i = (size_t)(name_hash(name) & mask);; i = (i + 1) & mask) {
        if (!bc->tab[i].name) return NULL;
        if (strcmp(bc->tab[i].name, name) == 0) return &bc->tab[i];
    }
}

/* Registers `local` as the visible binding of its name (reviving a dead entry). */
static void tab_set(BodyCtx *bc, IrLocal *local) {
    VisEntry *e = tab_find(bc, local->name);
    if (e) {
        e->local = local;
        e->alive = true;
        return;
    }
    if ((bc->count + 1) * 2 > bc->cap) tab_grow(bc);
    size_t mask = bc->cap - 1;
    size_t i = (size_t)(name_hash(local->name) & mask);
    while (bc->tab[i].name) i = (i + 1) & mask;
    bc->tab[i].name = local->name;
    bc->tab[i].local = local;
    bc->tab[i].alive = true;
    bc->count++;
}

void body_init(BodyCtx *bc, IrState *st, Ctx *decls, IrFunc *func) {
    memset(bc, 0, sizeof *bc);
    bc->st = st;
    bc->decls = decls;
    bc->func = func;
    for (size_t i = 0; i < func->n_params; i++) tab_set(bc, func->locals.items[i]);
}

void body_push_scope(BodyCtx *bc) {
    if (bc->depth == bc->scope_cap) {
        size_t cap = bc->scope_cap ? bc->scope_cap * 2 : 8;
        size_t *s = arena_alloc(bc->st->arena, cap * sizeof(size_t), alignof(size_t));
        if (bc->depth) memcpy(s, bc->scope_start, bc->depth * sizeof(size_t));
        bc->scope_start = s;
        bc->scope_cap = cap;
    }
    bc->scope_start[bc->depth++] = bc->func->locals.len;
}

void body_pop_scope(BodyCtx *bc) {
    assert(bc->depth > 0);
    size_t start = bc->scope_start[--bc->depth];
    for (size_t i = start; i < bc->func->locals.len; i++) {
        const IrLocal *l = bc->func->locals.items[i];
        tab_find(bc, l->name)->alive = false;
    }
}

IrLocal *body_lookup(const BodyCtx *bc, const char *name) {
    const VisEntry *e = tab_find(bc, name);
    return e && e->alive ? e->local : NULL;
}

IrLocal *body_declare(BodyCtx *bc, const char *name, const IrType *type, Span span) {
    IrLocal *l = ir_local_new(bc->st, (uint32_t)bc->func->locals.len, name, type, span, false);
    node_list_push(bc->st->arena, &bc->func->locals, l);
    tab_set(bc, l);
    return l;
}

/* ---- statements ---- */

static IrStmt *build_stmt(BodyCtx *bc, const Stmt *s);

static IrStmt *build_block(BodyCtx *bc, const Stmt *block) {
    NodeList stmts;
    node_list_init(&stmts);
    body_push_scope(bc);
    for (size_t i = 0; i < block->as.block.stmts.len; i++) {
        node_list_push(bc->st->arena, &stmts, build_stmt(bc, block->as.block.stmts.items[i]));
    }
    body_pop_scope(bc);
    return ir_stmt_block(bc->st, block->span, stmts);
}

/* `let name [: T] = init;`. In source order: the name is checked against the visible ones
 * (E0213 - a redeclaration anywhere in the function, parameters and enclosing blocks
 * included), then the annotation, then the initializer; the binding becomes visible only
 * AFTER its own statement, so `let x: u32 = x;` does not see the new x. A let whose annotation
 * or initializer failed still declares its name, typed ERROR, so later uses stay silent. A
 * redeclared name binds nothing: the first binding stays. */
static IrStmt *build_let(BodyCtx *bc, const Stmt *s) {
    IrState *st = bc->st;
    const char *name = s->as.let_stmt.name;
    Span name_span = s->as.let_stmt.name_span;
    bool dup = body_lookup(bc, name) != NULL;
    if (dup) ir_error(st, diag_ir_already_declared(st->arena, name_span, name));

    const IrType *annot = NULL;
    if (s->as.let_stmt.type) {
        annot = ir_resolve_type(bc->decls, s->as.let_stmt.type, CTX_PARAM);
        ir_check_type_size(bc->decls, s->as.let_stmt.type, annot);
    }
    IrExpr *init = annot ? ir_check_expr(bc, s->as.let_stmt.init, annot) : ir_infer_expr(bc, s->as.let_stmt.init);
    const IrType *t = annot ? annot : init->type;
    IrLocal *l = dup ? ir_local_new(st, UINT32_MAX, name, t, name_span, false) /* bound to nothing */
                     : body_declare(bc, name, t, name_span);
    return ir_stmt_let(st, s->span, l, init);
}

/* The lvalue rule (docs/M2_PLAN.md "Operators and statements"): a plain name, or a field or
 * element of an lvalue. Checked on the SHAPE alone, before anything is typed, so a bad target
 * is one E0270 and none of its parts is blamed for anything else. A parenthesized target is
 * not an lvalue (the productions have no parentheses). Iterative: the chain can be 512 long. */
static bool lvalue_shape(const Expr *e) {
    for (;;) {
        if (e->parenthesized) return false;
        if (e->kind == EXPR_FIELD_ACCESS) e = e->as.field_access.base;
        else if (e->kind == EXPR_INDEX) e = e->as.index.base;
        else return e->kind == EXPR_IDENT;
    }
}

static IrStmt *build_assign(BodyCtx *bc, const Stmt *s) {
    IrState *st = bc->st;
    const Expr *target = s->as.assign_stmt.target;
    const Expr *value = s->as.assign_stmt.value;
    if (!lvalue_shape(target)) {
        ir_error(st, diag_ir_not_assignable(target->span));
        /* The value is still checked for its own mistakes, against no type. */
        IrExpr *v = ir_check_expr(bc, value, ir_type_error(st));
        return ir_stmt_assign(st, s->span, ir_expr_int(st, target->span, ir_type_error(st), 0), v);
    }
    if (target->kind != EXPR_IDENT) ir_unimplemented(st, "assignment to a field or an element (steps 11-12)");
    IrExpr *place = ir_infer_expr(bc, target); /* the local, or E0230/E0231 and ERROR */
    IrExpr *v = ir_check_expr(bc, value, place->type);
    return ir_stmt_assign(st, s->span, place, v);
}

static IrStmt *build_if(BodyCtx *bc, const Stmt *s) {
    IrExpr *cond = ir_check_expr(bc, s->as.if_stmt.cond, ir_type_bool(bc->st));
    IrStmt *then_b = build_block(bc, s->as.if_stmt.then_branch);
    const Stmt *e = s->as.if_stmt.else_branch;
    IrStmt *else_b = NULL;
    if (e) else_b = e->kind == STMT_IF ? build_if(bc, e) : build_block(bc, e);
    return ir_stmt_if(bc->st, s->span, cond, then_b, else_b);
}

static IrStmt *build_stmt(BodyCtx *bc, const Stmt *s) {
    IrState *st = bc->st;
    switch (s->kind) {
        case STMT_LET:
            return build_let(bc, s);
        case STMT_ASSIGN:
            return build_assign(bc, s);
        case STMT_EXPR:
            return ir_stmt_expr(st, s->span, ir_infer_expr(bc, s->as.expr_stmt.expr));
        case STMT_IF:
            return build_if(bc, s);
        case STMT_RETURN: {
            if (!s->as.return_stmt.value) ir_unimplemented(st, "bare return (step 9)");
            if (bc->func->ret->kind == IR_T_UNIT) ir_unimplemented(st, "return in a unit function (step 9)");
            return ir_stmt_return(st, s->span, ir_check_expr(bc, s->as.return_stmt.value, bc->func->ret));
        }
        case STMT_WHILE:
        case STMT_FOR:
        case STMT_BREAK:
        case STMT_CONTINUE:
        case STMT_BLOCK:
            break;
    }
    ir_unimplemented(st, "loops, break and continue (step 8)");
}

IrStmt *ir_build_body(IrState *st, Ctx *decls, IrFunc *f, const Stmt *body) {
    BodyCtx bc;
    body_init(&bc, st, decls, f);
    return build_block(&bc, body);
}
