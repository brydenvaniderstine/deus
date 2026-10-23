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
        case EXPR_NAMED_ARG:
            /* The parser never builds this: it makes a named argument only as an element of a call's arguments
             * (parser.c:632), and the call and construction checks consume those themselves. */
            assert(!"a named argument outside a call's arguments");
            break;
        case EXPR_NUMBER:
        case EXPR_BOOL:
        case EXPR_IDENT:
        case EXPR_UNARY:
        case EXPR_BINARY:
        case EXPR_CALL:
        case EXPR_FIELD_ACCESS:
        case EXPR_CONVERT:
        case EXPR_INDEX:
        case EXPR_SLICE:
        case EXPR_ARRAY:
        case EXPR_ARRAY_REPEAT:
            break;
    }
    ir_unimplemented(st, "this expression"); /* every kind has a case above; kept so a new ExprKind stops the build instead of falling through */
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
 * (measured: docs/history/TYPED_IR.md step 9). Here they live only while an error is being reported. */

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

static IR_COLD void report_unit(IrState *st, Span at) { /* a call that returns nothing, used as a value */
    ir_error(st, diag_ir_unit_value(at));
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
        const SymEntry *top = symtab_lookup(&bc->decls->top, e->as.ident.name);
        if (strcmp(e->as.ident.name, "len") == 0) { /* the builtin: E0256 only if no declaration named len exists (else E0257 said it) */
            /* A struct, function or parameter named len is E0257, which stops the build in the declaration
             * pass before any body is read; only a local or loop variable can be named len here, and that
             * was found above. So no top-level declaration named len exists in a body. */
            assert(!top);
            ir_error(st, diag_ir_len_not_called(e->span));
            return error_node(st, e->span);
        }
        /* A function or struct used as a value is still a resolved use of that name: a rename must reach it. */
        Rename *tr = NULL;
        if (top && !top->is_struct && bc->decls->fn_renames) tr = bc->decls->fn_renames[top->index];
        if (top && top->is_struct && bc->decls->struct_renames) tr = bc->decls->struct_renames[top->index];
        ir_note_use(st, tr, e->as.ident.name_span);
        ir_note_free_name(bc, e->as.ident.name, tr); /* ... and no rename may make a local capture it (an unknown name too) */
        if (!top) ir_note_unresolved(st, bc->decls, NS_TOP, 0, e->as.ident.name);
        report_name(st, e, top);
        return error_node(st, e->span);
    }
    ir_note_use(st, body_rename(bc, l->slot), e->as.ident.name_span);
    return ir_expr_local(st, e->span, l);
}

/* ---- synth: the single bottom-up pass ----
 * synth, synth_unary and synth_binary are the only functions live across the recursion; each
 * holds at most the two operand results. Everything that happens AFTER the operands are typed is
 * in a finish_* function (non-inlined), so its locals exist only once the recursion returned. */

/* A fix that waits (step 19). A fix-it for E0261 changes the type of the expression it sits in, so it is only right if everything that
 * reads that expression accepts the new type. `pending` is 1 + the index of the one E0261 diagnostic whose fix still depends on what
 * reads the expression, and `ptype` the type the expression WOULD have after the fix. Every parent either checks that it accepts
 * `ptype` and passes the pending fix on (an operator whose other operand agrees, unary `-` of a signed type...), or ends it: a consumer
 * that accepts `ptype` lets it stand, any other withdraws the fix (its diagnostic loses it). Two sources: an E0261 inside the
 * expression (`a + b` in a place that expects what the wrap makes it), and a use of a local whose own initializer has such a fix. */
typedef struct {
    bool     group; /* an untyped literal expression: nothing built yet */
    uint32_t pending; /* an ERROR node only: see above (0: none) */
    const IrType *ptype; /* ... the type it would have (NULL when pending is 0) */
    IrExpr  *node;  /* otherwise the typed (or ERROR) expression */
} Synth;

static Synth synth(BodyCtx *bc, const Expr *e);

static Synth is_group(void)        { Synth s = {true, 0, NULL, NULL}; return s; }
static Synth is_node(IrExpr *n)    { Synth s = {false, 0, NULL, n}; return s; }
static Synth is_error(IrState *st, Span at) { return is_node(error_node(st, at)); }

static bool has_error(Synth s) { return !s.group && s.node->type->kind == IR_T_ERROR; }
static bool is_unit(Synth s)   { return !s.group && s.node->type->kind == IR_T_UNIT; }

static IR_COLD void withdraw_fix(BodyCtx *bc, uint32_t pending) {
    if (pending) bc->st->diags.items[pending - 1].fix = NULL;
}

/* The end of a fix that waits, at a consumer that cannot take the type the fix would give its operand (an array, a struct, a bool...): the
 * fix is withdrawn. `accept_int_pending` is for the consumers that take any integer (a conversion operand, an index, a slice bound). */
static Synth reject_pending(BodyCtx *bc, Synth s) {
    if (s.pending) {
        withdraw_fix(bc, s.pending);
        s.pending = 0;
    }
    return s;
}

static Synth accept_int_pending(BodyCtx *bc, Synth s) {
    if (s.pending && !is_int(s.ptype)) withdraw_fix(bc, s.pending);
    s.pending = 0;
    return s;
}

