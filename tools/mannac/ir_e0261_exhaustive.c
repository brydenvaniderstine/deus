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
 * Step 19 (re-audit 3, finding 10) added the contexts and shapes the first version never reached: the bound local's LATER USES (an
 * un-annotated let whose fix waits for them), literal siblings (in range, out of range, negative), a keyword directly before the operand
 * (`return-(a + b)`, `if(a < b)`, `while(..)`, `else if(..)`: the applier adds a space), logical `!` and `&&` operands, a field base,
 * a `len` argument, the first element of an inferred array, for-init and for-step, slice bounds, and two levels of nesting. The totals,
 * per category, are compared with tests/e0261_exhaustive.expected by `make test-e0261-exhaustive`: a generator that silently shrinks fails.
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

enum { C_RET, C_LET_T, C_LET, C_STMT, C_ARG, C_FIELD, C_ARR, C_ASSIGN, C_COND, C_CONV, C_INDEX, N_CTX_OLD,
       C_WHILE = N_CTX_OLD, C_ELSEIF, C_LEN, C_FBASE, C_ARR1, C_FOR_INIT, C_FOR_STEP, C_SLICE_LO, C_SLICE_HI, C_LET_USE, N_CTX };
enum { S_DIRECT, S_PAREN, S_NEG, S_NOT, S_L_ADD, S_R_ADD, S_L_SHL, S_R_SHL, S_L_LT, S_L_MUL, S_L_AND, N_SHAPE_OLD,
       S_LNOT = N_SHAPE_OLD, S_AND_L, S_AND_R, S_LIT_L, S_LIT_R, S_LIT_SHL, S_LIT_CMP, S_N1, S_N2, S_N3, S_N4, N_SHAPE };
enum { CAT_BASE, CAT_CTX, CAT_LNOT, CAT_NOSPACE, CAT_LITERAL, CAT_LOGICAL, CAT_NESTED, CAT_LATER, N_CAT };
static const char *const CAT_NAME[N_CAT] = {"base", "new contexts", "logical not", "no space after a keyword", "literal sibling", "&& operand", "two levels", "later use of the local"};
static const char *const LITS[7] = {"1", "255", "256", "65536", "4294967296", "-1", "-129"};
enum { F_PLAIN, F_CALL, F_CONSTRUCT, F_ARRAY, F_PAREN, N_FORM, F_NEG = N_FORM }; /* F_NEG (`-a`) is only used with a keyword in front of it */

typedef struct {
    int ctx, shape, lform, rform, op;
    const char *want;    /* the context's type (C_RET ... C_ASSIGN), "bool" allowed */
    const char *sib;     /* the sibling operand's type, shapes S_L_ADD ... ("bool": the && shapes) */
    int cat;             /* CAT_* */
    bool nosp;           /* no space between a keyword and the operand when the operand starts with ( - ~ ! */
    const char *lit;     /* the literal of the S_LIT_* shapes */
    int use;             /* C_LET_USE: how the bound local is used afterwards */
} Case;

static bool has_t(int ctx) { return ctx == C_RET || ctx == C_LET_T || ctx == C_ARG || ctx == C_FIELD || ctx == C_ARR || ctx == C_ASSIGN; }

