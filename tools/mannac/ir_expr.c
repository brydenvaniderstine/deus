#include "ir_internal.h"
#include <assert.h>
#include <stdint.h>

/* Expression checking (typed-IR steps 6-8, docs/M2_PLAN.md "Types and literals" and
 * "Operators and statements").
 *
 * An UNTYPED LITERAL EXPRESSION (a "group") is a number, unary `-` or `~` over one, or an
 * arithmetic / bitwise / shift operation whose operands are all groups (parentheses add no
 * node). It has no type of its own: a type is given to it, and every literal in it must fit.
 * The type comes from the expected type, or from the TYPED SIBLING when it is one operand of
 * a binary operator (`0 == x`, `1 + x`, `10 < n`, either side), and it flows into the WHOLE
 * group. With no type from anywhere it is E0225.
 *
 * ONE BOTTOM-UP PASS. synth(e) classifies and types in a single visit of each node and
 * returns either "group" (nothing is built yet) or a typed IrExpr. A group is later resolved
 * once its type is known, by resolve_group: a second visit of the group's nodes only. So every
 * node is visited at most twice and the checker is O(nodes); a predicate that re-walked a
 * subtree at every node ("is this a group?") would be O(nodes x height) - on `x + 1 + 2 + ...`
 * that is quadratic. ir_test.c counts the visits (IrState.expr_visits) on 512-high trees.
 *
 * Operators (Q6 i-vi): arithmetic, bitwise and shift need integers, both operands the SAME type
 * (a shift's count too); `==` `!=` also take bool; `< > <= >=` integers; `&&` `||` `!` bool; unary
 * `-` signed, `~` any integer. An operand type the operator is not defined for is E0260; two
 * integer types that differ are E0261 (with a conversion fix); a bool and an integer, E0261
 * without. A failed operand makes the expression ERROR and nothing built on it reports again.
 *
 * Stack: synth and resolve_group recurse once per tree level (the parser bounds the height to
 * 512, E0111); the measured per-level cost is in tools/measure_ir_build_stack.sh. */

_Noreturn void ir_unimplemented(IrState *st, const char *what) {
    st->unimplemented = what;
    longjmp(st->fatal_target, 1);
}

/* Which later step owns an expression kind this step does not check. */
static _Noreturn void unimplemented_kind(IrState *st, const Expr *e) {
    switch (e->kind) {
        case EXPR_CALL:
        case EXPR_NAMED_ARG:    ir_unimplemented(st, "calls and struct construction (steps 10-11)");
        case EXPR_FIELD_ACCESS: ir_unimplemented(st, "field access (step 11)");
        case EXPR_INDEX:
        case EXPR_SLICE:
        case EXPR_ARRAY:
        case EXPR_ARRAY_REPEAT: ir_unimplemented(st, "arrays, indexing and slicing (step 12)");
        case EXPR_CONVERT:      ir_unimplemented(st, "conversions (step 13)");
        case EXPR_NUMBER:
        case EXPR_BOOL:
        case EXPR_IDENT:
        case EXPR_UNARY:
        case EXPR_BINARY:
            break;
    }
    ir_unimplemented(st, "this expression (steps 9-13)");
}

static bool is_int(const IrType *t) {
    return t->kind == IR_T_INT;
}

_Static_assert(BINOP_SHR < BINOP_LT && BINOP_NE < BINOP_AND, "the operator classes rely on ast.h's BinOp order");
static bool is_arith_class(BinOp op) { return op <= BINOP_SHR; }                  /* + - * / % & | ^ << >> */
static bool is_shift(BinOp op)       { return op == BINOP_SHL || op == BINOP_SHR; }
static bool is_equality(BinOp op)    { return op == BINOP_EQ || op == BINOP_NE; }
static bool is_logical(BinOp op)     { return op == BINOP_AND || op == BINOP_OR; }

/* Largest value of an integer type, as an unsigned magnitude. */
static uint64_t int_max(const IrType *t) {
    unsigned bits = t->as.integer.bits;
    if (t->as.integer.is_signed) return (UINT64_C(1) << (bits - 1)) - 1;
    return bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
}