static IR_NOINLINE Synth finish_unary(BodyCtx *bc, const Expr *e, Synth o) {
    IrState *st = bc->st;
    UnOp op = e->as.unary.op;
    if (is_unit(o)) { /* a unit call is not an operand */
        report_unit(st, e->as.unary.operand->span);
        return is_error(st, e->span);
    }
    if (op != UNOP_NOT && o.group) return o;                  /* `-` and `~` of a group are groups */
    if (op == UNOP_NOT && o.group) {                          /* `!` needs a bool; a literal has no type to name */
        report_bool_expected(st, e->as.unary.operand->span);
        return is_error(st, e->span);
    }
    if (has_error(o)) { /* a unary operator on a silent operand: a fix that waits inside it goes on waiting if the operator accepts its new type */
        Synth s = is_error(st, e->span);
        if (o.pending) {
            const IrType *pt = o.ptype;
            bool ok = op == UNOP_NOT ? pt->kind == IR_T_BOOL : op == UNOP_NEG ? is_int(pt) && pt->as.integer.is_signed : is_int(pt);
            if (ok) { s.pending = o.pending; s.ptype = pt; }
            else withdraw_fix(bc, o.pending);
        }
        return s;
    }
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
    FixCtx my = bc->fix;
    /* `-x` and `~x` have x's type, so x sits where the whole sits; `!x` does not. `-` also needs a signed type (E0260 otherwise): a fix
     * must not make the operand unsigned or leave the place it is expected to fill unsigned. */
    if (e->as.unary.op == UNOP_NOT) bc->fix = (FixCtx){FIX_NONE, NULL};
    else if (e->as.unary.op == UNOP_NEG) {
        if (my.mode == FIX_FREE) bc->fix = (FixCtx){FIX_SIGNED, NULL};
        else if (my.mode == FIX_TYPE && !(my.type->kind == IR_T_INT && my.type->as.integer.is_signed)) bc->fix = (FixCtx){FIX_NONE, NULL};
    }
    Synth o = synth(bc, e->as.unary.operand);
    bc->fix = my;
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

/* May a fix make the expression's type `result`? Only where that type is known to be accepted (bc->fix). */
static bool fix_accepts(const BodyCtx *bc, const IrType *result) {
    if (bc->fix.mode == FIX_FREE || bc->fix.mode == FIX_LOCAL) return true;
    if (bc->fix.mode == FIX_SIGNED) return result->kind == IR_T_INT && result->as.integer.is_signed;
    return bc->fix.mode == FIX_TYPE && bc->fix.type->kind != IR_T_ERROR && bc->fix.type == result;
}

/* E0261 for two typed operands of different types. The fix (both integer types only) wraps exactly ONE operand in a LOSSLESS
 * widening to the other's type - never a narrowing, never a change of sign that can lose a value - and is offered only if the
 * binary's type after the wrap is known to be accepted where it sits (bc->fix, see FixCtx):
 *   - a comparison: the result is bool whichever operand is wrapped, so the fix is offered anywhere (when one direction is lossless);
 *   - an arithmetic or bitwise operator: the wrap makes the result the wider type, so it is offered where that type is expected or
 *     where nothing constrains the type (u8 + u32 -> `u32(a) + b`);
 *   - a shift: only the count is wrapped, into the left operand's type (the result type), and only if that widens it.
 * Anywhere else, no fix: a fix that leaves the program rejected is worse than none. Returns, for an arithmetic fix inside a context
 * that was passed down from an ancestor (FIX_TYPE), 1 + the index of the diagnostic: the ancestor may still have to withdraw it
 * (see settle_pending). (docs/M2_PLAN.md "Operators", E0261.) */
static IR_COLD uint32_t report_types_differ(BodyCtx *bc, const Expr *e, const IrType *lt, const IrType *rt, const IrType **ptype) {
    IrState *st = bc->st;
    const Expr *le = e->as.binary.lhs, *re = e->as.binary.rhs;
    bool wrap_left = false, arith_fix = false;
    const char *to = NULL;
    const IrType *new_type = NULL;
    if (is_int(lt) && is_int(rt)) {
        bool l2r = lossless(lt, rt), r2l = lossless(rt, lt);
        BinOp op = e->as.binary.op;
        const IrType *result = NULL;
        if (!is_arith_class(op)) { /* a comparison */
            if (l2r) { wrap_left = true; to = ir_type_text(st, rt); }
            else if (r2l) to = ir_type_text(st, lt);
        } else {
            if (is_shift(op)) {
                if (r2l) { to = ir_type_text(st, lt); result = lt; }
            } else if (l2r) {
                wrap_left = true;
                to = ir_type_text(st, rt);
                result = rt;
            } else if (r2l) {
                to = ir_type_text(st, lt);
                result = lt;
            }
            if (to && !fix_accepts(bc, result)) to = NULL;
            arith_fix = to != NULL;
            new_type = result;
        }
    }
    /* A wrap adds a conversion node above the operand, one more level on every path through it. The fixed source must still be within the
     * parser's height bound (E0111, MAX_EXPR_HEIGHT), or the cure is a different error: the longest path through this node is the nodes
     * above it (expr_depth counts this one) plus its own height after the wrap. (docs/error_codes.md E0261, the 511-operation chain.) */
    if (to) {
        unsigned lh = le->height + (wrap_left ? 1u : 0u), rh = re->height + (wrap_left ? 0u : 1u);
        size_t path = (bc->expr_depth - 1) + 1 + (lh > rh ? lh : rh);
        if (path > MAX_EXPR_HEIGHT) { to = NULL; arith_fix = false; }
    }
    size_t before = st->diags.len;
    ir_error(st, diag_ir_operand_types_differ(st->arena, e->span, ir_type_text(st, lt), ir_type_text(st, rt),
                                              le->span, re->span, wrap_left, to));
    if (arith_fix && (bc->fix.mode == FIX_TYPE || bc->fix.mode == FIX_LOCAL) && st->diags.len > before) {
        *ptype = new_type;
        return (uint32_t)before + 1;
    }
    return 0;
}


/* A literal, or unary `-`/`~` and arithmetic, bitwise and shift operators over literals: no name, no call, nothing that records a use or a
 * call edge. The only subtrees that are dry-checked (group_fits). */
static bool pure_literal_tree(const Expr *e) {
    switch (e->kind) {
        case EXPR_NUMBER: return true;
        case EXPR_UNARY: return (e->as.unary.op == UNOP_NEG || e->as.unary.op == UNOP_BITNOT) && pure_literal_tree(e->as.unary.operand);
        case EXPR_BINARY: return !is_equality(e->as.binary.op) && pure_literal_tree(e->as.binary.lhs) && pure_literal_tree(e->as.binary.rhs);
        default: return false;
    }
}

/* Would the literal group `g` check against `t` without a diagnostic (every literal in range, no `-` on an unsigned type)? It is resolved
 * again with the diagnostics muted and counted. Only a pure literal tree is ever resolved this way: muted mode records name uses and call
 * edges, and a literal tree has neither. Anything else is "no". */
static IR_COLD bool group_fits(BodyCtx *bc, const Expr *g, const IrType *t) {
    assert(pure_literal_tree(g));
    if (!t || !is_int(t) || !pure_literal_tree(g)) return false;
    IrState *st = bc->st;
    size_t hits = st->muted_hits;
    st->muted++;
    (void)resolve_group(st, g, t);
    st->muted--;
    return st->muted_hits == hits;
}

/* A binary operator with an ERROR operand is silent, but a fix that waits inside it (see Synth) was offered on the strength of this operator
 * accepting the new type, and the fixed source must stay clean HERE too. With the pending operand of type P = `ptype`:
 *   - the other operand must have type P (a typed one), fit P (an untyped literal group, dry-checked), or be silent already (ERROR);
 *     otherwise the fixed source fails at this operator and the fix is withdrawn;
 *   - the operator must be defined for P (`+` is not for bool);
 *   - the operator's own result is what the parent sees: P for arithmetic, bitwise and shift (a shift's result is its LEFT operand's
 *     type, which is P either way once they agree), bool for a comparison. The fix goes on waiting with that type.
 * Both operands carrying the same fix (`r + r` of one local) agree with each other; two different fixes are two mistakes: both are withdrawn. */
static IR_COLD Synth settle_pending(BodyCtx *bc, const Expr *e, Synth l, Synth r) {
    Synth out = is_error(bc->st, e->span);
    if (!l.pending && !r.pending) return out;
    if (l.pending && r.pending && (l.pending != r.pending || l.ptype != r.ptype)) {
        withdraw_fix(bc, l.pending);
        withdraw_fix(bc, r.pending);
        return out;
    }
    bool same = l.pending && r.pending;
    bool p_left = l.pending != 0;
    const Synth p = p_left ? l : r, o = p_left ? r : l;
    const Expr *oe = p_left ? e->as.binary.rhs : e->as.binary.lhs;
    BinOp op = e->as.binary.op;
    const IrType *pt = p.ptype;
    bool agrees = same || (!has_error(o) && (o.group ? group_fits(bc, oe, pt) : o.node->type == pt));
    bool op_ok = is_logical(op) ? pt->kind == IR_T_BOOL : operand_ok(op, pt);
    if (!agrees || !op_ok) {
        withdraw_fix(bc, p.pending);
        return out;
    }
    out.pending = p.pending;
    out.ptype = is_arith_class(op) ? pt : ir_type_bool(bc->st);
    return out;
}

/* A binary operator with at least one TYPED operand (the other may be a group). In order: an
 * ERROR operand silences everything; two groups under a comparison have no type (E0225, once,
 * at the left); an operand type the operator is not defined for is E0260, once, naming the
 * left one first; then a group takes its typed sibling's type; then the types must agree. */
static IR_NOINLINE Synth typed_binary(BodyCtx *bc, const Expr *e, Synth l, Synth r) {
    IrState *st = bc->st;
    BinOp op = e->as.binary.op;
    const Expr *le = e->as.binary.lhs, *re = e->as.binary.rhs;
    if (has_error(l) || has_error(r)) return settle_pending(bc, e, l, r);
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
        Synth s = is_error(st, e->span);
        s.pending = report_types_differ(bc, e, ln->type, rn->type, &s.ptype);
        return s;
    }
    const IrType *result = is_arith_class(op) ? ln->type : ir_type_bool(st);
    return is_node(ir_expr_binary(st, e->span, result, op, ln, rn));
}