static const char *form_text(char *buf, size_t cap, int form, const char *var, const char *ty) {
    switch (form) {
        case F_PLAIN: snprintf(buf, cap, "%s", var); break;
        case F_CALL: snprintf(buf, cap, "id_%s(%s + %s)", ty, var, var); break;
        case F_CONSTRUCT: snprintf(buf, cap, "P%s(x: %s + %s).x", ty, var, var); break;
        case F_ARRAY: snprintf(buf, cap, "[%s, %s + %s][1]", var, var, var); break;
        case F_NEG: snprintf(buf, cap, "-%s", var); break;
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
    }
    if (c->ctx == C_LET_USE) {
        sprintf(out + strlen(out), "fn take_%s(x: %s) { }\n", w, w);
    }
    if (has_t(c->ctx)) {
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
        case S_LNOT: snprintf(x, sizeof x, "!(%s)", e); break;
        case S_AND_L: snprintf(x, sizeof x, "(%s) && %s", e, s); break;
        case S_AND_R: snprintf(x, sizeof x, "%s && (%s)", s, e); break;
        case S_LIT_L: snprintf(x, sizeof x, "(%s) + %s", e, c->lit); break;
        case S_LIT_R: snprintf(x, sizeof x, "%s + (%s)", c->lit, e); break;
        case S_LIT_SHL: snprintf(x, sizeof x, "(%s) << %s", e, c->lit); break;
        case S_LIT_CMP: snprintf(x, sizeof x, "(%s) < %s", e, c->lit); break;
        case S_N1: snprintf(x, sizeof x, "((%s) + %s) * %s", e, s, s); break;
        case S_N2: snprintf(x, sizeof x, "-((%s) + %s)", e, s); break;
        case S_N3: snprintf(x, sizeof x, "%s + (%s * (%s))", s, s, e); break;
        case S_N4: snprintf(x, sizeof x, "~((%s) | %s)", e, s); break;
        default: snprintf(x, sizeof x, "(%s) & %s", e, s); break;
    }
    char params[96];
    if (c->sib) snprintf(params, sizeof params, "a: %s, b: %s, c: %s", ta, tb, c->sib);
    else snprintf(params, sizeof params, "a: %s, b: %s", ta, tb);
    char *o = out + strlen(out);
    const char *sp = c->nosp && strchr("(-~!", x[0]) ? "" : " ";
    switch (c->ctx) {
        case C_RET: sprintf(o, "fn f(%s) -> %s { return%s%s; }\n", params, w, sp, x); break;
        case C_WHILE: sprintf(o, "fn f(%s) { while%s%s { } }\n", params, sp, x); break;
        case C_ELSEIF: sprintf(o, "fn f(%s, k: bool) { if k { } else if%s%s { } }\n", params, sp, x); break;
        case C_LEN: sprintf(o, "fn f(%s) { let r = len(%s); }\n", params, x); break;
        case C_FBASE: sprintf(o, "fn f(%s) { let r = (%s).x; }\n", params, x); break;
        case C_ARR1:
            if (c->sib) sprintf(o, "fn f(%s) { let r = [%s, c]; }\n", params, x);
            else sprintf(o, "fn f(%s) { let r = [%s]; }\n", params, x);
            break;
        case C_FOR_INIT: sprintf(o, "fn f(%s) { for let i = %s; i < 3; i = i + 1 { } }\n", params, x); break;
        case C_FOR_STEP: sprintf(o, "fn f(%s) { for let i: u8 = 0; i < 3; i = %s { } }\n", params, x); break;
        case C_SLICE_LO: sprintf(o, "fn f(%s) { let arr: [4]u8 = [1, 2, 3, 4]; let r = arr[%s..2]; }\n", params, x); break;
        case C_SLICE_HI: sprintf(o, "fn f(%s) { let arr: [4]u8 = [1, 2, 3, 4]; let r = arr[1..%s]; }\n", params, x); break;
        case C_LET_USE: {
            static const char *const USES[9] = {"return r;", "let k = r < c;", "take_%s(r);", "arr[r];", "let k = u16(r);", "r = c;", "let k = r + c;", "if r { }", "let k = r.x;"};
            char use[64];
            snprintf(use, sizeof use, USES[c->use], w);
            sprintf(o, "fn f(%s)%s%s { let arr: [4]u8 = [1, 2, 3, 4]; let r = %s; %s }\n", params, c->use == 0 ? " -> " : "", c->use == 0 ? w : "", x, use);
            break;
        }
        case C_LET_T: sprintf(o, "fn f(%s) { let r: %s = %s; }\n", params, w, x); break;
        case C_LET: sprintf(o, "fn f(%s) { let r = %s; }\n", params, x); break;
        case C_STMT: sprintf(o, "fn f(%s) { %s; }\n", params, x); break;
        case C_ARG: sprintf(o, "fn f(%s) { take_%s(%s); }\n", params, w, x); break;
        case C_FIELD: sprintf(o, "fn f(%s) { let r = Q%s(x: %s); }\n", params, w, x); break;
        case C_ARR: sprintf(o, "fn f(%s) { let r: [1]%s = [%s]; }\n", params, w, x); break;
        case C_ASSIGN: sprintf(o, "fn f(%s) { let r: %s = %s; r = %s; }\n", params, w, strcmp(w, "bool") == 0 ? "false" : "0", x); break;
        case C_COND: sprintf(o, "fn f(%s) { if%s%s { } }\n", params, sp, x); break;
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

static long n_cases, n_single, n_beside, n_fixed, n_cmp_fixed, n_arith_fixed, n_shift_fixed;
static long cat_cases[N_CAT], cat_single[N_CAT], cat_beside[N_CAT], cat_fixed[N_CAT];
static long n_glued_single, n_glued_fixed, n_padded; /* the no-space programs whose source really has the keyword touching the operand */

static bool keyword_glued(const char *src) {
    static const char *const G[] = {"return(", "return-", "return~", "return!", "if(", "if-", "if~", "if!", "while(", "while-", "while~", "while!"};
    for (size_t i = 0; i < sizeof G / sizeof G[0]; i++) if (strstr(src, G[i])) return true;
    return false;
}

static void fail(const char *what, const char *src, const char *fixed, const char *base) {
    fprintf(stderr, "FAIL: %s\n--- source\n%s--- fixed\n%s--- baseline\n%s", what, src, fixed ? fixed : "(none)\n", base ? base : "(none)\n");
    exit(1);
}

static void run_case(const Case *c, const char *ta, const char *tb) {
    static char src[4096], base[4096];
    gen(src, c, ta, tb);
    n_cases++;
    cat_cases[c->cat]++;
    bool glued = c->nosp && keyword_glued(src);
    Result o;
    build(&o, src);
    size_t e261 = 0, at = 0;
    for (size_t i = 0; i < o.n; i++) if (strcmp(o.codes[i], "E0261") == 0) { e261++; at = i; }
    /* ours: exactly one E0261 and nothing else, or (comparisons only, step 20f) the E0261 BESIDE the diagnostic of a context that rejects the
     * comparison's bool, which the E0261 used to hide; any other diagnostic decides the program */
    bool beside = o.n != 1;
    if (o.status != IR_ERRORS || e261 != 1 || (beside && c->op < 10)) {
        release(&o);
        return;
    }
    if (beside) n_beside++;
    else n_single++;
    cat_single[c->cat] += !beside;
    cat_beside[c->cat] += beside;
    n_glued_single += glued;
    const DiagFix *fx = o.st.diags.items[at].fix;
    if (!fx) {
        release(&o);
        return;
    }
    n_fixed++;
    cat_fixed[c->cat]++;
    {   /* the self-check (step 20f): the fix removes the E0261 and changes nothing else; any failure stops the test */
        char *why = ir_fix_selfcheck(src, strlen(src), &o.st);
        if (why) fail(why, src, NULL, NULL);
    }
    n_glued_fixed += glued;
    char *fixed = ir_apply_fixes(src, strlen(src), &o.st.diags);
    /* 1: one operand, in the other's type, lossless */
    /* The wrap `T(` ... `)` adds exactly its two texts, plus ONE space when the insert would touch a keyword (`return(a)` -> `return u16((a))`):
     * only in the programs generated with the keyword glued to the operand. */
    long extra = fx->n_edits == 2 ? (long)strlen(fixed) - (long)(strlen(src) + strlen(fx->edits[0].text) + strlen(fx->edits[1].text)) : -1;
    if (fx->n_edits != 2 || extra < 0 || extra > 1 || (extra == 1 && !c->nosp)) fail("not exactly one wrap", src, fixed, NULL);
    n_padded += extra;
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
                const char *ta = TYPES[pa], *tb = TYPES[pb];
                Case c = {0};
                c.op = op;
                /* A: every context, the four shapes without a sibling (and `!` since step 19) */
                for (int ctx = 0; ctx < N_CTX_OLD; ctx++) {
                    for (int sh = S_DIRECT; sh <= S_LNOT; sh++) {
                        if (sh > S_NOT && sh < S_LNOT) continue;
                        int nw = has_t(ctx) ? 9 : 1;
                        for (int wi = 0; wi < nw; wi++) {
                            c = (Case){0}; c.op = op;
                            c.ctx = ctx; c.shape = sh; c.lform = c.rform = F_PLAIN;
                            c.cat = sh == S_LNOT ? CAT_LNOT : CAT_BASE;
                            c.want = has_t(ctx) ? wants_all[wi] : "";
                            run_case(&c, ta, tb);
                        }
                    }
                }
                /* B: the shapes with a sibling operand, in a return, a plain let and a statement */
                for (int sh = S_L_ADD; sh < N_SHAPE_OLD; sh++) {
                    for (int si = 0; si < 4; si++) {
                        for (int ci = 0; ci < 6; ci++) {
                            c = (Case){0}; c.op = op;
                            c.shape = sh; c.sib = sibs[si]; c.lform = c.rform = F_PLAIN; c.cat = CAT_BASE;
                            if (ci < 4) { c.ctx = C_RET; c.want = wants_few[ci]; }
                            else { c.ctx = ci == 4 ? C_LET : C_STMT; c.want = ""; }
                            run_case(&c, ta, tb);
                        }
                    }
                }
                /* C: operands that contain nested checked expressions, in a return and a plain let, with and without a sibling */
                for (int lf = 0; lf < N_FORM; lf++) {
                    for (int rf = 0; rf < N_FORM; rf++) {
                        for (int ci = 0; ci < 5; ci++) {
                            c = (Case){0}; c.op = op;
                            c.shape = S_DIRECT; c.lform = lf; c.rform = rf; c.cat = CAT_BASE;
                            if (ci < 4) { c.ctx = C_RET; c.want = wants_few[ci]; }
                            else { c.ctx = C_LET; c.want = ""; }
                            run_case(&c, ta, tb);
                        }
                    }
                }
                /* D (step 19): the contexts the first version never reached, with the shapes that need no sibling; a sibling for the one
                 * that takes two elements */
                for (int ctx = C_WHILE; ctx <= C_SLICE_HI; ctx++) {
                    for (int sh = S_DIRECT; sh <= S_LNOT; sh++) {
                        if (sh > S_NOT && sh < S_LNOT) continue;
                        for (int si = 0; si < (ctx == C_ARR1 ? 5 : 1); si++) {
                            c = (Case){0}; c.op = op;
                            c.ctx = ctx; c.shape = sh; c.lform = c.rform = F_PLAIN; c.want = ""; c.cat = CAT_CTX;
                            c.sib = si ? sibs[si - 1] : NULL;
                            run_case(&c, ta, tb);
                        }
                    }
                }
                /* E: a keyword directly before the operand */
                for (int ctx = 0; ctx < 4; ctx++) {
                    static const int CTXS[4] = {C_RET, C_COND, C_WHILE, C_ELSEIF};
                    for (int sh = S_PAREN; sh <= S_LNOT; sh++) {
                        if (sh > S_NOT && sh < S_LNOT) continue;
                        for (int wi = 0; wi < (CTXS[ctx] == C_RET ? 4 : 1); wi++) {
                            c = (Case){0}; c.op = op;
                            c.ctx = CTXS[ctx]; c.shape = sh; c.lform = c.rform = F_PLAIN; c.nosp = true; c.cat = CAT_NOSPACE;
                            c.want = CTXS[ctx] == C_RET ? wants_few[wi] : "";
                            run_case(&c, ta, tb);
                        }
                    }
                }
                /* E2: the OPERAND itself starts right after the keyword, so the fix's insert would touch it: `return(a) + b`, `return-a + b`,
                 * `if(a) < b`, `while(a) < b`, `else if(a) < b` (re-audit 3, finding 2, k1-k4: the applier must add the space) */
                for (int ctx = 0; ctx < 4; ctx++) {
                    static const int CTXS2[4] = {C_RET, C_COND, C_WHILE, C_ELSEIF};
                    for (int lf = 0; lf < 2; lf++) {
                        for (int wi = 0; wi < (CTXS2[ctx] == C_RET ? 4 : 1); wi++) {
                            c = (Case){0}; c.op = op;
                            c.ctx = CTXS2[ctx]; c.shape = S_DIRECT; c.lform = lf ? F_NEG : F_PAREN; c.rform = F_PLAIN; c.nosp = true; c.cat = CAT_NOSPACE;
                            c.want = CTXS2[ctx] == C_RET ? wants_few[wi] : "";
                            run_case(&c, ta, tb);
                        }
                    }
                }
                /* F: a literal beside the operator (in range, out of range, negative), in a return, a plain let and a statement */
                for (int sh = S_LIT_L; sh <= S_LIT_CMP; sh++) {
                    for (int li = 0; li < 7; li++) {
                        for (int ci = 0; ci < 6; ci++) {
                            c = (Case){0}; c.op = op;
                            c.shape = sh; c.lit = LITS[li]; c.lform = c.rform = F_PLAIN; c.cat = CAT_LITERAL;
                            if (ci < 4) { c.ctx = C_RET; c.want = wants_few[ci]; }
                            else { c.ctx = ci == 4 ? C_LET : C_STMT; c.want = ""; }
                            run_case(&c, ta, tb);
                        }
                    }
                }
                /* G: `&&` operands (the sibling is a bool parameter) */
                for (int sh = S_AND_L; sh <= S_AND_R; sh++) {
                    for (int ci = 0; ci < 4; ci++) {
                        c = (Case){0}; c.op = op;
                        c.shape = sh; c.sib = "bool"; c.lform = c.rform = F_PLAIN; c.cat = CAT_LOGICAL;
                        static const int CTXS[4] = {C_RET, C_COND, C_LET, C_STMT};
                        c.ctx = CTXS[ci]; c.want = ci == 0 ? "bool" : "";
                        run_case(&c, ta, tb);
                    }
                }
                /* H: two levels of operators above the checked one */
                for (int sh = S_N1; sh <= S_N4; sh++) {
                    for (int si = 0; si < 4; si++) {
                        for (int ci = 0; ci < 6; ci++) {
                            c = (Case){0}; c.op = op;
                            c.shape = sh; c.sib = sibs[si]; c.lform = c.rform = F_PLAIN; c.cat = CAT_NESTED;
                            if (ci < 4) { c.ctx = C_RET; c.want = wants_few[ci]; }
                            else { c.ctx = ci == 4 ? C_LET : C_STMT; c.want = ""; }
                            run_case(&c, ta, tb);
                        }
                    }
                }
                /* I: the bound local's later uses (an un-annotated let: its fix waits for them) */
                for (int use = 0; use < 9; use++) {
                    for (int si = 0; si < 4; si++) {
                        for (int wi = 0; wi < 4; wi++) {
                            c = (Case){0}; c.op = op;
                            c.ctx = C_LET_USE; c.shape = S_DIRECT; c.use = use; c.sib = sibs[si]; c.want = wants_few[wi];
                            c.lform = c.rform = F_PLAIN; c.cat = CAT_LATER;
                            run_case(&c, ta, tb);
                        }
                    }
                }
            }
        }
    }
    printf("e0261 exhaustive: %ld programs, %ld with exactly one diagnostic (E0261), %ld with the E0261 beside its context's own diagnostic, %ld offered a fix (%ld arithmetic/bitwise, %ld shift, %ld comparison); every one lossless, one wrap, self-checked, and clean or baseline-equal\n",
           n_cases, n_single, n_beside, n_fixed, n_arith_fixed, n_shift_fixed, n_cmp_fixed);
    for (int k = 0; k < N_CAT; k++) {
        printf("  %-26s %ld programs, %ld with exactly one E0261 (+%ld beside a context diagnostic), %ld offered a fix\n", CAT_NAME[k], cat_cases[k], cat_single[k], cat_beside[k], cat_fixed[k]);
        assert(cat_fixed[k] > 0); /* a category that offers no fix at all proves nothing */
    }
    printf("  keyword directly before the operand: %ld programs with one E0261, %ld offered a fix, %ld of them needed the applier's space\n", n_glued_single, n_glued_fixed, n_padded);
    assert(n_padded > 0 && n_glued_fixed > 0 && n_glued_single <= cat_single[CAT_NOSPACE] + cat_beside[CAT_NOSPACE]);
    assert(n_arith_fixed > 0 && n_shift_fixed > 0 && n_cmp_fixed > 0);
    return 0;
}
