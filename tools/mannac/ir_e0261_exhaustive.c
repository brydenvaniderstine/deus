/* Exhaustive check of E0261's fix-it (typed-IR step 18; audit findings 1 and 4 of the m2-ir-v0 re-audit).
 *
 * Every program is generated from a binary expression `L op R` whose operands have different integer types, placed in a context
 * (a return, an annotated or plain let, an argument, a construction field, an array element, an assignment, a condition, a
 * conversion operand, an index, an expression statement) and a shape (parentheses, unary - and ~, an operand of an enclosing
 * arithmetic, shift or comparison operator, operands that contain nested checked expressions). For every program whose only
 * diagnostic is one E0261 with a fix:
 *   1. the fix wraps exactly ONE operand, in the type of the OTHER operand, and that conversion is lossless (decided here, by an
 *      independent function of the two type names, not by the compiler);
 *   2. an arithmetic, bitwise or shift fix leaves a source with ZERO diagnostics, and the program with both operands already of the
 *      new type (the "baseline") has none either: a fix is offered only where the new type is accepted;
 *   3. a comparison fix leaves exactly the diagnostics the baseline has (the context may reject a bool for its own reasons; the fix
 *      neither adds nor removes any).
 * The test never says which fixes must be offered; it says every offered fix is safe, and prints how many were offered. Run:
 * `make test-e0261-exhaustive`. Not an audit of completeness: a missing fix is a usability gap, a wrong fix is a bug. */
#include "ir.h"
#include "parser_state.h"
#include "parser.h"
#include "lexer.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const TYPES[8] = {"u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64"};
static const char *const OPS[16] = {"+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>", "<", "<=", ">", ">=", "==", "!="};

static int bits_of(const char *t) { return atoi(t + 1); }
static bool signed_of(const char *t) { return t[0] == 'i'; }
/* Independent of the compiler: can every value of `from` be represented in `to`? */
static bool lossless(const char *from, const char *to) {
    if (signed_of(from) == signed_of(to)) return bits_of(to) > bits_of(from);
    return !signed_of(from) && signed_of(to) && bits_of(to) > bits_of(from);
}

enum { C_RET, C_LET_T, C_LET, C_STMT, C_ARG, C_FIELD, C_ARR, C_ASSIGN, C_COND, C_CONV, C_INDEX, N_CTX };
enum { S_DIRECT, S_PAREN, S_NEG, S_NOT, S_L_ADD, S_R_ADD, S_L_SHL, S_R_SHL, S_L_LT, S_L_MUL, S_L_AND, N_SHAPE };
enum { F_PLAIN, F_CALL, F_CONSTRUCT, F_ARRAY, F_PAREN, N_FORM };

typedef struct {
    int ctx, shape, lform, rform, op;
    const char *want;    /* the context's type (C_RET ... C_ASSIGN), "bool" allowed */
    const char *sib;     /* the sibling operand's type, shapes S_L_ADD ... */
} Case;

static bool has_t(int ctx) { return ctx == C_RET || ctx == C_LET_T || ctx == C_ARG || ctx == C_FIELD || ctx == C_ARR || ctx == C_ASSIGN; }

static const char *form_text(char *buf, size_t cap, int form, const char *var, const char *ty) {
    switch (form) {
        case F_PLAIN: snprintf(buf, cap, "%s", var); break;
        case F_CALL: snprintf(buf, cap, "id_%s(%s + %s)", ty, var, var); break;
        case F_CONSTRUCT: snprintf(buf, cap, "P%s(x: %s + %s).x", ty, var, var); break;
        case F_ARRAY: snprintf(buf, cap, "[%s, %s + %s][1]", var, var, var); break;
        default: snprintf(buf, cap, "(%s)", var); break;
    }
    return buf;
}

static void helper(char *out, const char *ty, const char *seen, char *seen_out) {
    (void)seen;
    (void)seen_out;
    sprintf(out + strlen(out), "fn id_%s(x: %s) -> %s { return x; }\nstruct P%s { x: %s, }\n", ty, ty, ty, ty, ty);
}