/* `&&` and `||`: both operands bool. A typed non-bool operand is E0260 (once, the left first); a
 * literal has no type to name, so it is a mismatch against bool. */
static IR_NOINLINE Synth finish_logical(BodyCtx *bc, const Expr *e, Synth l, Synth r) {
    IrState *st = bc->st;
    BinOp op = e->as.binary.op;
    if (has_error(l) || has_error(r)) return settle_pending(bc, e, l, r);
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
    if (is_unit(l) || is_unit(r)) { /* a unit call is not an operand; the left one is reported first, once */
        report_unit(bc->st, is_unit(l) ? e->as.binary.lhs->span : e->as.binary.rhs->span);
        return is_error(bc->st, e->span);
    }
    if (is_logical(op)) return finish_logical(bc, e, l, r);
    if (is_arith_class(op) && l.group && r.group) return is_group();
    return typed_binary(bc, e, l, r);
}

/* The operands of an arithmetic or bitwise operator sit where the whole sits when that is a known type (the fix there must make the
 * operand that type); the left operand of a shift too. Anything else - a comparison, `&&`, a shift count, or a whole that is only
 * unconstrained (the other operand decides the type) - leaves the operand's place unknown. */
static Synth synth_binary(BodyCtx *bc, const Expr *e) {
    FixCtx my = bc->fix;
    BinOp op = e->as.binary.op;
    bool through = is_arith_class(op) && (my.mode == FIX_TYPE || my.mode == FIX_LOCAL);
    if (!through) bc->fix = (FixCtx){FIX_NONE, NULL};
    Synth l = synth(bc, e->as.binary.lhs);
    bc->fix = through && !is_shift(op) ? my : (FixCtx){FIX_NONE, NULL};
    Synth r = synth(bc, e->as.binary.rhs);
    bc->fix = my;
    return finish_binary(bc, e, l, r);
}

/* A resolved use of field `k` of `s` (a field access name or a construction argument name): recorded only for
 * a misnamed field. */
static void ir_note_field_use(BodyCtx *bc, const IrStruct *s, size_t k, Span at) {
    const StructInfo *si = &bc->decls->structs[s - bc->decls->irs];
    if (si->field_renames) ir_note_use(bc->st, si->field_renames[k], at);
}

/* ---- calls (typed-IR step 10, docs/M2_PLAN.md "Calls and struct construction") ----
 * The callee is looked up among the TOP-LEVEL names only (a local of the same name is irrelevant;
 * forward references are legal). Unknown: E0232 at the name, the arguments still checked for their
 * own mistakes, arity not compared, the call ERROR. A struct is construction (below). Otherwise the
 * call has the function's declared return type whatever is wrong with its arguments, so an argument
 * mistake never spreads to the code around the call. Arguments are positional only (E0234 for a
 * named one, once, at the first), their count must match (E0233, at the call) and each is checked
 * against its parameter's type, left to right (the IR keeps them in written order, Q6 (v)). */

static IR_NOINLINE void silently_check_args(BodyCtx *bc, const Expr *e) {
    const NodeList *args = &e->as.call.args;
    for (size_t i = 0; i < args->len; i++) {
        const Expr *a = args->items[i];
        bc->expr_depth += a->kind == EXPR_NAMED_ARG; /* a named argument is a level of the tree */
        ir_check_silent(bc, a->kind == EXPR_NAMED_ARG ? a->as.named_arg.value : a);
        bc->expr_depth -= a->kind == EXPR_NAMED_ARG;
    }
}

static IR_COLD void report_unknown_function(BodyCtx *bc, const Expr *e) {
    ir_note_unresolved(bc->st, bc->decls, NS_TOP, 0, e->as.call.name);
    ir_error(bc->st, diag_ir_unknown_function(bc->st->arena, e->as.call.name_span, e->as.call.name));
}

