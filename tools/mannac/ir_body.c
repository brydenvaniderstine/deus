#include "ir_internal.h"
#include <assert.h>

/* Function bodies (typed-IR step 7, docs/M2_PLAN.md "Names, scopes, items" and "Operators and
 * statements"): block scopes, `let`, assignment and its lvalue rule, expression statements,
 * `return` (its shape, E0276, and the missing-return analysis, E0277), `if`/`else`, and the loops:
 * `while`, `for`, `break`, `continue`. */

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
static IR_NOINLINE IrStmt *build_let(BodyCtx *bc, const Stmt *s) {
    IrState *st = bc->st;
    const char *name = s->as.let_stmt.name;
    Span name_span = s->as.let_stmt.name_span;
    bool dup = body_lookup(bc, name) != NULL;
    if (dup) ir_error(st, diag_ir_already_declared(st->arena, name_span, name));
    if (strcmp(name, "len") == 0) ir_error(st, diag_ir_len_declared(name_span)); /* the declaration still binds */

    Rename *rename = NULL;
    if (!dup && strcmp(name, "len") != 0) rename = ir_check_naming(st, bc->decls, REN_LOCAL, bc->fn_index, false, name, name_span);

    const IrType *annot = NULL;
    if (s->as.let_stmt.type) {
        annot = ir_resolve_type(bc->decls, s->as.let_stmt.type, CTX_PARAM);
        ir_check_type_size(bc->decls, s->as.let_stmt.type, annot);
    }
    IrExpr *init = annot ? ir_check_expr(bc, s->as.let_stmt.init, annot) : ir_infer_expr(bc, s->as.let_stmt.init);
    const IrType *t = annot ? annot : init->type;
    IrLocal *l = dup ? ir_local_new(st, UINT32_MAX, name, t, name_span, false) /* bound to nothing */
                     : body_declare(bc, name, t, name_span);
    if (rename) body_set_rename(bc, l->slot, rename);
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

static IR_NOINLINE IrStmt *build_assign(BodyCtx *bc, const Stmt *s) {
    IrState *st = bc->st;
    const Expr *target = s->as.assign_stmt.target;
    const Expr *value = s->as.assign_stmt.value;
    if (!lvalue_shape(target)) {
        ir_error(st, diag_ir_not_assignable(target->span));
        /* The value is still checked for its own mistakes, against no type. */
        ir_check_silent(bc, value);
        return ir_stmt_assign(st, s->span, ir_expr_int(st, target->span, ir_type_error(st), 0),
                              ir_expr_int(st, value->span, ir_type_error(st), 0));
    }
    /* The place: the local, or a chain of fields over it (an element, step 12, still stops the build in
     * the checker); on a mistake in it (E0230/E0231, E0242, E0251) the place is ERROR. */
    IrExpr *place = ir_infer_expr(bc, target);
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

/* `while cond { body }`: the condition is checked against bool; `break`/`continue` are legal in
 * the body (lexically: an `if` inside the body is still inside the loop). */
static IrStmt *build_while(BodyCtx *bc, const Stmt *s) {
    IrExpr *cond = ir_check_expr(bc, s->as.while_stmt.cond, ir_type_bool(bc->st));
    bc->loop_depth++;
    IrStmt *body = build_block(bc, s->as.while_stmt.body);
    bc->loop_depth--;
    return ir_stmt_while(bc->st, s->span, cond, body);
}

/* `for init; cond; step { body }` stays a `for` (so `continue` keeps meaning "run the step").
 * The loop opens its own scope: an `init` that is a `let` is visible in the condition, the step
 * and the body and gone after the loop, and is checked against every visible name like any
 * other `let` (E0213). Only the body counts as inside the loop for `break`/`continue`. */
static IrStmt *build_for(BodyCtx *bc, const Stmt *s) {
    body_push_scope(bc);
    IrStmt *init = build_stmt(bc, s->as.for_stmt.init);
    IrExpr *cond = ir_check_expr(bc, s->as.for_stmt.cond, ir_type_bool(bc->st));
    IrStmt *step = build_stmt(bc, s->as.for_stmt.step);
    bc->loop_depth++;
    IrStmt *body = build_block(bc, s->as.for_stmt.body);
    bc->loop_depth--;
    body_pop_scope(bc);
    return ir_stmt_for(bc->st, s->span, init, cond, step, body);
}

/* `return;` / `return v;` against the function. The shape must match (E0276): a value in a function
 * that returns one - `return;` is "missing" (reported at the statement) - and no value in a function that
 * returns nothing ("unexpected", reported at the value, which is still checked for its OWN mistakes
 * against no type). A value is checked against the return type like any other expectation. Any
 * `return`, mistaken or not, counts as returning for E0277: one mistake, one error. */
static IR_NOINLINE IrStmt *build_return(BodyCtx *bc, const Stmt *s) {
    IrState *st = bc->st;
    const IrFunc *f = bc->func;
    const Expr *v = s->as.return_stmt.value;
    bool unit = f->ret->kind == IR_T_UNIT;
    if (!v) {
        if (!unit) ir_error(st, diag_ir_missing_return_value(st->arena, s->span, f->name, ir_type_text(st, f->ret)));
        return ir_stmt_return(st, s->span, NULL);
    }
    if (unit) {
        ir_error(st, diag_ir_unexpected_return_value(st->arena, v->span, f->name));
        ir_check_silent(bc, v);
        return ir_stmt_return(st, s->span, ir_expr_int(st, v->span, ir_type_error(st), 0));
    }
    return ir_stmt_return(st, s->span, ir_check_expr(bc, v, f->ret));
}

/* `break` / `continue`: legal only inside a loop body (E0275); the statement is built either way. */
static IR_NOINLINE IrStmt *build_jump(BodyCtx *bc, const Stmt *s) {
    IrState *st = bc->st;
    bool is_break = s->kind == STMT_BREAK;
    if (bc->loop_depth == 0) ir_error(st, diag_ir_outside_loop(s->span, is_break ? "break" : "continue"));
    return is_break ? ir_stmt_break(st, s->span) : ir_stmt_continue(st, s->span);
}

static IR_NOINLINE IrStmt *build_expr_stmt(BodyCtx *bc, const Stmt *s) {
    return ir_stmt_expr(bc->st, s->span, ir_infer_discard(bc, s->as.expr_stmt.expr));
}

/* A thin dispatcher: it is live across every level of statement nesting (build_stmt -> build_if /
 * build_while / build_for -> build_block -> build_stmt), so each kind's work is in a function of its
 * own and this frame holds nothing. */
static IrStmt *build_stmt(BodyCtx *bc, const Stmt *s) {
    switch (s->kind) {
        case STMT_LET:      return build_let(bc, s);
        case STMT_ASSIGN:   return build_assign(bc, s);
        case STMT_EXPR:     return build_expr_stmt(bc, s);
        case STMT_IF:       return build_if(bc, s);
        case STMT_WHILE:    return build_while(bc, s);
        case STMT_FOR:      return build_for(bc, s);
        case STMT_BREAK:
        case STMT_CONTINUE: return build_jump(bc, s);
        case STMT_RETURN:   return build_return(bc, s);
        case STMT_BLOCK:    break;
    }
    /* The parser never builds this: a STMT_BLOCK is made only as a body by parse_block (parser.c:1755, 1765), and
     * `{` at the start of a statement is E0105 (a bare block is not v0 syntax). With asserts off (NDEBUG) the
     * fallback keeps the build defined: it stops with IR_UNIMPLEMENTED instead of running off the end. */
    assert(!"a bare block statement");
    ir_unimplemented(bc->st, "a bare block statement");
}

/* ---- missing return (E0277) ---- */

/* Does control certainly reach a `return` (so it cannot fall off the end)? Structural, per Q5
 * (docs/M2_PLAN.md "missing return"): a block does if ANY of its statements does (statements after
 * a `return` are unreachable, which is allowed, so `return 1; let x = 2;` returns); a `return` does;
 * an `if` does only with an `else` and when the then-block and the else both do (an else-if chain
 * is an else that is itself an `if`). LOOPS NEVER COUNT - not even `while true { return 1; }` or a
 * `for` whose body returns - and `break`/`continue` do not return. No constant folding: `if true
 * { return 1; }` without an else does not return. Runs on the built IR; the else-if chain is a
 * loop, the only recursion is through nested blocks (bounded by the parser's nesting depth). */
static bool block_returns(const IrStmt *b);

static bool if_returns(const IrStmt *s) {
    for (;;) {
        if (!block_returns(s->as.if_stmt.then_branch)) return false;
        const IrStmt *e = s->as.if_stmt.else_branch;
        if (!e) return false;
        if (e->kind == IR_S_IF) { s = e; continue; }
        return block_returns(e);
    }
}

static bool block_returns(const IrStmt *b) {
    for (size_t i = 0; i < b->as.block.stmts.len; i++) {
        const IrStmt *s = b->as.block.stmts.items[i];
        if (s->kind == IR_S_RETURN || (s->kind == IR_S_IF && if_returns(s))) return true;
    }
    return false;
}

/* E0277 is reported at the body's closing `}`: that is where control runs off the end, it is
 * always on a line of its own end of the function (the name is far above in a long body), and the
 * message names the function. One per function, after the body's own errors. */
IrStmt *ir_build_body(IrState *st, Ctx *decls, IrFunc *f, size_t fn_index, const Stmt *body) {
    BodyCtx bc;
    body_init(&bc, st, decls, f);
    bc.fn_index = fn_index;
    if (decls->param_renames && decls->param_renames[fn_index]) {
        for (size_t i = 0; i < f->n_params; i++) {
            if (decls->param_renames[fn_index][i]) body_set_rename(&bc, i, decls->param_renames[fn_index][i]);
        }
    }
    IrStmt *blk = build_block(&bc, body);
    if (f->ret->kind != IR_T_UNIT && !block_returns(blk)) {
        Span end = body->span;
        Span brace = {end.end_line, end.end_col - 1, end.end_line, end.end_col};
        ir_error(st, diag_ir_may_end_without_return(st->arena, brace, f->name));
    }
    return blk;
}