/* An error placeholder: the module is never produced once an error is recorded, so the node
 * only carries the ERROR type that silences enclosing checks. */
static IrExpr *error_node(IrState *st, Span span) {
    return ir_expr_int(st, span, ir_type_error(st), 0);
}

/* ---- diagnostics: built and reported in cold, non-inlined helpers ----
 * A Diagnostic is 64 bytes, and every `ir_error(st, diag_x(...))` written inline puts a 64-byte
 * temporary (plus its ASan redzone) into the frame of the function containing it; the functions
 * below are live once per tree level, so those temporaries were multiplied by the height
 * (measured: docs/TYPED_IR.md step 9). Here they live only while an error is being reported. */

static IR_COLD void report_range(IrState *st, Span at, uint64_t v, bool negated, const IrType *t) {
    ir_error(st, diag_ir_literal_range(st->arena, at, v, negated, ir_type_text(st, t)));
}

static IR_COLD void report_operator(IrState *st, Span at, const char *op, const IrType *t) {
    ir_error(st, diag_ir_operator_undefined(st->arena, at, op, ir_type_text(st, t)));
}

static IR_COLD void report_mismatch(IrState *st, Span at, const IrType *expected, const char *found) {
    ir_error(st, diag_ir_type_mismatch(st->arena, at, ir_type_text(st, expected), found));
}

static IR_COLD void report_mismatch_types(IrState *st, Span at, const IrType *expected, const IrType *found) {
    ir_error(st, diag_ir_type_mismatch(st->arena, at, ir_type_text(st, expected), ir_type_text(st, found)));
}

static IR_COLD void report_bool_expected(IrState *st, Span at) { /* a literal where a bool is required */
    ir_error(st, diag_ir_type_mismatch(st->arena, at, "bool", "integer literal"));
}

static IR_COLD void report_cannot_infer(IrState *st, Span at) {
    ir_error(st, diag_ir_cannot_infer(st->arena, at));
}

static IR_COLD void report_name(IrState *st, const Expr *e, const SymEntry *top) {
    const char *name = e->as.ident.name;
    ir_error(st, top ? diag_ir_not_a_value(st->arena, e->span, name, top->is_struct)
                     : diag_ir_unknown_name(st->arena, e->span, name));
}

/* ---- groups: resolved top-down once the type is known ----
 * resolve_group is a thin dispatcher; the work of each node kind is in a function of its own, so
 * the frame that stays live across the recursion holds only the two operand results. */

static IrExpr *resolve_group(IrState *st, const Expr *e, const IrType *t);

static IR_NOINLINE IrExpr *group_literal(IrState *st, const Expr *e, const IrType *t) {
    uint64_t v = e->as.number.value;
    if (v > int_max(t)) {
        report_range(st, e->span, v, false, t);
        /* No poison: the type is known, so enclosing checks go on and every bad literal
         * reports on its own. */
    }
    return ir_expr_int(st, e->span, t, v);
}

/* A literal DIRECTLY negated, no parentheses between, against a signed type: magnitude 2^(N-1)
 * is iN::MIN (the one folding: it is representation, 2^(N-1) alone does not fit iN); a larger
 * magnitude does not fit, reported with its sign over the whole `-N`; anything else is NULL (the
 * ordinary unary node). Not on the recursion path, so its locals stay out of group_neg's frame. */
static IR_NOINLINE IrExpr *neg_of_literal(IrState *st, const Expr *e, const Expr *op, const IrType *t) {
    uint64_t half = UINT64_C(1) << (t->as.integer.bits - 1);
    uint64_t v = op->as.number.value;
    st->expr_visits++;
    if (v == half) return ir_expr_int(st, e->span, t, UINT64_C(0) - half); /* sign-extended -half */
    if (v > half) {
        report_range(st, e->span, v, true, t);
        return ir_expr_int(st, e->span, t, 0);
    }
    return NULL;
}