static IR_NOINLINE void record_call(BodyCtx *bc, size_t callee, Span span) {
    Ctx *cx = bc->decls;
    if (cx->n_edges == cx->cap_edges) {
        size_t cap = cx->cap_edges ? cx->cap_edges * 2 : 64;
        CallEdge *edges = arena_alloc(bc->st->arena, cap * sizeof(CallEdge), alignof(CallEdge));
        if (cx->n_edges) memcpy(edges, cx->edges, cx->n_edges * sizeof(CallEdge));
        cx->edges = edges; /* installed only after the allocation succeeded */
        cx->cap_edges = cap;
    }
    cx->edges[cx->n_edges++] = (CallEdge){(uint32_t)bc->fn_index, (uint32_t)callee, span};
}

/* The top-level name a call names (a function or a struct), or NULL after reporting E0232. */
static IR_NOINLINE const SymEntry *call_target(BodyCtx *bc, const Expr *e) {
    const SymEntry *t = symtab_lookup(&bc->decls->top, e->as.call.name);
    if (!t) {
        report_unknown_function(bc, e);
        silently_check_args(bc, e);
    }
    return t;
}

/* True if the arguments are positional and as many as the parameters, i.e. each is to be checked
 * against its parameter. Otherwise the one error (E0234 or E0233) is reported and the arguments are
 * checked for their own mistakes only. */
static IR_NOINLINE bool call_shape_ok(BodyCtx *bc, const Expr *e, const IrFunc *f) {
    IrState *st = bc->st;
    const NodeList *args = &e->as.call.args;
    for (size_t i = 0; i < args->len; i++) {
        const Expr *a = args->items[i];
        if (a->kind != EXPR_NAMED_ARG) continue;
        ir_error(st, diag_ir_named_args_to_function(st->arena, a->span, f->name));
        silently_check_args(bc, e);
        return false;
    }
    if (args->len != f->n_params) {
        ir_error(st, diag_ir_wrong_arity(st->arena, e->span, f->name, f->n_params, args->len));
        silently_check_args(bc, e);
        return false;
    }
    return true;
}

/* ---- struct construction (typed-IR step 11, docs/M2_PLAN.md "Calls and struct construction") ----
 * `S(a: 1, b: 2)`: every argument is `name: value` (E0240 otherwise, once, at the first positional
 * argument - alone or mixed - and nothing else about the arguments is checked, like E0234); the name is
 * a bare, unparenthesized identifier (E0241), a declared field (E0242), given once (E0244) and in
 * declaration order (E0245, once per construction: reversing three fields is one mistake); every
 * field is given (E0243, one per missing field, at the construction - but not when an argument could
 * not be attributed to a field, E0241/E0242, since the missing field is that argument's echo). Each
 * value is checked against its field's type, in written order. Whatever is wrong, the construction
 * has the struct's type, so a mistake in it never spreads to the code around it. The IR keeps the
 * values in declaration order (which a valid construction also writes them in). */

typedef struct {
    const IrStruct *s;
    IrExpr        **slots;         /* per field: its checked value, NULL = not given */
    size_t          highest;       /* 1 + the index of the last-declared field written so far (0 = none) */
    bool            order_reported;
    bool            unrecognized;  /* E0240, E0241 or E0242 was reported: E0243 would only echo it */
} Construct;

/* The index of the field called `name`, or SIZE_MAX. The search starts at `hint` (the field a
 * well-ordered construction names next) and wraps, so the usual case is one comparison. */
static size_t field_index(const IrStruct *s, const char *name, size_t hint) {
    for (size_t i = 0; i < s->n_fields; i++) {
        size_t k = hint + i < s->n_fields ? hint + i : hint + i - s->n_fields;
        if (strcmp(s->fields[k].name, name) == 0) return k;
    }
    return SIZE_MAX;
}

static IR_NOINLINE void construct_begin(BodyCtx *bc, Construct *c, size_t struct_index) {
    c->s = bc->decls->structs[struct_index].ir;
    c->slots = NULL;
    if (c->s->n_fields) {
        c->slots = arena_alloc(bc->st->arena, c->s->n_fields * sizeof(IrExpr *), alignof(IrExpr *));
        memset(c->slots, 0, c->s->n_fields * sizeof(IrExpr *));
    }
    c->highest = 0;
    c->order_reported = false;
    c->unrecognized = false;
}

/* True if every argument is `name: value`; otherwise E0240 at the first one that is not, with the
 * values checked for their own mistakes only. */
static IR_NOINLINE bool construct_shape_ok(BodyCtx *bc, const Expr *e) {
    const NodeList *args = &e->as.call.args;
    for (size_t i = 0; i < args->len; i++) {
        const Expr *a = args->items[i];
        if (a->kind == EXPR_NAMED_ARG) continue;
        ir_error(bc->st, diag_ir_positional_construction(a->span));
        silently_check_args(bc, e);
        return false;
    }
    return true;
}

/* The field a named argument gives, or -1 after reporting why it gives none (its value is then
 * checked for its own mistakes only). */
static IR_NOINLINE long construct_field(BodyCtx *bc, Construct *c, const Expr *a) {
    IrState *st = bc->st;
    const Expr *n = a->as.named_arg.name;
    if (n->kind != EXPR_IDENT || n->parenthesized) {
        ir_error(st, diag_ir_not_a_field_name(n->span));
        c->unrecognized = true;
        return -1;
    }
    const char *name = n->as.ident.name;
    size_t k = field_index(c->s, name, c->highest < c->s->n_fields ? c->highest : 0);
    if (k == SIZE_MAX) {
        ir_note_unresolved(st, bc->decls, NS_FIELD, (size_t)(c->s - bc->decls->irs), name);
        ir_error(st, diag_ir_no_such_field(st->arena, n->span, c->s->name, name));
        c->unrecognized = true;
        return -1;
    }
    if (c->slots[k]) {
        ir_error(st, diag_ir_field_given_twice(st->arena, n->span, name));
        ir_note_field_use(bc, c->s, k, n->span); /* the name still resolves to the field: a rename must reach it */
        return -1;
    }
    ir_note_field_use(bc, c->s, k, n->span);
    if (k + 1 < c->highest && !c->order_reported) {
        c->order_reported = true;
        ir_error(st, diag_ir_field_out_of_order(st->arena, n->span, name, c->s->fields[c->highest - 1].name));
    }
    if (k + 1 > c->highest) c->highest = k + 1;
    return (long)k;
}

static IR_NOINLINE Synth construct_finish(BodyCtx *bc, Construct *c, const Expr *e) {
    IrState *st = bc->st;
    NodeList values;
    node_list_init(&values);
    for (size_t k = 0; k < c->s->n_fields; k++) {
        if (!c->slots[k]) {
            if (!c->unrecognized) ir_error(st, diag_ir_missing_field(st->arena, e->span, c->s->fields[k].name, c->s->name));
            node_list_push(st->arena, &values, error_node(st, e->span)); /* a placeholder: the module is never produced */
        } else {
            node_list_push(st->arena, &values, c->slots[k]);
        }
    }
    return is_node(ir_expr_struct_new(st, e->span, ir_type_struct(st, c->s), values));
}