/* The whole program for operand types ta, tb. */
static void gen(char *out, const Case *c, const char *ta, const char *tb) {
    strcpy(out, "manna v0;\n");
    const char *ty[4] = {ta, tb, c->sib ? c->sib : ta, NULL};
    for (int i = 0; i < 3; i++) {
        bool dup = false;
        for (int j = 0; j < i; j++) dup = dup || strcmp(ty[i], ty[j]) == 0;
        if (!dup) helper(out, ty[i], NULL, NULL);
    }
    const char *w = c->want;
    if (has_t(c->ctx)) {
        if (c->ctx == C_ARG) sprintf(out + strlen(out), "fn take_%s(x: %s) { }\n", w, w);
        if (c->ctx == C_FIELD) sprintf(out + strlen(out), "struct Q%s { x: %s, }\n", w, w);
    }
    char lb[96], rb[96], e[400], x[600];
    form_text(lb, sizeof lb, c->lform, "a", ta);
    form_text(rb, sizeof rb, c->rform, "b", tb);
    snprintf(e, sizeof e, "%s %s %s", lb, OPS[c->op], rb);
    const char *s = c->sib ? "c" : "";
    switch (c->shape) {
        case S_DIRECT: snprintf(x, sizeof x, "%s", e); break;
        case S_PAREN: snprintf(x, sizeof x, "(%s)", e); break;
        case S_NEG: snprintf(x, sizeof x, "-(%s)", e); break;
        case S_NOT: snprintf(x, sizeof x, "~(%s)", e); break;
        case S_L_ADD: snprintf(x, sizeof x, "(%s) + %s", e, s); break;
        case S_R_ADD: snprintf(x, sizeof x, "%s + (%s)", s, e); break;
        case S_L_SHL: snprintf(x, sizeof x, "(%s) << %s", e, s); break;
        case S_R_SHL: snprintf(x, sizeof x, "%s << (%s)", s, e); break;
        case S_L_LT: snprintf(x, sizeof x, "(%s) < %s", e, s); break;
        case S_L_MUL: snprintf(x, sizeof x, "(%s) * %s", e, s); break;
        default: snprintf(x, sizeof x, "(%s) & %s", e, s); break;
    }
    char params[96];
    if (c->sib) snprintf(params, sizeof params, "a: %s, b: %s, c: %s", ta, tb, c->sib);
    else snprintf(params, sizeof params, "a: %s, b: %s", ta, tb);
    char *o = out + strlen(out);
    switch (c->ctx) {
        case C_RET: sprintf(o, "fn f(%s) -> %s { return %s; }\n", params, w, x); break;
        case C_LET_T: sprintf(o, "fn f(%s) { let r: %s = %s; }\n", params, w, x); break;
        case C_LET: sprintf(o, "fn f(%s) { let r = %s; }\n", params, x); break;
        case C_STMT: sprintf(o, "fn f(%s) { %s; }\n", params, x); break;
        case C_ARG: sprintf(o, "fn f(%s) { take_%s(%s); }\n", params, w, x); break;
        case C_FIELD: sprintf(o, "fn f(%s) { let r = Q%s(x: %s); }\n", params, w, x); break;
        case C_ARR: sprintf(o, "fn f(%s) { let r: [1]%s = [%s]; }\n", params, w, x); break;
        case C_ASSIGN: sprintf(o, "fn f(%s) { let r: %s = %s; r = %s; }\n", params, w, strcmp(w, "bool") == 0 ? "false" : "0", x); break;
        case C_COND: sprintf(o, "fn f(%s) { if %s { } }\n", params, x); break;
        case C_CONV: sprintf(o, "fn f(%s) { let r = u32(%s); }\n", params, x); break;
        default: sprintf(o, "fn f(%s) { let arr: [4]u8 = [1, 2, 3, 4]; let r = arr[%s]; }\n", params, x); break;
    }
}

/* ---- build ---- */

typedef struct {
    IrState st;
    ParserState ps;
    NodeList items;
    IrStatus status;
    char codes[64][8];
    size_t n;
} Result;

static void build(Result *r, const char *src) {
    parser_state_init(&r->ps, 1 << 20);
    node_list_init(&r->items);
    if (setjmp(r->ps.fatal_target) == 0) {
        Lexer lx;
        lexer_init(&lx, src, strlen(src));
        (void)parser_parse_file(&r->ps, &lx, &r->items);
    }
    ir_state_init(&r->st, 1 << 20);
    IrModule *m = NULL;
    r->status = ir_build(&r->st, &r->ps.diags, &r->items, &m);
    r->n = r->st.diags.len;
    memset(r->codes, 0, sizeof r->codes);
    for (size_t i = 0; i < r->n && i < 64; i++) strcpy(r->codes[i], r->st.diags.items[i].code);
}

static void release(Result *r) {
    ir_state_destroy(&r->st);
    parser_state_destroy(&r->ps);
}

static long n_cases, n_single, n_fixed, n_cmp_fixed, n_arith_fixed, n_shift_fixed;

static void fail(const char *what, const char *src, const char *fixed, const char *base) {
    fprintf(stderr, "FAIL: %s\n--- source\n%s--- fixed\n%s--- baseline\n%s", what, src, fixed ? fixed : "(none)\n", base ? base : "(none)\n");
    exit(1);
}