/* Unary `-` over a group, against an integer type. */
static IR_NOINLINE IrExpr *group_neg(IrState *st, const Expr *e, const IrType *t) {
    const Expr *op = e->as.unary.operand;
    if (!t->as.integer.is_signed) {
        report_operator(st, e->span, "-", t);
        return error_node(st, e->span);
    }
    if (op->kind == EXPR_NUMBER && !op->parenthesized) {
        IrExpr *folded = neg_of_literal(st, e, op, t);
        if (folded) return folded;
    }
    return ir_expr_unary(st, e->span, t, UNOP_NEG, resolve_group(st, op, t));
}

static IR_NOINLINE IrExpr *resolve_unary(IrState *st, const Expr *e, const IrType *t) {
    if (e->as.unary.op == UNOP_NEG) return group_neg(st, e, t);
    assert(e->as.unary.op == UNOP_BITNOT); /* `!` is never part of a group */
    return ir_expr_unary(st, e->span, t, UNOP_BITNOT, resolve_group(st, e->as.unary.operand, t));
}

static IR_NOINLINE IrExpr *resolve_binary(IrState *st, const Expr *e, const IrType *t) {
    assert(!is_equality(e->as.binary.op)); /* a group's only binaries are arithmetic, bitwise and shift */
    /* Both operands get the group's type, left to right, so diagnostics come out in source order. */
    IrExpr *l = resolve_group(st, e->as.binary.lhs, t);
    IrExpr *r = resolve_group(st, e->as.binary.rhs, t);
    return ir_expr_binary(st, e->span, t, e->as.binary.op, l, r);
}

/* `e` is known to be a group (synth said so) and `t` an integer type. */
static IrExpr *resolve_group(IrState *st, const Expr *e, const IrType *t) {
    st->expr_visits++;
    if (e->kind == EXPR_BINARY) return resolve_binary(st, e, t);
    if (e->kind == EXPR_UNARY) return resolve_unary(st, e, t);
    return group_literal(st, e, t);
}

/* A group against the type it is expected to take: ERROR = already failed (say nothing), a
 * non-integer type is one mismatch for the whole group (`expected bool, found integer
 * literal`), an integer type resolves it. */
static IR_NOINLINE IrExpr *group_against(IrState *st, const Expr *e, const IrType *expected) {
    if (expected->kind == IR_T_ERROR) return error_node(st, e->span);
    if (!is_int(expected)) {
        report_mismatch(st, e->span, expected, "integer literal");
        return error_node(st, e->span);
    }
    return resolve_group(st, e, expected);
}

/* ---- names ---- */

/* A name used as a value: a visible local (parameters included), else - because the top-level
 * namespace is separate and inner - a function or struct name, which is not a value (E0231),
 * else unknown (E0230). A local whose own type is ERROR is silent: its mistake was reported
 * where it was declared. */
static IR_NOINLINE IrExpr *check_ident(BodyCtx *bc, const Expr *e) {
    IrState *st = bc->st;
    IrLocal *l = body_lookup(bc, e->as.ident.name);
    if (!l) {
        report_name(st, e, symtab_lookup(&bc->decls->top, e->as.ident.name));
        return error_node(st, e->span);
    }
    return ir_expr_local(st, e->span, l);
}

/* ---- synth: the single bottom-up pass ----
 * synth, synth_unary and synth_binary are the only functions live across the recursion; each
 * holds at most the two operand results. Everything that happens AFTER the operands are typed is
 * in a finish_* function (non-inlined), so its locals exist only once the recursion returned. */

typedef struct {
    bool     group; /* an untyped literal expression: nothing built yet */
    IrExpr  *node;  /* otherwise the typed (or ERROR) expression */
} Synth;

static Synth synth(BodyCtx *bc, const Expr *e);

static Synth is_group(void)        { Synth s = {true, NULL}; return s; }
static Synth is_node(IrExpr *n)    { Synth s = {false, n}; return s; }
static Synth is_error(IrState *st, Span at) { return is_node(error_node(st, at)); }

static bool has_error(Synth s) { return !s.group && s.node->type->kind == IR_T_ERROR; }