static IR_NOINLINE Synth synth_construct(BodyCtx *bc, const Expr *e, size_t struct_index) {
    Construct c;
    if (bc->decls->struct_renames) ir_note_use(bc->st, bc->decls->struct_renames[struct_index], e->as.call.name_span);
    construct_begin(bc, &c, struct_index);
    if (!construct_shape_ok(bc, e)) {
        c.unrecognized = true;
    } else {
        for (size_t i = 0; i < e->as.call.args.len; i++) {
            const Expr *a = e->as.call.args.items[i];
            long k = construct_field(bc, &c, a);
            bc->expr_depth++; /* the named argument is a node of the tree between the construction and its value */
            if (k < 0) ir_check_silent(bc, a->as.named_arg.value);
            else c.slots[k] = ir_check_expr(bc, a->as.named_arg.value, c.s->fields[k].type);
            bc->expr_depth--;
        }
    }
    return construct_finish(bc, &c, e);
}

/* ---- len(x) (typed-IR step 12, Admission #15) ----
 * A builtin, resolved BEFORE the top-level lookup (so a function or struct named len, E0257, never
 * captures the call). Exactly one positional argument of array or slice type; the result is u64 whatever
 * is wrong with the argument. The IR keeps the operand (LEN{operand}) so it is evaluated exactly once,
 * for an array too, whose length is a constant. */

static IR_NOINLINE Synth len_node(BodyCtx *bc, const Expr *e, IrExpr *operand) {
    return is_node(ir_expr_len(bc->st, e->span, ir_type_int(bc->st, 64, false), operand));
}

static IR_NOINLINE Synth len_bad_arity(BodyCtx *bc, const Expr *e) {
    const NodeList *args = &e->as.call.args;
    bool named = false;
    for (size_t i = 0; i < args->len; i++) named = named || ((const Expr *)args->items[i])->kind == EXPR_NAMED_ARG;
    ir_error(bc->st, diag_ir_len_arity(bc->st->arena, e->span, args->len, named));
    silently_check_args(bc, e);
    return len_node(bc, e, error_node(bc->st, e->span));
}

static IR_NOINLINE Synth finish_len(BodyCtx *bc, const Expr *e, Synth a) {
    IrState *st = bc->st;
    const Expr *arg = e->as.call.args.items[0];
    if (a.group) { /* a literal has no type to name */
        report_cannot_infer(st, arg->span);
        return len_node(bc, e, error_node(st, e->span));
    }
    if (is_unit(a)) {
        report_unit(st, arg->span);
        return len_node(bc, e, error_node(st, e->span));
    }
    if (!has_error(a) && a.node->type->kind != IR_T_ARRAY && a.node->type->kind != IR_T_SLICE) {
        ir_error(st, diag_ir_len_argument(st->arena, arg->span, ir_type_text(st, a.node->type)));
    }
    return len_node(bc, e, a.node);
}

static Synth synth_len(BodyCtx *bc, const Expr *e) {
    const NodeList *args = &e->as.call.args;
    if (args->len != 1 || ((const Expr *)args->items[0])->kind == EXPR_NAMED_ARG) return len_bad_arity(bc, e);
    Synth a = reject_pending(bc, synth(bc, args->items[0]));
    return finish_len(bc, e, a);
}

static Synth synth_call(BodyCtx *bc, const Expr *e) {
    if (strcmp(e->as.call.name, "len") == 0) return synth_len(bc, e);
    const SymEntry *t = call_target(bc, e);
    if (!t) return is_error(bc->st, e->span);
    if (t->is_struct) return synth_construct(bc, e, t->index);
    record_call(bc, t->index, e->span);
    if (bc->decls->fn_renames) ir_note_use(bc->st, bc->decls->fn_renames[t->index], e->as.call.name_span);
    const IrFunc *f = bc->decls->fns[t->index];
    NodeList args;
    node_list_init(&args);
    if (call_shape_ok(bc, e, f)) {
        for (size_t i = 0; i < f->n_params; i++) {
            const IrLocal *p = f->locals.items[i];
            node_list_push(bc->st->arena, &args, ir_check_expr(bc, e->as.call.args.items[i], p->type));
        }
    }
    return is_node(ir_expr_call(bc->st, e->span, f->ret, f, args));
}

/* ---- field access (typed-IR step 11) ----
 * `base.name`: the base must be a struct (E0251 otherwise, at the base) with a field of that name
 * (E0242 at the name, struct zero-field included); the result is the field's type. A literal group
 * as a base has no type (E0225), a unit call is not a value (E0226). There are no pseudo-fields:
 * `x.len` is E0251 / E0242 like any other name. */

static IR_COLD void report_no_fields(IrState *st, Span at, const IrType *t) {
    ir_error(st, diag_ir_type_has_no_fields(st->arena, at, ir_type_text(st, t)));
}

static IR_COLD void report_no_such_field(BodyCtx *bc, const Expr *e, const IrStruct *s) {
    IrState *st = bc->st;
    ir_note_unresolved(st, bc->decls, NS_FIELD, (size_t)(s - bc->decls->irs), e->as.field_access.name);
    ir_error(st, diag_ir_no_such_field(st->arena, e->as.field_access.name_span, s->name, e->as.field_access.name));
}

static IR_NOINLINE Synth finish_field(BodyCtx *bc, const Expr *e, Synth b) {
    IrState *st = bc->st;
    const Expr *base = e->as.field_access.base;
    if (b.group) { /* `(1).x`: a literal with no expected type */
        report_cannot_infer(st, base->span);
        return is_error(st, e->span);
    }
    if (is_unit(b)) {
        report_unit(st, base->span);
        return is_error(st, e->span);
    }
    if (has_error(b)) return is_error(st, e->span);
    const IrType *t = b.node->type;
    if (t->kind != IR_T_STRUCT) {
        report_no_fields(st, base->span, t);
        return is_error(st, e->span);
    }
    const IrStruct *s = t->as.strukt.decl;
    for (size_t k = 0; k < s->n_fields; k++) {
        if (strcmp(s->fields[k].name, e->as.field_access.name) == 0) {
            ir_note_field_use(bc, s, k, e->as.field_access.name_span);
            return is_node(ir_expr_field(st, e->span, s->fields[k].type, b.node, (uint32_t)k));
        }
    }
    report_no_such_field(bc, e, s);
    return is_error(st, e->span);
}