static void run_case(const Case *c, const char *ta, const char *tb) {
    static char src[4096], base[4096];
    gen(src, c, ta, tb);
    n_cases++;
    Result o;
    build(&o, src);
    if (o.status != IR_ERRORS || o.n != 1 || strcmp(o.codes[0], "E0261") != 0) { /* another diagnostic decides this program: not ours */
        release(&o);
        return;
    }
    n_single++;
    const DiagFix *fx = o.st.diags.items[0].fix;
    if (!fx) {
        release(&o);
        return;
    }
    n_fixed++;
    char *fixed = ir_apply_fixes(src, strlen(src), &o.st.diags);
    /* 1: one operand, in the other's type, lossless */
    if (fx->n_edits != 2 || strlen(fixed) != strlen(src) + strlen(fx->edits[0].text) + strlen(fx->edits[1].text)) fail("not exactly one wrap", src, fixed, NULL);
    char to[8];
    size_t tl = strlen(fx->edits[0].text);
    assert(tl >= 3 && tl < 8 && fx->edits[0].text[tl - 1] == '(' && strcmp(fx->edits[1].text, ")") == 0);
    memcpy(to, fx->edits[0].text, tl - 1);
    to[tl - 1] = '\0';
    const char *from;
    if (strcmp(to, ta) == 0) from = tb;
    else if (strcmp(to, tb) == 0) from = ta;
    else fail("wrapped into a type that is neither operand's", src, fixed, NULL);
    if (!lossless(from, to)) fail("a lossy wrap was offered", src, fixed, NULL);
    /* 2 and 3: against the baseline, the same program with both operands already of the new type */
    gen(base, c, to, to);
    Result b, f;
    build(&b, base);
    build(&f, fixed);
    bool cmp = c->op >= 10;
    if (cmp) {
        n_cmp_fixed++;
        bool same = f.n == b.n;
        for (size_t i = 0; same && i < b.n; i++) same = strcmp(f.codes[i], b.codes[i]) == 0;
        if (!same) fail("a comparison fix changed the context's diagnostics", src, fixed, base);
    } else {
        if (c->op == 8 || c->op == 9) n_shift_fixed++;
        else n_arith_fixed++;
        if (b.n != 0) fail("an arithmetic fix was offered where the new type is not accepted", src, fixed, base);
        if (f.n != 0) fail("the fixed source has diagnostics", src, fixed, base);
    }
    release(&b);
    release(&f);
    free(fixed);
    release(&o);
}

int main(void) {
    const char *wants_all[9] = {"u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64", "bool"};
    const char *wants_few[4] = {"u8", "u32", "i64", "bool"};
    const char *sibs[4] = {"u8", "u32", "i16", "i64"};
    for (int pa = 0; pa < 8; pa++) {
        for (int pb = 0; pb < 8; pb++) {
            if (pa == pb) continue;
            for (int op = 0; op < 16; op++) {
                Case c = {0};
                c.op = op;
                /* A: every context, the four shapes without a sibling */
                for (int ctx = 0; ctx < N_CTX; ctx++) {
                    for (int sh = S_DIRECT; sh <= S_NOT; sh++) {
                        int nw = has_t(ctx) ? 9 : 1;
                        for (int wi = 0; wi < nw; wi++) {
                            c.ctx = ctx; c.shape = sh; c.lform = c.rform = F_PLAIN; c.sib = NULL;
                            c.want = has_t(ctx) ? wants_all[wi] : "";
                            run_case(&c, TYPES[pa], TYPES[pb]);
                        }
                    }
                }
                /* B: the shapes with a sibling operand, in a return, a plain let and a statement */
                for (int sh = S_L_ADD; sh < N_SHAPE; sh++) {
                    for (int si = 0; si < 4; si++) {
                        for (int ci = 0; ci < 6; ci++) {
                            c.shape = sh; c.sib = sibs[si]; c.lform = c.rform = F_PLAIN;
                            if (ci < 4) { c.ctx = C_RET; c.want = wants_few[ci]; }
                            else { c.ctx = ci == 4 ? C_LET : C_STMT; c.want = ""; }
                            run_case(&c, TYPES[pa], TYPES[pb]);
                        }
                    }
                }
                /* C: operands that contain nested checked expressions, in a return and a plain let, with and without a sibling */
                for (int lf = 0; lf < N_FORM; lf++) {
                    for (int rf = 0; rf < N_FORM; rf++) {
                        for (int ci = 0; ci < 5; ci++) {
                            c.shape = S_DIRECT; c.sib = NULL; c.lform = lf; c.rform = rf;
                            if (ci < 4) { c.ctx = C_RET; c.want = wants_few[ci]; }
                            else { c.ctx = C_LET; c.want = ""; }
                            run_case(&c, TYPES[pa], TYPES[pb]);
                        }
                    }
                }
            }
        }
    }
    printf("e0261 exhaustive: %ld programs, %ld with exactly one diagnostic (E0261), %ld offered a fix (%ld arithmetic/bitwise, %ld shift, %ld comparison); every one lossless, one wrap, and clean or baseline-equal\n",
           n_cases, n_single, n_fixed, n_arith_fixed, n_shift_fixed, n_cmp_fixed);
    assert(n_arith_fixed > 0 && n_shift_fixed > 0 && n_cmp_fixed > 0);
    return 0;
}