static IR_NOINLINE Synth finish_unary(BodyCtx *bc, const Expr *e, Synth o) {
    IrState *st = bc->st;
    UnOp op = e->as.unary.op;
    if (op != UNOP_NOT && o.group) return o;                  /* `-` and `~` of a group are groups */
    if (op == UNOP_NOT && o.group) {                          /* `!` needs a bool; a literal has no type to name */
        report_bool_expected(st, e->as.unary.operand->span);
        return is_error(st, e->span);
    }
    if (has_error(o)) return is_error(st, e->span);
    const IrType *t = o.node->type;
    bool ok = op == UNOP_NOT ? t->kind == IR_T_BOOL
            : op == UNOP_NEG ? is_int(t) && t->as.integer.is_signed
                             : is_int(t);
    if (!ok) {
        report_operator(st, e->span, ir_unop_text(op), t);
        return is_error(st, e->span);
    }
    return is_node(ir_expr_unary(st, e->span, t, op, o.node));
}

static Synth synth_unary(BodyCtx *bc, const Expr *e) {
    Synth o = synth(bc, e->as.unary.operand);
    return finish_unary(bc, e, o);
}

/* May `t` be an operand of `op`? Arithmetic, bitwise, shift and ordering: integers. Equality:
 * integers and bool. (&& and || are handled by finish_logical.) */
static bool operand_ok(BinOp op, const IrType *t) {
    return is_int(t) || (is_equality(op) && t->kind == IR_T_BOOL);
}

/* Can a value of integer type `from` be converted to `to` without losing any? */
static bool lossless(const IrType *from, const IrType *to) {
    unsigned fb = from->as.integer.bits, tb = to->as.integer.bits;
    bool fs = from->as.integer.is_signed, ts = to->as.integer.is_signed;
    if (fs == ts) return tb > fb;
    return !fs && ts && tb > fb; /* unsigned -> a wider signed type */
}

/* E0261 for two typed operands of different types. The fix (both integer types only) wraps ONE
 * operand in the conversion to the other's type: the one that converts without loss if exactly
 * one does (u8 + u32 -> `u32(a) + b`), else the RIGHT operand into the left's type; a shift's
 * count is always the one wrapped (its left operand decides the result type). */
static IR_COLD void report_types_differ(BodyCtx *bc, const Expr *e, const IrType *lt, const IrType *rt) {
    IrState *st = bc->st;
    const Expr *le = e->as.binary.lhs, *re = e->as.binary.rhs;
    bool wrap_left = false;
    const char *to = NULL;
    if (is_int(lt) && is_int(rt)) {
        bool l2r = lossless(lt, rt), r2l = lossless(rt, lt);
        wrap_left = !is_shift(e->as.binary.op) && l2r && !r2l;
        to = ir_type_text(st, wrap_left ? rt : lt);
    }
    ir_error(st, diag_ir_operand_types_differ(st->arena, e->span, ir_type_text(st, lt), ir_type_text(st, rt),
                                              le->span, re->span, wrap_left, to));
}

/* A binary operator with at least one TYPED operand (the other may be a group). In order: an
 * ERROR operand silences everything; two groups under a comparison have no type (E0225, once,
 * at the left); an operand type the operator is not defined for is E0260, once, naming the
 * left one first; then a group takes its typed sibling's type; then the types must agree. */
static IR_NOINLINE Synth typed_binary(BodyCtx *bc, const Expr *e, Synth l, Synth r) {
    IrState *st = bc->st;
    BinOp op = e->as.binary.op;
    const Expr *le = e->as.binary.lhs, *re = e->as.binary.rhs;
    if (has_error(l) || has_error(r)) return is_error(st, e->span);
    if (l.group && r.group) { /* only a comparison reaches here with two groups */
        report_cannot_infer(st, le->span);
        return is_error(st, e->span);
    }
    const IrType *lt = l.group ? NULL : l.node->type;
    const IrType *rt = r.group ? NULL : r.node->type;
    const IrType *bad = (lt && !operand_ok(op, lt)) ? lt : (rt && !operand_ok(op, rt)) ? rt : NULL;
    if (bad) {
        report_operator(st, e->span, ir_binop_text(op), bad);
        return is_error(st, e->span);
    }
    IrExpr *ln = l.group ? group_against(st, le, rt) : l.node;
    IrExpr *rn = r.group ? group_against(st, re, lt) : r.node;
    if (ln->type->kind == IR_T_ERROR || rn->type->kind == IR_T_ERROR) return is_error(st, e->span); /* already reported */
    if (ln->type != rn->type) {
        report_types_differ(bc, e, ln->type, rn->type);
        return is_error(st, e->span);
    }
    const IrType *result = is_arith_class(op) ? ln->type : ir_type_bool(st);
    return is_node(ir_expr_binary(st, e->span, result, op, ln, rn));
}