static Synth synth_field(BodyCtx *bc, const Expr *e) {
    Synth b = reject_pending(bc, synth(bc, e->as.field_access.base));
    return finish_field(bc, e, b);
}

/* ---- conversions (typed-IR step 13, docs/M2_PLAN.md "Conversions") ----
 * `u8(x)`: target and operand must both be INTEGER types. The operand is typed on its own, never from the
 * target (a conversion supplies no type): a bare literal operand (`u8(255)`, `u8(300)`, `u32(-1)`) has no type
 * and is E0225 at the operand. `bool` on either side, and a struct, array or slice operand, are E0265 at the
 * whole conversion (an integer converted to bool also suggests `x != 0`; there is no spelling for bool to
 * integer); a unit call is E0226. A same-type conversion is legal and kept (CONVERT). Whatever is wrong, the
 * result has the TARGET type, so a mistake in the operand never spreads. The IR only records the conversion;
 * the runtime rule (the value itself if it fits, else a trap) is the backends'. */

static IR_COLD void report_cannot_convert(IrState *st, Span at, const IrType *from, const IrType *to) {
    ir_error(st, diag_ir_cannot_convert(st->arena, at, ir_type_text(st, from), ir_type_text(st, to),
                                        to->kind == IR_T_BOOL && is_int(from)));
}

static IR_NOINLINE Synth finish_convert(BodyCtx *bc, const Expr *e, Synth o) {
    IrState *st = bc->st;
    const IrType *target = ir_prim_type(st, e->as.convert.target);
    assert(target); /* the parser only builds a conversion for one of the nine primitive keywords */
    IrExpr *operand;
    if (o.group) { /* a literal has no type of its own and a conversion gives it none */
        report_cannot_infer(st, e->as.convert.operand->span);
        operand = error_node(st, e->span);
    } else if (is_unit(o)) {
        report_unit(st, e->as.convert.operand->span);
        operand = error_node(st, e->span);
    } else {
        operand = o.node;
        const IrType *from = operand->type;
        if (from->kind != IR_T_ERROR && !(is_int(from) && is_int(target))) report_cannot_convert(st, e->span, from, target);
    }
    return is_node(ir_expr_convert(st, e->span, target, operand));
}

static Synth synth_convert(BodyCtx *bc, const Expr *e) {
    Synth o = accept_int_pending(bc, synth(bc, e->as.convert.operand));
    return finish_convert(bc, e, o);
}

/* ---- arrays, indexing, slicing (typed-IR step 12, docs/M2_PLAN.md "Arrays, slices, storage") ----
 * `a[i]`: a base of array or slice type (E0252 otherwise, at the base), an index of ANY integer type
 * (E0253 otherwise); the result is the element type. `a[lo..hi]`: the same base rule and integer bounds;
 * the result is `[]T`, which is an error (E0227) when T itself contains a slice. An integer LITERAL
 * index or bound has no type of its own, so it takes u64 (an out-of-range value is the verifier's, not
 * an error here). Errors come out in source order: the base is validated before the index is read.
 * Array literals and `[v; N]` take their element type from the expected array/slice type, else from the
 * first element / the value; an expected-length mismatch is one E0223; a literal whose type exceeds the
 * size limit is E0222 at the literal. `v` of `[v; N]` is evaluated exactly once, N == 0 included (the IR
 * keeps it). */

static IR_COLD void report_cannot_index(IrState *st, Span at, const IrType *t) {
    ir_error(st, diag_ir_cannot_index(st->arena, at, ir_type_text(st, t)));
}

static IR_COLD void report_index_type(IrState *st, Span at, const IrType *t) {
    ir_error(st, diag_ir_index_not_integer(st->arena, at, ir_type_text(st, t)));
}

static IR_COLD void report_too_large(IrState *st, Span at) {
    ir_error(st, diag_ir_type_too_large(at));
}

static const IrType *element_type(const IrType *t) {
    return t->kind == IR_T_ARRAY ? t->as.array.elem : t->as.slice.elem;
}

/* An index or a slice bound: an integer of any type; a literal takes u64. */
static IR_NOINLINE IrExpr *check_index_operand(BodyCtx *bc, const Expr *e) {
    IrState *st = bc->st;
    Synth s = accept_int_pending(bc, synth(bc, e));
    if (s.group) return group_against(st, e, ir_type_int(st, 64, false));
    if (is_unit(s)) {
        report_unit(st, e->span);
        return error_node(st, e->span);
    }
    if (has_error(s)) return s.node;
    if (!is_int(s.node->type)) {
        report_index_type(st, e->span, s.node->type);
        return error_node(st, e->span);
    }
    return s.node;
}

/* The element type of an indexable base, or NULL after reporting why it is not one (a literal group
 * has no type to name, a unit call is not a value, an ERROR base is silent). */
static IR_NOINLINE const IrType *indexable_element(BodyCtx *bc, const Expr *base, Synth b) {
    IrState *st = bc->st;
    if (b.group) { report_cannot_infer(st, base->span); return NULL; }
    if (is_unit(b)) { report_unit(st, base->span); return NULL; }
    if (has_error(b)) return NULL;
    const IrType *t = b.node->type;
    if (t->kind != IR_T_ARRAY && t->kind != IR_T_SLICE) {
        report_cannot_index(st, base->span, t);
        return NULL;
    }
    return element_type(t);
}

static IR_NOINLINE Synth finish_index(BodyCtx *bc, const Expr *e, Synth b, const IrType *elem) {
    IrState *st = bc->st;
    if (!elem) { /* the base is at fault: the index is checked for its own mistakes only */
        ir_check_silent(bc, e->as.index.index);
        return is_error(st, e->span);
    }
    IrExpr *i = check_index_operand(bc, e->as.index.index);
    return is_node(ir_expr_index(st, e->span, elem, b.node, i));
}

static Synth synth_index(BodyCtx *bc, const Expr *e) {
    Synth b = reject_pending(bc, synth(bc, e->as.index.base));
    return finish_index(bc, e, b, indexable_element(bc, e->as.index.base, b));
}

static bool contains_slice(const IrType *t) {
    while (t->kind == IR_T_ARRAY) t = t->as.array.elem;
    return t->kind == IR_T_SLICE;
}

