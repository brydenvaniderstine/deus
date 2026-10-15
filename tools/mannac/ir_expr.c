#include "ir.h"
#include <assert.h>
#include <stdint.h>

/* Expression checking (typed-IR step 6, docs/M2_PLAN.md "Types and literals"): the
 * bidirectional `check(expr, expected)` for the integer literal expressions step 6 owns.
 *
 * An UNTYPED LITERAL EXPRESSION is a number, unary `-` over one, or an arithmetic /
 * bitwise / shift operation whose operands are all untyped literal expressions
 * (parentheses add no node). It has no type of its own: the expected type gives it
 * one, and every literal in it must fit. Every other expression kind belongs to a
 * later step and stops the build with IR_UNIMPLEMENTED (ir_unimplemented), never a
 * half-built or half-checked IR.
 *
 * Because every supported node is a member of the group, no "is this subtree a group?"
 * predicate is needed. When identifiers and mixed operands arrive (steps 7-8) that
 * predicate MUST be computed bottom-up in one pass, not re-walked per node: re-walking
 * is O(nodes x height).
 *
 * Stack: check_group recurses once per tree level (tree height is bounded to 512 by the
 * parser, E0111); require_group is the same walk without building. The measured
 * per-level cost is in tools/measure_ir_build_stack.sh and ir_test.c. */

_Noreturn void ir_unimplemented(IrState *st, const char *what) {
    st->unimplemented = what;
    longjmp(st->fatal_target, 1);
}

/* Which later step owns an expression kind this step does not check. */
static void unimplemented_kind(IrState *st, const Expr *e) {
    switch (e->kind) {
        case EXPR_IDENT:        ir_unimplemented(st, "identifiers (step 7)");
        case EXPR_CALL:
        case EXPR_NAMED_ARG:    ir_unimplemented(st, "calls and struct construction (steps 10-11)");
        case EXPR_FIELD_ACCESS: ir_unimplemented(st, "field access (step 11)");
        case EXPR_INDEX:
        case EXPR_SLICE:
        case EXPR_ARRAY:
        case EXPR_ARRAY_REPEAT: ir_unimplemented(st, "arrays, indexing and slicing (step 12)");
        case EXPR_CONVERT:      ir_unimplemented(st, "conversions (step 13)");
        case EXPR_BOOL:         ir_unimplemented(st, "bool literals (step 6b)");
        case EXPR_NUMBER:
        case EXPR_UNARY:
        case EXPR_BINARY:
            break;
    }
    ir_unimplemented(st, "this expression (steps 7-13)");
}

static bool is_int(const IrType *t) {
    return t->kind == IR_T_INT;
}

/* Largest value of an integer type, as an unsigned magnitude. */
static uint64_t int_max(const IrType *t) {
    unsigned bits = t->as.integer.bits;
    if (t->as.integer.is_signed) return (UINT64_C(1) << (bits - 1)) - 1;
    return bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
}

/* An error placeholder: the module is never produced once an error is recorded, so the
 * node only carries the ERROR type that silences enclosing checks. */
static IrExpr *error_node(IrState *st, Span span) {
    return ir_expr_int(st, span, ir_type_error(st), 0);
}


/* Walks an untyped literal expression WITHOUT building anything, stopping the build
 * (IR_UNIMPLEMENTED) at the first node outside the group. Used before reporting an error
 * about a whole group, so we never blame a group that a later step would have typed
 * differently (`-x` in a u32 function is E0223 if x is an i32, not E0260). */
static void require_group(IrState *st, const Expr *e) {
    switch (e->kind) {
        case EXPR_NUMBER:
            return;
        case EXPR_UNARY:
            if (e->as.unary.op != UNOP_NEG) ir_unimplemented(st, "unary '!' and '~' (step 8)");
            require_group(st, e->as.unary.operand);
            return;
        default:
            unimplemented_kind(st, e);
            return;
    }
}

static IrExpr *check_literal(IrState *st, const Expr *e, const IrType *t) {
    uint64_t v = e->as.number.value;
    if (v > int_max(t)) {
        ir_error(st, diag_ir_literal_range(st->arena, e->span, v, false, ir_type_text(st, t)));
        /* No poison: the type is known, so enclosing checks go on and every bad literal
         * reports on its own. */
    }
    return ir_expr_int(st, e->span, t, v);
}

static IrExpr *check_group(IrState *st, const Expr *e, const IrType *t);

/* Unary `-` against an integer type. */
static IrExpr *check_neg(IrState *st, const Expr *e, const IrType *t) {
    const Expr *op = e->as.unary.operand;
    if (!t->as.integer.is_signed) {
        require_group(st, op);
        ir_error(st, diag_ir_operator_undefined(st->arena, e->span, "-", ir_type_text(st, t)));
        return error_node(st, e->span);
    }
    if (op->kind == EXPR_NUMBER && !op->parenthesized) {
        /* A literal DIRECTLY negated, no parentheses between: magnitude 2^(N-1) is iN::MIN
         * (the one folding: it is representation, 2^(N-1) alone does not fit iN); a larger
         * magnitude does not fit, reported with its sign over the whole `-N`. */
        uint64_t half = UINT64_C(1) << (t->as.integer.bits - 1);
        uint64_t v = op->as.number.value;
        if (v == half) return ir_expr_int(st, e->span, t, UINT64_C(0) - half); /* sign-extended -half */
        if (v > half) {
            ir_error(st, diag_ir_literal_range(st->arena, e->span, v, true, ir_type_text(st, t)));
            return ir_expr_int(st, e->span, t, 0);
        }
    }
    return ir_expr_unary(st, e->span, t, UNOP_NEG, check_group(st, op, t));
}

static IrExpr *check_group(IrState *st, const Expr *e, const IrType *t) {
    switch (e->kind) {
        case EXPR_NUMBER:
            return check_literal(st, e, t);
        case EXPR_UNARY:
            if (e->as.unary.op != UNOP_NEG) ir_unimplemented(st, "unary '!' and '~' (step 8)");
            return check_neg(st, e, t);
        default:
            unimplemented_kind(st, e);
            return NULL;
    }
}

IrExpr *ir_check_expr(IrState *st, const Expr *e, const IrType *expected) {
    assert(ir_ast_height_ok(e)); /* the parser's E0111 bound; this stage asserts it, enforces none */
    if (!is_int(expected)) {
        /* An integer literal expression where a non-integer type is expected: reported
         * as a type mismatch in step 6b. */
        require_group(st, e);
        ir_unimplemented(st, "integer literal where a non-integer type is expected (step 6b)");
    }
    return check_group(st, e, expected);
}