/* `&&` and `||`: both operands bool. A typed non-bool operand is E0260 (once, the left first); a
 * literal has no type to name, so it is a mismatch against bool. */
static IR_NOINLINE Synth finish_logical(BodyCtx *bc, const Expr *e, Synth l, Synth r) {
    IrState *st = bc->st;
    BinOp op = e->as.binary.op;
    if (has_error(l) || has_error(r)) return is_error(st, e->span);
    const Expr *ops[2] = {e->as.binary.lhs, e->as.binary.rhs};
    Synth sy[2] = {l, r};
    for (int i = 0; i < 2; i++) {
        if (sy[i].group) {
            report_bool_expected(st, ops[i]->span);
            return is_error(st, e->span);
        }
        if (sy[i].node->type->kind != IR_T_BOOL) {
            report_operator(st, e->span, ir_binop_text(op), sy[i].node->type);
            return is_error(st, e->span);
        }
    }
    return is_node(ir_expr_binary(st, e->span, ir_type_bool(st), op, l.node, r.node));
}

static IR_NOINLINE Synth finish_binary(BodyCtx *bc, const Expr *e, Synth l, Synth r) {
    BinOp op = e->as.binary.op;
    if (is_logical(op)) return finish_logical(bc, e, l, r);
    if (is_arith_class(op) && l.group && r.group) return is_group();
    return typed_binary(bc, e, l, r);
}

static Synth synth_binary(BodyCtx *bc, const Expr *e) {
    Synth l = synth(bc, e->as.binary.lhs);
    Synth r = synth(bc, e->as.binary.rhs);
    return finish_binary(bc, e, l, r);
}

static IR_NOINLINE Synth synth_leaf(BodyCtx *bc, const Expr *e) {
    IrState *st = bc->st;
    switch (e->kind) {
        case EXPR_NUMBER:
            return is_group();
        case EXPR_BOOL:
            return is_node(ir_expr_bool(st, e->span, e->as.boolean.value));
        case EXPR_IDENT:
            return is_node(check_ident(bc, e));
        default:
            unimplemented_kind(st, e);
    }
}

static Synth synth(BodyCtx *bc, const Expr *e) {
    bc->st->expr_visits++;
    assert(ir_ast_height_ok(e)); /* the parser's E0111 bound; this stage asserts it, enforces none */
    if (e->kind == EXPR_BINARY) return synth_binary(bc, e);
    if (e->kind == EXPR_UNARY) return synth_unary(bc, e);
    return synth_leaf(bc, e);
}

/* ---- the two entry points ---- */

IrExpr *ir_check_expr(BodyCtx *bc, const Expr *e, const IrType *expected) {
    IrState *st = bc->st;
    Synth s = synth(bc, e);
    if (s.group) return group_against(st, e, expected);
    const IrType *t = s.node->type;
    if (t->kind == IR_T_ERROR || expected->kind == IR_T_ERROR || t == expected) return s.node;
    report_mismatch_types(st, e->span, expected, t);
    return error_node(st, e->span);
}

IrExpr *ir_infer_expr(BodyCtx *bc, const Expr *e) {
    IrState *st = bc->st;
    Synth s = synth(bc, e);
    if (!s.group) return s.node;
    /* A literal expression has no type of its own: it takes the one expected of it, and nothing
     * expects one here. */
    report_cannot_infer(st, e->span);
    return error_node(st, e->span);
}