static IR_NOINLINE Synth finish_slice(BodyCtx *bc, const Expr *e, Synth b, const IrType *elem) {
    IrState *st = bc->st;
    if (!elem) {
        ir_check_silent(bc, e->as.slice.lo);
        ir_check_silent(bc, e->as.slice.hi);
        return is_error(st, e->span);
    }
    IrExpr *lo = check_index_operand(bc, e->as.slice.lo);
    IrExpr *hi = check_index_operand(bc, e->as.slice.hi);
    if (contains_slice(elem)) { /* `[][]T` is not a type (a slice's element may not contain a slice) */
        ir_error(st, diag_ir_slice_of_slice(e->span));
        return is_error(st, e->span);
    }
    return is_node(ir_expr_slice(st, e->span, ir_type_slice(st, elem), b.node, lo, hi));
}

static Synth synth_slice(BodyCtx *bc, const Expr *e) {
    Synth b = reject_pending(bc, synth(bc, e->as.slice.base));
    return finish_slice(bc, e, b, indexable_element(bc, e->as.slice.base, b));
}

/* The type `[len]elem` of a literal, or NULL after E0222. */
static IR_NOINLINE const IrType *literal_array_type(IrState *st, Span at, uint64_t len, const IrType *elem) {
    const IrType *t = ir_type_array(st, len, elem);
    if (ir_type_exceeds_limit(t)) {
        report_too_large(st, at);
        return NULL;
    }
    return t;
}

static IR_NOINLINE Synth array_done(BodyCtx *bc, const Expr *e, const IrType *elem, NodeList elems) {
    const IrType *t = literal_array_type(bc->st, e->span, elems.len, elem);
    if (!t) return is_error(bc->st, e->span);
    return is_node(ir_expr_array(bc->st, e->span, t, elems));
}

/* The type a first element / a repeated value decides, or NULL after reporting. A literal group has no
 * type: E0225 at it, or - when the literal is expected to be something that is not an array at all
 * (`let x: u8 = [1, 2];`, `expected` non-NULL) - the one mismatch at the whole literal, which is the
 * mistake the author made. */
static IR_NOINLINE const IrType *deciding_type(BodyCtx *bc, const Expr *at, Synth s, const Expr *whole, const IrType *expected) {
    if (s.group) {
        if (expected) report_mismatch(bc->st, whole->span, expected, "array literal");
        else report_cannot_infer(bc->st, at->span);
        return NULL;
    }
    if (is_unit(s)) { report_unit(bc->st, at->span); return NULL; }
    if (has_error(s)) return NULL;
    return s.node->type;
}

/* The first element decides the element type. `expected`: see deciding_type (NULL = no expectation). */
static Synth array_infer(BodyCtx *bc, const Expr *e, const IrType *expected) {
    const NodeList *src = &e->as.array.elems;
    NodeList out;
    node_list_init(&out);
    Synth first = reject_pending(bc, synth(bc, src->items[0]));
    const IrType *t = deciding_type(bc, src->items[0], first, e, expected);
    if (!t) {
        for (size_t i = 1; i < src->len; i++) ir_check_silent(bc, src->items[i]);
        return is_error(bc->st, e->span);
    }
    node_list_push(bc->st->arena, &out, first.node);
    for (size_t i = 1; i < src->len; i++) node_list_push(bc->st->arena, &out, ir_check_expr(bc, src->items[i], t));
    return array_done(bc, e, t, out);
}

static IR_NOINLINE Synth repeat_done(BodyCtx *bc, const Expr *e, IrExpr *v) {
    const IrType *t = literal_array_type(bc->st, e->span, e->as.array_repeat.count, v->type);
    if (!t) return is_error(bc->st, e->span);
    return is_node(ir_expr_repeat(bc->st, e->span, t, v, e->as.array_repeat.count));
}

static Synth repeat_infer(BodyCtx *bc, const Expr *e, const IrType *expected) {
    const Expr *ve = e->as.array_repeat.value;
    Synth v = reject_pending(bc, synth(bc, ve));
    const IrType *t = deciding_type(bc, ve, v, e, expected);
    if (!t) return is_error(bc->st, e->span);
    return repeat_done(bc, e, v.node);
}

static Synth synth_array(BodyCtx *bc, const Expr *e) { return array_infer(bc, e, NULL); }
static Synth synth_repeat(BodyCtx *bc, const Expr *e) { return repeat_infer(bc, e, NULL); }

/* An array literal or `[v; N]` against an expected type. An expected array or slice type hands
 * its element type down to the elements; the result then has the literal's OWN length, and a
 * length that differs from the expected array's (or any array where a slice is expected) is the
 * one mismatch. Any other expected type: the literal is typed on its own and compared. An ERROR
 * expectation silences everything about types. */
static IR_NOINLINE IrExpr *check_array_literal(BodyCtx *bc, const Expr *e, const IrType *expected) {
    IrState *st = bc->st;
    bool repeat = e->kind == EXPR_ARRAY_REPEAT;
    if (expected->kind != IR_T_ARRAY && expected->kind != IR_T_SLICE && expected->kind != IR_T_ERROR) {
        Synth s = repeat ? repeat_infer(bc, e, expected) : array_infer(bc, e, expected);
        if (s.node->type->kind != IR_T_ERROR) report_mismatch_types(st, e->span, expected, s.node->type);
        return error_node(st, e->span);
    }
    const IrType *hint = expected->kind == IR_T_ERROR ? expected : element_type(expected);
    uint64_t len = repeat ? e->as.array_repeat.count : e->as.array.elems.len;
    NodeList out;
    node_list_init(&out);
    IrExpr *rv = NULL;
    if (repeat) {
        rv = ir_check_expr(bc, e->as.array_repeat.value, hint);
    } else {
        for (size_t i = 0; i < e->as.array.elems.len; i++) {
            node_list_push(st->arena, &out, ir_check_expr(bc, e->as.array.elems.items[i], hint));
        }
    }
    if (expected->kind == IR_T_ERROR || hint->kind == IR_T_ERROR) return error_node(st, e->span);
    const IrType *t = literal_array_type(st, e->span, len, hint);
    if (!t) return error_node(st, e->span);
    if (t != expected) {
        report_mismatch_types(st, e->span, expected, t);
        return error_node(st, e->span);
    }
    return repeat ? ir_expr_repeat(st, e->span, t, rv, len) : ir_expr_array(st, e->span, t, out);
}

static IR_NOINLINE Synth synth_leaf(BodyCtx *bc, const Expr *e) {
    IrState *st = bc->st;
    switch (e->kind) {
        case EXPR_NUMBER:
            return is_group();
        case EXPR_BOOL:
            return is_node(ir_expr_bool(st, e->span, e->as.boolean.value));
        case EXPR_IDENT: {
            Synth s = is_node(check_ident(bc, e));
            if (s.node->kind == IR_E_LOCAL) { /* a local that waits for its uses (its initializer's E0261 fix): this use is one of them */
                const IrLocal *l = s.node->as.local.local;
                if (l->fix_diag && st->diags.items[l->fix_diag - 1].fix) {
                    s.pending = l->fix_diag;
                    s.ptype = l->fix_type;
                }
            }
            return s;
        }
        default:
            unimplemented_kind(st, e);
    }
}

static Synth synth_other(BodyCtx *bc, const Expr *e);
static Synth synth_kind(BodyCtx *bc, const Expr *e);

static Synth synth(BodyCtx *bc, const Expr *e) {
    bc->st->expr_visits++;
    assert(ir_ast_height_ok(e)); /* the parser's E0111 bound; this stage asserts it, enforces none */
    bc->expr_depth++;
    Synth s = e->kind == EXPR_BINARY ? synth_binary(bc, e) : e->kind == EXPR_UNARY ? synth_unary(bc, e) : synth_other(bc, e);
    bc->expr_depth--;
    return s;
}

/* Every other kind of expression: its parts do not sit where it sits (an argument is checked against the parameter, an index against
 * nothing), so they start with an unknown place. */
static IR_NOINLINE Synth synth_other(BodyCtx *bc, const Expr *e) {
    FixCtx my = bc->fix;
    bc->fix = (FixCtx){FIX_NONE, NULL};
    Synth s = synth_kind(bc, e);
    bc->fix = my;
    return s;
}

static Synth synth_kind(BodyCtx *bc, const Expr *e) {
    if (e->kind == EXPR_CALL) return synth_call(bc, e);
    if (e->kind == EXPR_FIELD_ACCESS) return synth_field(bc, e);
    if (e->kind == EXPR_CONVERT) return synth_convert(bc, e);
    if (e->kind == EXPR_INDEX) return synth_index(bc, e);
    if (e->kind == EXPR_SLICE) return synth_slice(bc, e);
    if (e->kind == EXPR_ARRAY) return synth_array(bc, e);
    if (e->kind == EXPR_ARRAY_REPEAT) return synth_repeat(bc, e);
    return synth_leaf(bc, e);
}

/* ---- the two entry points ---- */

IrExpr *ir_check_expr(BodyCtx *bc, const Expr *e, const IrType *expected) {
    IrState *st = bc->st;
    FixCtx saved = bc->fix;
    if (e->kind == EXPR_ARRAY || e->kind == EXPR_ARRAY_REPEAT) {
        bc->fix = (FixCtx){FIX_NONE, NULL};
        bc->expr_depth++; /* this node is not synthesized; it is a level of the tree all the same */
        IrExpr *n = check_array_literal(bc, e, expected);
        bc->expr_depth--;
        bc->fix = saved;
        return n;
    }
    bc->fix = (FixCtx){FIX_TYPE, expected}; /* the expression must have exactly this type: E0261's fix may use it */
    Synth s = synth(bc, e);
    bc->fix = saved;
    if (s.pending && expected->kind != IR_T_ERROR && s.ptype != expected) withdraw_fix(bc, s.pending); /* the place does not take the new type */
    if (s.group) return group_against(st, e, expected);
    if (is_unit(s)) { /* nothing is expected to BE unit: a call that returns nothing is not a value */
        report_unit(st, e->span);
        return error_node(st, e->span);
    }
    const IrType *t = s.node->type;
    if (t->kind == IR_T_ERROR || expected->kind == IR_T_ERROR || t == expected) return s.node;
    report_mismatch_types(st, e->span, expected, t);
    return error_node(st, e->span);
}

/* The entries whose expression is not checked against a type. An expression statement is FREE (the binary's type may become whatever the fix
 * makes it: nothing else reads it). An un-annotated `let` or `for let` initializer is FIX_LOCAL: the new type would become the LOCAL's type,
 * so the fix is attached and WAITS (IrLocal.fix_diag): every later use of the local either accepts the new type or withdraws the fix
 * (`let r = a + b; return r;` in a `-> u8` function, a call argument of another type, a comparison with a u8: re-audit 3, finding 3).
 * A silent walk reports nothing, so its fixes do not matter. */
static Synth synth_in(BodyCtx *bc, const Expr *e, int mode) {
    FixCtx saved = bc->fix;
    bc->fix = (FixCtx){mode, NULL};
    Synth s = synth(bc, e);
    bc->fix = saved;
    return s;
}

/* The initializer of a new local, or the target of an assignment: the fix that waits in it (0: none) and the type it would give are
 * handed to the caller instead of being decided here. */
IrExpr *ir_infer_pending(BodyCtx *bc, const Expr *e, int mode, uint32_t *pending, const IrType **ptype) {
    IrState *st = bc->st;
    Synth s = synth_in(bc, e, mode);
    *pending = 0;
    *ptype = NULL;
    if (is_unit(s)) {
        report_unit(st, e->span);
        return error_node(st, e->span);
    }
    if (s.group) { /* a literal expression has no type of its own, and nothing expects one here */
        report_cannot_infer(st, e->span);
        return error_node(st, e->span);
    }
    *pending = s.pending;
    *ptype = s.ptype;
    return s.node;
}

IrExpr *ir_infer_expr(BodyCtx *bc, const Expr *e) {
    uint32_t pending;
    const IrType *ptype;
    IrExpr *n = ir_infer_pending(bc, e, FIX_NONE, &pending, &ptype);
    if (pending) withdraw_fix(bc, pending); /* the caller keeps no local for the fix to wait on */
    return n;
}

/* `target = value` where `target` is a local that waits (pending): the value must have the type the fix would give the local. The real
 * check (against the local's present ERROR type) said nothing about the value's type; this one asks the checker for the same value against
 * the new type with its diagnostics muted and counted, and withdraws the fix if it would have reported any. */
void ir_assign_to_pending(BodyCtx *bc, const Expr *value, uint32_t pending, const IrType *ptype) {
    IrState *st = bc->st;
    size_t hits = st->muted_hits;
    st->muted++;
    (void)ir_check_expr(bc, value, ptype);
    st->muted--;
    if (st->muted_hits != hits) withdraw_fix(bc, pending);
}

IrExpr *ir_infer_discard(BodyCtx *bc, const Expr *e) {
    IrState *st = bc->st;
    Synth s = synth_in(bc, e, FIX_FREE);
    if (!s.group) return s.node; /* a value that is thrown away may be unit */
    report_cannot_infer(st, e->span);
    return error_node(st, e->span);
}

void ir_check_silent(BodyCtx *bc, const Expr *e) {
    (void)synth_in(bc, e, FIX_NONE);
}
