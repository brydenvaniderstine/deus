/* POSIX feature macro first: tmpfile/ftell are C11, but keep the strict-header
 * lesson of the parser's G6 (glibc -std=c11 hides POSIX declarations). */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ir.h"
#include "lexer.h"
#include "parser.h"
#include "parser_state.h"

/* Unit tests for the typed IR (docs/TYPED_IR.md step 3): types and interning,
 * IrState (caps, E0201, E0203, reuse). Grows in 3b (dump, stack) and 3c (gate,
 * build). Plain assert(): the Makefile builds without NDEBUG. */

static Span mk(int sl, int sc, int el, int ec) {
    Span s = {sl, sc, el, ec};
    return s;
}

static void pass(const char *what) {
    printf("PASS: %s\n", what);
}

/* ---- types ---- */

static void test_primitives(void) {
    static IrState st;
    ir_state_init(&st, 1 << 20);

    const char *const names[8] = {"u8", "i8", "u16", "i16", "u32", "i32", "u64", "i64"};
    const IrType *seen[11];
    int n = 0;
    for (int i = 0; i < 8; i++) {
        int bits = 8 << (i / 2);
        bool sg = (i % 2) == 1;
        const IrType *t = ir_type_int(&st, bits, sg);
        assert(t->kind == IR_T_INT && t->as.integer.bits == bits && t->as.integer.is_signed == sg);
        assert(ir_type_int(&st, bits, sg) == t); /* canonical */
        assert(strcmp(ir_type_text(&st, t), names[i]) == 0);
        seen[n++] = t;
    }
    seen[n++] = ir_type_bool(&st);
    seen[n++] = ir_type_unit(&st);
    seen[n++] = ir_type_error(&st);
    assert(ir_type_bool(&st)->kind == IR_T_BOOL && ir_type_unit(&st)->kind == IR_T_UNIT);
    assert(ir_type_error(&st)->kind == IR_T_ERROR);
    assert(strcmp(ir_type_text(&st, ir_type_bool(&st)), "bool") == 0);
    assert(strcmp(ir_type_text(&st, ir_type_unit(&st)), "unit") == 0);
    assert(strcmp(ir_type_text(&st, ir_type_error(&st)), "<error>") == 0);
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) assert(seen[i] != seen[j]);
    }
    ir_state_destroy(&st);
    pass("primitives: eleven distinct canonical types with the right text");
}

static void test_composites(void) {
    static IrState st;
    ir_state_init(&st, 4 << 20);
    const IrType *u8 = ir_type_int(&st, 8, false);
    const IrType *i32 = ir_type_int(&st, 32, true);

    const IrType *a4 = ir_type_array(&st, 4, u8);
    assert(a4->kind == IR_T_ARRAY && a4->as.array.len == 4 && a4->as.array.elem == u8);
    assert(ir_type_array(&st, 4, u8) == a4);
    assert(ir_type_array(&st, 5, u8) != a4);   /* length is part of the type */
    assert(ir_type_array(&st, 4, i32) != a4);  /* so is the element */
    const IrType *a0 = ir_type_array(&st, 0, u8);
    assert(a0 != a4 && ir_type_array(&st, 0, u8) == a0);

    const IrType *s = ir_type_slice(&st, u8);
    assert(s->kind == IR_T_SLICE && s->as.slice.elem == u8);
    assert(ir_type_slice(&st, u8) == s && ir_type_slice(&st, i32) != s);
    assert(s != a4 && s != a0);

    const IrType *nested = ir_type_array(&st, 2, a4);
    assert(ir_type_array(&st, 2, ir_type_array(&st, 4, u8)) == nested);
    assert(strcmp(ir_type_text(&st, a4), "[4]u8") == 0);
    assert(strcmp(ir_type_text(&st, s), "[]u8") == 0);
    assert(strcmp(ir_type_text(&st, nested), "[2][4]u8") == 0);
    assert(strcmp(ir_type_text(&st, ir_type_slice(&st, a4)), "[][4]u8") == 0);
    assert(strcmp(ir_type_text(&st, ir_type_array(&st, 18446744073709551615ULL, u8)), "[18446744073709551615]u8") == 0);

    IrStruct pt = {0}, pt2 = {0};
    pt.name = "Point";
    pt2.name = "Point"; /* same name, different declaration: a different type */
    const IrType *tp = ir_type_struct(&st, &pt);
    assert(tp->kind == IR_T_STRUCT && tp->as.strukt.decl == &pt);
    assert(ir_type_struct(&st, &pt) == tp && ir_type_struct(&st, &pt2) != tp);
    assert(strcmp(ir_type_text(&st, tp), "Point") == 0);
    assert(strcmp(ir_type_text(&st, ir_type_array(&st, 3, tp)), "[3]Point") == 0);

    ir_state_destroy(&st);
    pass("composites: arrays, slices and structs are interned by structure");
}

static void test_table_growth(void) {
    static IrState st;
    ir_state_init(&st, 16 << 20);
    const IrType *u8 = ir_type_int(&st, 8, false);
    enum { N = 5000 };
    static const IrType *first[N];
    for (int i = 0; i < N; i++) first[i] = ir_type_array(&st, (uint64_t)i, u8);
    const IrType *early = first[0];
    for (int i = 0; i < N; i++) {
        assert(ir_type_array(&st, (uint64_t)i, u8) == first[i]); /* stable across every growth */
        assert(first[i]->as.array.len == (uint64_t)i);
        for (int j = 0; j < i && j < 3; j++) assert(first[i] != first[j]);
    }
    assert(first[0] == early);
    assert(st.composites.count == N);
    ir_state_destroy(&st);
    pass("table growth: 5000 distinct arrays, every pointer stable across rehashes");
}

/* Rehashing must carry every kind of composite across, not just arrays: slices and
 * structs interleaved with arrays, enough of each to force several growths. */
static void test_table_growth_mixed_kinds(void) {
    static IrState st;
    ir_state_init(&st, 16 << 20);
    const IrType *u8 = ir_type_int(&st, 8, false);
    enum { N = 1500 };
    static IrStruct decls[N];
    static const IrType *arr[N], *sl[N], *str[N];
    for (int i = 0; i < N; i++) {
        decls[i].name = "S";
        arr[i] = ir_type_array(&st, (uint64_t)i, u8);
        sl[i] = ir_type_slice(&st, arr[i]);   /* a distinct elem each time: N distinct slices */
        str[i] = ir_type_struct(&st, &decls[i]);
    }
    assert(st.composites.count == 3 * N);
    for (int i = 0; i < N; i++) {
        assert(ir_type_array(&st, (uint64_t)i, u8) == arr[i]);
        assert(ir_type_slice(&st, arr[i]) == sl[i]);
        assert(ir_type_struct(&st, &decls[i]) == str[i]);
    }
    assert(st.composites.count == 3 * N); /* nothing was duplicated by a rehash */
    ir_state_destroy(&st);
    pass("table growth: arrays, slices and structs interleaved, all stable across rehashes");
}

/* ---- state ---- */

static void test_e0203(void) {
    static IrState st;
    ir_state_init(&st, 1 << 20);
    /* Nothing can leave the setjmp frame un-read, so the counters live in st. */
    if (setjmp(st.fatal_target) != 0) {
        assert(st.error_count == IR_MAX_ERRORS);
        assert(st.diags.len == IR_MAX_ERRORS + 1);
        const Diagnostic *last = &st.diags.items[st.diags.len - 1];
        assert(strcmp(last->code, "E0203") == 0);
        assert(strcmp(last->message, "too many errors, stopping") == 0);
        assert(last->fix == NULL);
        /* located at the 20th error's span (line 20) */
        assert(last->span.start_line == IR_MAX_ERRORS && last->span.start_col == 3);
        assert(strcmp(st.diags.items[0].code, "E0201") != 0);
        ir_state_destroy(&st);
        pass("E0203: the 20th error records a 21st diagnostic at its span, then unwinds");
        return;
    }
    for (int i = 1; i <= IR_MAX_ERRORS; i++) {
        Diagnostic d = diag_ir_input_too_large(mk(i, 3, i, 4)); /* any E02xx-shaped diagnostic */
        d.code = "E0223";
        ir_error(&st, d);
        assert(i < IR_MAX_ERRORS); /* the 20th never returns */
        assert(st.diags.len == (size_t)i);
    }
    assert(!"unreachable: the 20th ir_error must longjmp");
}

static void test_e0201_and_reuse(void) {
    static IrState st;
    ir_state_init(&st, 64 << 10);
    st.progress = mk(7, 1, 9, 2);

    if (setjmp(st.fatal_target) != 0) {
        assert(st.diags.len == 1);
        const Diagnostic *d = &st.diags.items[0];
        assert(strcmp(d->code, "E0201") == 0);
        assert(strcmp(d->message, "input too large: the type checker's memory limit was exceeded") == 0);
        assert(d->span.start_line == 7 && d->span.start_col == 1); /* the declaration being built */
        assert(d->fix == NULL);
        pass("E0201: arena exhaustion records the code at the declaration being built");
        goto reuse;
    }
    for (;;) arena_alloc(st.arena, 1000, 8);
reuse:;
    /* Reuse after a fatal (the parser's R6/G4 lessons): the hook must fire again. */
    size_t used_before = arena_used(st.arena);
    ir_state_reset_for_build(&st);
    assert(st.error_count == 0 && st.composites.count == 0 && st.composites.slots == NULL);
    assert(arena_used(st.arena) == used_before); /* the arena is not reset */
    static int second;
    if (setjmp(st.fatal_target) != 0) {
        second = 1;
        assert(st.diags.len == 2 && strcmp(st.diags.items[1].code, "E0201") == 0);
        assert(st.diags.items[1].span.start_line == 1); /* progress was reset to 1:1 */
        ir_state_destroy(&st);
        pass("reuse after a fatal: counters reset, hook re-armed (second E0201 recorded)");
        return;
    }
    for (;;) arena_alloc(st.arena, 1000, 8);
    assert(!second);
}

/* Fix 1 (review): the primitives must survive, or be rebuilt in place, across
 * a reset. They are embedded in IrState, and the reset rebuilds them; prove
 * both by scribbling over them first. */
static void test_reset_primitives(void) {
    static IrState st;
    ir_state_init(&st, 1 << 20);

    const IrType *before[11];
    int n = 0;
    for (int i = 0; i < 8; i++) before[n++] = ir_type_int(&st, 8 << (i / 2), (i % 2) == 1);
    before[n++] = ir_type_bool(&st);
    before[n++] = ir_type_unit(&st);
    before[n++] = ir_type_error(&st);
    const IrType *arr = ir_type_array(&st, 3, before[0]);

    /* A fatal that leaves the arena usable (an exhausted arena could not even
     * re-intern the array below): the 20th error. */
    if (setjmp(st.fatal_target) == 0) {
        for (int i = 1; i <= IR_MAX_ERRORS; i++) {
            Diagnostic d = diag_ir_too_many_errors(mk(i, 1, i, 2));
            d.code = "E0223";
            ir_error(&st, d);
        }
    }
    assert(st.error_count == IR_MAX_ERRORS);
    memset(st.prim, 0xFF, sizeof(st.prim)); /* worst case: nothing survived */
    assert(st.composites.count == 1);
    ir_state_reset_for_build(&st);
    assert(st.composites.count == 0 && st.composites.slots == NULL && st.composites.cap == 0);

    const IrType *after[11];
    n = 0;
    for (int i = 0; i < 8; i++) after[n++] = ir_type_int(&st, 8 << (i / 2), (i % 2) == 1);
    after[n++] = ir_type_bool(&st);
    after[n++] = ir_type_unit(&st);
    after[n++] = ir_type_error(&st);
    for (int i = 0; i < 11; i++) assert(after[i] == before[i]); /* same identity after the reset */
    assert(after[0]->kind == IR_T_INT && after[0]->as.integer.bits == 8 && !after[0]->as.integer.is_signed);
    assert(after[7]->kind == IR_T_INT && after[7]->as.integer.bits == 64 && after[7]->as.integer.is_signed);
    assert(after[8]->kind == IR_T_BOOL && after[9]->kind == IR_T_UNIT && after[10]->kind == IR_T_ERROR);
    assert(strcmp(ir_type_text(&st, after[7]), "i64") == 0);
    /* The composite table was emptied: the old array type is not handed out again. */
    const IrType *arr2 = ir_type_array(&st, 3, after[0]);
    assert(arr2->kind == IR_T_ARRAY && arr2->as.array.elem == after[0]);
    assert(arr2 != arr);
    ir_state_destroy(&st);
    pass("reset: primitive identity preserved (rebuilt in place); composites cleared");
}

/* ---- dump ---- */

/* Runs `dump` into a tmpfile and returns the text (malloc'd; caller frees). */
static char *capture(void (*dump)(FILE *, const void *), const void *node) {
    FILE *f = tmpfile();
    assert(f);
    dump(f, node);
    long n = ftell(f);
    assert(n >= 0);
    rewind(f);
    char *buf = malloc((size_t)n + 1);
    assert(buf);
    size_t got = fread(buf, 1, (size_t)n, f);
    assert(got == (size_t)n);
    buf[n] = 0;
    fclose(f);
    return buf;
}

static void dump_e(FILE *f, const void *n) { ir_dump_expr(f, n, 0); }
static void dump_s(FILE *f, const void *n) { ir_dump_stmt(f, n, 0); }
static void dump_f(FILE *f, const void *n) { ir_dump_func(f, n); }
static void dump_st(FILE *f, const void *n) { ir_dump_struct(f, n); }
static void dump_m(FILE *f, const void *n) { ir_dump_module(f, n); }

static void expect(char *got, const char *want) {
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "dump mismatch\n--- got\n%s\n--- want\n%s\n---\n", got, want);
        abort();
    }
    free(got);
}

static NodeList list1(IrState *st, void *a) {
    NodeList l;
    node_list_init(&l);
    node_list_push(st->arena, &l, a);
    return l;
}

static NodeList list2(IrState *st, void *a, void *b) {
    NodeList l = list1(st, a);
    node_list_push(st->arena, &l, b);
    return l;
}

static void test_dump_expressions(void) {
    static IrState st;
    ir_state_init(&st, 1 << 20);
    const IrType *u8 = ir_type_int(&st, 8, false), *i8 = ir_type_int(&st, 8, true);
    const IrType *u32 = ir_type_int(&st, 32, false), *i64t = ir_type_int(&st, 64, true);
    const IrType *u64t = ir_type_int(&st, 64, false), *bl = ir_type_bool(&st);
    Span sp = mk(1, 1, 1, 4);

    /* integers print by the TYPE's signedness; i64::MIN and u64::MAX are the edges */
    expect(capture(dump_e, ir_expr_int(&st, sp, u8, 255)), "(int@1:1-1:4 255 u8)");
    expect(capture(dump_e, ir_expr_int(&st, sp, i8, (uint64_t)(int64_t)-128)), "(int@1:1-1:4 -128 i8)");
    expect(capture(dump_e, ir_expr_int(&st, sp, i8, 127)), "(int@1:1-1:4 127 i8)");
    expect(capture(dump_e, ir_expr_int(&st, sp, i64t, 0x8000000000000000ULL)), "(int@1:1-1:4 -9223372036854775808 i64)");
    expect(capture(dump_e, ir_expr_int(&st, sp, u64t, 0xFFFFFFFFFFFFFFFFULL)), "(int@1:1-1:4 18446744073709551615 u64)");
    expect(capture(dump_e, ir_expr_bool(&st, sp, true)), "(bool@1:1-1:4 true bool)");
    expect(capture(dump_e, ir_expr_bool(&st, sp, false)), "(bool@1:1-1:4 false bool)");

    IrLocal *a = ir_local_new(&st, 0, "a", u32, mk(2, 8, 2, 17), true);
    IrLocal *b = ir_local_new(&st, 1, "b", u32, mk(2, 19, 2, 28), true);
    IrExpr *la = ir_expr_local(&st, mk(3, 12, 3, 13), a), *lb = ir_expr_local(&st, mk(3, 16, 3, 17), b);
    expect(capture(dump_e, la), "(local@3:12-3:13 a #0 u32)");

    expect(capture(dump_e, ir_expr_unary(&st, sp, bl, UNOP_NOT, ir_expr_bool(&st, sp, true))),
           "(unary@1:1-1:4 ! bool\n  (bool@1:1-1:4 true bool))");
    expect(capture(dump_e, ir_expr_unary(&st, sp, i8, UNOP_NEG, ir_expr_int(&st, sp, i8, 5))),
           "(unary@1:1-1:4 - i8\n  (int@1:1-1:4 5 i8))");
    expect(capture(dump_e, ir_expr_unary(&st, sp, u32, UNOP_BITNOT, la)),
           "(unary@1:1-1:4 ~ u32\n  (local@3:12-3:13 a #0 u32))");

    /* every operator's spelling, in BinOp order */
    static const char *const ops[] = {"+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>",
                                      "<", ">", "<=", ">=", "==", "!=", "&&", "||"};
    for (int op = BINOP_ADD; op <= BINOP_OR; op++) {
        char want[160];
        snprintf(want, sizeof want, "(binary@1:1-1:4 %s u32\n  (local@3:12-3:13 a #0 u32)\n  (local@3:16-3:17 b #1 u32))", ops[op]);
        expect(capture(dump_e, ir_expr_binary(&st, sp, u32, (BinOp)op, la, lb)), want);
    }

    /* calls: the id, the type (a unit-typed call prints `unit`), the arguments in order */
    IrFunc *g = ir_func_new(&st, mk(1, 1, 1, 2), "g", false, list1(&st, a), 1, ir_type_unit(&st), NULL);
    expect(capture(dump_e, ir_expr_call(&st, sp, ir_type_unit(&st), g, list1(&st, la))),
           "(call@1:1-1:4 fn:g unit\n  (local@3:12-3:13 a #0 u32))");
    expect(capture(dump_e, ir_expr_call(&st, sp, u32, g, list2(&st, la, lb))),
           "(call@1:1-1:4 fn:g u32\n  (local@3:12-3:13 a #0 u32)\n  (local@3:16-3:17 b #1 u32))");

    /* structs, fields */
    IrStruct pt = {0};
    IrField fields[2] = {{"x", mk(2, 5, 2, 11), u32, 0}, {"y", mk(3, 5, 3, 11), u32, 4}};
    pt.name = "Point"; pt.span = mk(1, 1, 4, 2); pt.fields = fields; pt.n_fields = 2; pt.size = 8; pt.align = 4;
    const IrType *tp = ir_type_struct(&st, &pt);
    IrLocal *p = ir_local_new(&st, 0, "p", tp, sp, false);
    IrExpr *lp = ir_expr_local(&st, sp, p);
    expect(capture(dump_e, ir_expr_struct_new(&st, sp, tp, list2(&st, ir_expr_int(&st, sp, u32, 1), ir_expr_int(&st, sp, u32, 2)))),
           "(struct_new@1:1-1:4 Point\n  (int@1:1-1:4 1 u32)\n  (int@1:1-1:4 2 u32))");
    expect(capture(dump_e, ir_expr_field(&st, sp, u32, lp, 1)),
           "(field@1:1-1:4 y #1 u32\n  (local@1:1-1:4 p #0 Point))");
    expect(capture(dump_st, &pt),
           "(struct@1:1-4:2 Point size=8 align=4\n  (field@2:5-2:11 x u32 +0)\n  (field@3:5-3:11 y u32 +4))");
    IrStruct empty = {0};
    empty.name = "Empty"; empty.span = mk(1, 1, 1, 15); empty.align = 1;
    expect(capture(dump_st, &empty), "(struct@1:1-1:15 Empty size=0 align=1)");

    /* arrays, slices, indexing, conversion */
    const IrType *a3 = ir_type_array(&st, 3, u8), *s8 = ir_type_slice(&st, u8);
    IrExpr *e1 = ir_expr_int(&st, sp, u8, 1), *e2 = ir_expr_int(&st, sp, u8, 2);
    expect(capture(dump_e, ir_expr_array(&st, sp, ir_type_array(&st, 2, u8), list2(&st, e1, e2))),
           "(array@1:1-1:4 [2]u8\n  (int@1:1-1:4 1 u8)\n  (int@1:1-1:4 2 u8))");
    expect(capture(dump_e, ir_expr_repeat(&st, sp, a3, e1, 3)), "(repeat@1:1-1:4 3 [3]u8\n  (int@1:1-1:4 1 u8))");
    IrLocal *arr = ir_local_new(&st, 1, "arr", a3, sp, false);
    IrExpr *larr = ir_expr_local(&st, sp, arr), *i0 = ir_expr_int(&st, sp, u32, 0), *i2 = ir_expr_int(&st, sp, u32, 2);
    expect(capture(dump_e, ir_expr_index(&st, sp, u8, larr, i0)),
           "(index@1:1-1:4 u8\n  (local@1:1-1:4 arr #1 [3]u8)\n  (int@1:1-1:4 0 u32))");
    expect(capture(dump_e, ir_expr_slice(&st, sp, s8, larr, i0, i2)),
           "(slice@1:1-1:4 []u8\n  (local@1:1-1:4 arr #1 [3]u8)\n  (int@1:1-1:4 0 u32)\n  (int@1:1-1:4 2 u32))");
    expect(capture(dump_e, ir_expr_convert(&st, sp, u8, la)), "(convert@1:1-1:4 u8\n  (local@3:12-3:13 a #0 u32))");

    ir_state_destroy(&st);
    pass("dump: every expression kind, every operator, integer edges (i64::MIN, u64::MAX, i8 -128)");
}

static IrFunc *build_add(IrState *st) {
    /* M2_PLAN "IR shape", the documented example, byte for byte */
    const IrType *u32 = ir_type_int(st, 32, false);
    IrLocal *a = ir_local_new(st, 0, "a", u32, mk(2, 8, 2, 17), true);
    IrLocal *b = ir_local_new(st, 1, "b", u32, mk(2, 19, 2, 28), true);
    IrExpr *sum = ir_expr_binary(st, mk(3, 12, 3, 17), u32, BINOP_ADD, ir_expr_local(st, mk(3, 12, 3, 13), a),
                                 ir_expr_local(st, mk(3, 16, 3, 17), b));
    IrStmt *ret = ir_stmt_return(st, mk(3, 5, 3, 14), sum);
    IrStmt *body = ir_stmt_block(st, mk(2, 31, 5, 1), list1(st, ret));
    return ir_func_new(st, mk(2, 1, 5, 1), "add", false, list2(st, a, b), 2, u32, body);
}

static const char *const ADD_DUMP =
    "(fn@2:1-5:1 fn:add\n"
    "  (params (param@2:8-2:17 a #0 u32) (param@2:19-2:28 b #1 u32))\n"
    "  (returns u32)\n"
    "  (block@2:31-5:1\n"
    "    (return@3:5-3:14\n"
    "      (binary@3:12-3:17 + u32\n"
    "        (local@3:12-3:13 a #0 u32)\n"
    "        (local@3:16-3:17 b #1 u32)))))";

static void test_dump_functions_and_statements(void) {
    static IrState st;
    ir_state_init(&st, 1 << 20);
    expect(capture(dump_f, build_add(&st)), ADD_DUMP);

    /* an empty unit function, and a kernel_hook one */
    NodeList none;
    node_list_init(&none);
    IrStmt *eb = ir_stmt_block(&st, mk(2, 8, 2, 11), none);
    IrFunc *f = ir_func_new(&st, mk(2, 1, 2, 11), "a", false, none, 0, ir_type_unit(&st), eb);
    expect(capture(dump_f, f), "(fn@2:1-2:11 fn:a\n  (params)\n  (returns)\n  (block@2:8-2:11))");
    f->kernel_hook = true;
    expect(capture(dump_f, f), "(fn@2:1-2:11 fn:a kernel_hook\n  (params)\n  (returns)\n  (block@2:8-2:11))");

    /* every statement kind */
    const IrType *u32 = ir_type_int(&st, 32, false), *bl = ir_type_bool(&st);
    Span sp = mk(4, 1, 4, 9);
    IrLocal *x = ir_local_new(&st, 0, "x", u32, sp, false);
    IrExpr *lx = ir_expr_local(&st, sp, x), *one = ir_expr_int(&st, sp, u32, 1), *t = ir_expr_bool(&st, sp, true);
    expect(capture(dump_s, ir_stmt_let(&st, sp, x, one)), "(let@4:1-4:9 x #0 u32\n  (int@4:1-4:9 1 u32))");
    expect(capture(dump_s, ir_stmt_assign(&st, sp, lx, one)),
           "(assign@4:1-4:9\n  (local@4:1-4:9 x #0 u32)\n  (int@4:1-4:9 1 u32))");
    expect(capture(dump_s, ir_stmt_break(&st, sp)), "(break@4:1-4:9)");
    expect(capture(dump_s, ir_stmt_continue(&st, sp)), "(continue@4:1-4:9)");
    expect(capture(dump_s, ir_stmt_return(&st, sp, NULL)), "(return@4:1-4:9)");
    expect(capture(dump_s, ir_stmt_return(&st, sp, one)), "(return@4:1-4:9\n  (int@4:1-4:9 1 u32))");
    expect(capture(dump_s, ir_stmt_expr(&st, sp, one)), "(expr_stmt@4:1-4:9\n  (int@4:1-4:9 1 u32))");
    IrStmt *blk = ir_stmt_block(&st, sp, none);
    expect(capture(dump_s, ir_stmt_while(&st, sp, t, blk)),
           "(while@4:1-4:9\n  (bool@4:1-4:9 true bool)\n  (block@4:1-4:9))");
    expect(capture(dump_s, ir_stmt_if(&st, sp, t, blk, NULL)),
           "(if@4:1-4:9\n  (bool@4:1-4:9 true bool)\n  (block@4:1-4:9))");
    IrStmt *inner = ir_stmt_if(&st, sp, t, blk, ir_stmt_block(&st, sp, none));
    expect(capture(dump_s, ir_stmt_if(&st, sp, t, blk, inner)),
           "(if@4:1-4:9\n  (bool@4:1-4:9 true bool)\n  (block@4:1-4:9)\n"
           "  (if@4:1-4:9\n    (bool@4:1-4:9 true bool)\n    (block@4:1-4:9)\n    (block@4:1-4:9)))");
    IrExpr *cond = ir_expr_binary(&st, sp, bl, BINOP_LT, lx, one);
    IrStmt *step = ir_stmt_assign(&st, sp, lx, ir_expr_binary(&st, sp, u32, BINOP_ADD, lx, one));
    expect(capture(dump_s, ir_stmt_for(&st, sp, ir_stmt_let(&st, sp, x, ir_expr_int(&st, sp, u32, 0)), cond, step, blk)),
           "(for@4:1-4:9\n"
           "  (let@4:1-4:9 x #0 u32\n    (int@4:1-4:9 0 u32))\n"
           "  (binary@4:1-4:9 < bool\n    (local@4:1-4:9 x #0 u32)\n    (int@4:1-4:9 1 u32))\n"
           "  (assign@4:1-4:9\n    (local@4:1-4:9 x #0 u32)\n    (binary@4:1-4:9 + u32\n      (local@4:1-4:9 x #0 u32)\n      (int@4:1-4:9 1 u32)))\n"
           "  (block@4:1-4:9))");
    ir_state_destroy(&st);
    pass("dump: functions (params, returns, kernel_hook) and every statement kind");
}

static void test_dump_module_and_determinism(void) {
    char *dumps[2];
    for (int round = 0; round < 2; round++) {
        static IrState st[2];
        ir_state_init(&st[round], 1 << 20);
        void *pad = NULL;
        if (round == 1) {
            (void)arena_alloc(st[round].arena, 4096 + 3, 1); /* a different arena layout */
            pad = malloc(1234);                              /* and a shifted heap; freed below */
        }
        IrModule m;
        node_list_init(&m.structs);
        node_list_init(&m.funcs);
        IrStruct *s = arena_alloc(st[round].arena, sizeof(IrStruct), 8);
        memset(s, 0, sizeof *s);
        s->name = "Empty"; s->span = mk(1, 1, 1, 15); s->align = 1;
        node_list_push(st[round].arena, &m.structs, s);
        node_list_push(st[round].arena, &m.funcs, build_add(&st[round]));
        dumps[round] = capture(dump_m, &m);
        ir_state_destroy(&st[round]);
        free(pad);
    }
    assert(strcmp(dumps[0], dumps[1]) == 0);
    /* structs first, then functions; one form per line, each ending in a newline */
    assert(strncmp(dumps[0], "(struct@1:1-1:15 Empty size=0 align=1)\n(fn@2:1-5:1 fn:add\n", 58) == 0);
    size_t n = strlen(dumps[0]);
    assert(n > 0 && dumps[0][n - 1] == '\n');
    free(dumps[0]);
    free(dumps[1]);

    IrModule none;
    node_list_init(&none.structs);
    node_list_init(&none.funcs);
    expect(capture(dump_m, &none), ""); /* an empty module prints nothing */
    pass("dump: module order (structs, then functions), empty module, identical across arena bases");
}

/* ---- the parser's height bound, as the IR will assert it ---- */

static void test_ast_height_assertion(void) {
    Arena *a = arena_create(1 << 20);
    Span sp = mk(1, 1, 1, 2);
    Expr *e = expr_number(a, sp, 1);
    for (int i = 0; i < MAX_EXPR_HEIGHT - 1; i++) {
        e = expr_binary(a, sp, BINOP_ADD, e, expr_number(a, sp, 1));
    }
    assert(e->height == MAX_EXPR_HEIGHT && ir_ast_height_ok(e));
    e = expr_binary(a, sp, BINOP_ADD, e, expr_number(a, sp, 1));
    assert(e->height == MAX_EXPR_HEIGHT + 1 && !ir_ast_height_ok(e));
    arena_destroy(a);
    pass("ir_ast_height_ok: 512 accepted, 513 rejected");
}

/* ---- stack: the dumper on maximal trees ---- */

typedef enum { SH_BINARY, SH_MIXED, SH_FIELD, SH_INDEX, SH_POSTFIX_MIX, SH_IN_CALLS, SH_IN_IFS, SH_COUNT } Shape;
static const char *const shape_names[SH_COUNT] = {"binary", "mixed_prec", "field", "index", "postfix_mix", "in_calls", "in_ifs"};

/* Builds a tree of expression height exactly MAX_EXPR_HEIGHT (leaf = 1; asserted by
 * construction, counted in `h`) in the given shape; `in_ifs` wraps the statement in
 * 120 nested ifs (the parser allows 127 levels of nested if) around it. Hand-built
 * IR: the dumper never type-checks, so types are only as precise as it prints. */
static IrExpr *build_chain(IrState *st, Shape sh, int *h) {
    const IrType *u32 = ir_type_int(st, 32, false);
    Span sp = mk(1, 1, 1, 2);
    static IrStruct rec; /* struct S { f: S }: lets `field` chain without a type error to hide */
    static IrField rec_f;
    rec_f.name = "f"; rec_f.type = NULL;
    rec.name = "S"; rec.fields = &rec_f; rec.n_fields = 1; rec.size = 0; rec.align = 1;
    const IrType *ts = ir_type_struct(st, &rec);
    rec_f.type = ts;
    IrLocal *l = ir_local_new(st, 0, "s", ts, sp, false);
    IrExpr *leaf = ir_expr_int(st, sp, u32, 1);
    IrExpr *e;
    *h = 1;
    switch (sh) {
        case SH_BINARY:
        case SH_IN_IFS:
            e = leaf;
            while (*h < MAX_EXPR_HEIGHT) { e = ir_expr_binary(st, sp, u32, BINOP_ADD, e, leaf); (*h)++; }
            return e;
        case SH_MIXED:
            e = leaf;
            *h = 1;
            while (*h < MAX_EXPR_HEIGHT) {
                IrExpr *rhs = ir_expr_binary(st, sp, u32, BINOP_MUL, leaf, leaf); /* height 2 */
                e = ir_expr_binary(st, sp, u32, BINOP_ADD, e, rhs);
                *h = *h < 2 ? 3 : *h + 1;
            }
            return e;
        case SH_FIELD:
            e = ir_expr_local(st, sp, l);
            while (*h < MAX_EXPR_HEIGHT) { e = ir_expr_field(st, sp, ts, e, 0); (*h)++; }
            return e;
        case SH_INDEX:
            e = ir_expr_local(st, sp, l);
            while (*h < MAX_EXPR_HEIGHT) { e = ir_expr_index(st, sp, u32, e, leaf); (*h)++; }
            return e;
        case SH_POSTFIX_MIX:
            e = ir_expr_local(st, sp, l);
            while (*h < MAX_EXPR_HEIGHT) {
                e = (*h % 2) ? ir_expr_field(st, sp, ts, e, 0) : ir_expr_index(st, sp, ts, e, leaf);
                (*h)++;
            }
            return e;
        case SH_IN_CALLS: {
            NodeList no_locals;
            node_list_init(&no_locals);
            IrFunc *f = ir_func_new(st, sp, "f", false, no_locals, 0, u32, NULL);
            e = leaf;
            while (*h < MAX_EXPR_HEIGHT - 200) { e = ir_expr_binary(st, sp, u32, BINOP_ADD, e, leaf); (*h)++; }
            for (int i = 0; i < 200; i++) { e = ir_expr_call(st, sp, u32, f, list1(st, e)); (*h)++; }
            return e;
        }
        default:
            assert(!"bad shape");
            return NULL;
    }
}

static void run_shape(Shape sh) {
    static IrState st;
    ir_state_init(&st, 16 << 20);
    int h;
    IrExpr *e = build_chain(&st, sh, &h);
    assert(h == MAX_EXPR_HEIGHT);
    IrStmt *s = ir_stmt_expr(&st, mk(1, 1, 1, 2), e);
    if (sh == SH_IN_IFS) {
        IrExpr *cond = ir_expr_bool(&st, mk(1, 1, 1, 2), true);
        for (int i = 0; i < 120; i++) {
            IrStmt *blk = ir_stmt_block(&st, mk(1, 1, 1, 2), list1(&st, s));
            s = ir_stmt_if(&st, mk(1, 1, 1, 2), cond, blk, NULL);
        }
    }
    FILE *f = tmpfile();
    assert(f);
    ir_dump_stmt(f, s, 0);
    long n = ftell(f);
    assert(n > 0);
    rewind(f);
    long open = 0, close = 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        open += (c == '(');
        close += (c == ')');
    }
    fclose(f);
    assert(open == close && open > MAX_EXPR_HEIGHT);
    ir_state_destroy(&st);
}

static void test_stack_shapes(void) {
    for (int sh = 0; sh < SH_COUNT; sh++) {
        run_shape((Shape)sh);
        printf("PASS: stack: %s, height %d\n", shape_names[sh], MAX_EXPR_HEIGHT);
    }
}

/* ---- build: the gate, the empty-function skeleton, IR_UNIMPLEMENTED ---- */

/* Parses `src` into the static parser state below (fatal parse errors included:
 * they leave diagnostics and no usable items, exactly what the gate must see).
 * Static: modified after setjmp, read after the longjmp. */
static ParserState g_ps;
static NodeList g_items;

static void parse_src(const char *src) {
    parser_state_init(&g_ps, 16 * 1024 * 1024);
    node_list_init(&g_items);
    if (setjmp(g_ps.fatal_target) == 0) {
        Lexer lx;
        lexer_init(&lx, src, strlen(src));
        (void)parser_parse_file(&g_ps, &lx, &g_items);
    }
}

static void parse_done(void) {
    parser_state_destroy(&g_ps);
}

static char *dump_module_of(const IrModule *m) {
    return capture(dump_m, m);
}

static void test_gate(void) {
    static IrState st;
    ir_state_init(&st, 1 << 20);

    /* the file TYPED_IR.md names: E0109 leaves a tree byte-identical to a clean one */
    char *long_chain = malloc(64 * 1024);
    assert(long_chain);
    char *w = long_chain + sprintf(long_chain, "manna v0;\nfn f() { let x = 1");
    for (int i = 0; i < MAX_EXPR_HEIGHT; i++) w += sprintf(w, "+1"); /* height 513: E0111, fatal */
    strcpy(w, "; }\n");
    const char *cases[] = {
        "manna v0;\nfn f() -> unit { }\n",              /* E0109, tree intact */
        "manna v0;\nfn g() { let = 2; }\nfn h() { }\n", /* E0105, a statement dropped with no placeholder */
        "manna v1;\nfn f() { }\n",                      /* E0106, no items */
        "fn f() { }\n",                                 /* E0107, no items */
        long_chain,                                     /* E0111, fatal: no usable tree */
    };
    const char *codes[] = {"E0109", "E0105", "E0106", "E0107", "E0111"};
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        parse_src(cases[i]);
        assert(g_ps.diags.len >= 1 && strcmp(g_ps.diags.items[0].code, codes[i]) == 0);
        size_t used = arena_used(st.arena);
        IrModule *m = (IrModule *)0x1;
        assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_SKIPPED);
        assert(m == NULL);                       /* nothing downstream can be handed a module */
        assert(st.diags.len == 0);               /* the gate adds no diagnostics ... */
        assert(arena_used(st.arena) == used);    /* ... and touches nothing */
        assert(st.unimplemented == NULL);
        parse_done();
    }
    free(long_chain);

    /* and a clean file passes the gate */
    parse_src("manna v0;\nfn f() { }\n");
    assert(g_ps.diags.len == 0);
    IrModule *m = NULL;
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_BUILT && m != NULL);
    parse_done();
    ir_state_destroy(&st);
    pass("gate: five parse-diagnostic files (E0109, E0105, E0106, E0107, E0111) are refused untouched");
}

static void test_build_empty_functions(void) {
    static IrState st;
    ir_state_init(&st, 1 << 20);

    parse_src("manna v0;\n");
    IrModule *m = NULL;
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_BUILT);
    assert(m->structs.len == 0 && m->funcs.len == 0);
    expect(dump_module_of(m), "");
    parse_done();

    parse_src("manna v0;\nfn first() { }\nfn second() {\n}\n");
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_BUILT); /* reuse of a built state */
    assert(m->funcs.len == 2);
    const IrFunc *f = m->funcs.items[0];
    assert(strcmp(f->name, "first") == 0 && strcmp(f->id, "fn:first") == 0);
    assert(!f->kernel_hook && f->n_params == 0 && f->locals.len == 0);
    assert(f->ret == ir_type_unit(&st));                           /* absent '-> type' is unit */
    assert(f->body->kind == IR_S_BLOCK && f->body->as.block.stmts.len == 0);
    assert(f->span.start_line == 2 && f->span.end_col == 15);       /* spans come from the AST */
    assert(strcmp(((const IrFunc *)m->funcs.items[1])->id, "fn:second") == 0);
    expect(dump_module_of(m),
           "(fn@2:1-2:15 fn:first\n  (params)\n  (returns)\n  (block@2:12-2:15))\n"
           "(fn@3:1-4:2 fn:second\n  (params)\n  (returns)\n  (block@3:13-4:2))\n");
    parse_done();
    ir_state_destroy(&st);
    pass("build: empty module and empty functions (unit return, AST spans, source order)");
}

/* Fix 2 (review): IR_UNIMPLEMENTED is a status, never a diagnostic and never a dump. */
static void test_unimplemented(void) {
    static IrState st;
    ir_state_init(&st, 1 << 20);
    static const struct { const char *src, *what; } cases[] = {
        {"manna v0;\nstruct P { x: u32, }\n", "struct declarations"},
        {"manna v0;\n[kernel_hook] fn f() { }\n", "attributes"},
        {"manna v0;\nfn f(a: u8) { }\n", "parameters"},
        {"manna v0;\nfn f() -> u8 { }\n", "return types"},
        {"manna v0;\nfn f() { return; }\n", "statements"},
        {"manna v0;\nfn ok() { }\nfn f() { return; }\n", "statements"}, /* after a buildable function */
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        parse_src(cases[i].src);
        assert(g_ps.diags.len == 0);
        IrModule *m = (IrModule *)0x1;
        assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_UNIMPLEMENTED);
        assert(m == NULL && st.diags.len == 0);
        assert(st.unimplemented && strstr(st.unimplemented, cases[i].what));
        parse_done();
    }
    ir_state_destroy(&st);
    pass("IR_UNIMPLEMENTED: struct / attribute / parameter / return type / statement, no diagnostic, no module");
}

static void test_build_e0201_and_reuse(void) {
    enum { N = 5000 };
    char *src = malloc((size_t)N * 20 + 32);
    assert(src);
    char *w = src + sprintf(src, "manna v0;\n");
    for (int i = 0; i < N; i++) w += sprintf(w, "fn f%d() { }\n", i);

    static IrState st;
    ir_state_init(&st, 64 << 10); /* 64 KiB: far fewer than 5000 functions' worth */
    parse_src(src);
    assert(g_ps.diags.len == 0 && g_items.len == N);
    IrModule *m = (IrModule *)0x1;
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS);
    assert(m == NULL && st.diags.len == 1);
    const Diagnostic *d = &st.diags.items[0];
    assert(strcmp(d->code, "E0201") == 0);
    assert(d->span.start_col == 1 && d->span.start_line > 2 && d->span.start_line <= N + 1);
    /* Pinned (like the parser's G5): the line of the function being built when the cap is hit. */
    assert(d->span.start_line == 271); /* ~152 bytes a function (block 56, func 88, id) + list growth vs 48 KiB */

    /* The state stays exhausted (the arena is not reset): a second build reports E0201
     * again - the hook is re-armed, so reuse is never silent (the parser's G4 lesson). */
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS);
    assert(m == NULL && st.diags.len == 2 && strcmp(st.diags.items[1].code, "E0201") == 0);
    parse_done();
    ir_state_destroy(&st);
    free(src);
    pass("build: arena exhaustion gives E0201 at the declaration being built; reuse reports it again");
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0); /* a hang or abort must not swallow the PASS lines before it */
    if (argc == 2 && strcmp(argv[1], "--stack") == 0) { /* the Makefile's 2 MB (4x) gate */
        test_stack_shapes();
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--shape") == 0) { /* tools/measure_ir_stack.sh */
        for (int sh = 0; sh < SH_COUNT; sh++) {
            if (strcmp(argv[2], shape_names[sh]) == 0) { run_shape((Shape)sh); return 0; }
        }
        fprintf(stderr, "unknown shape %s\n", argv[2]);
        return 2;
    }
    test_primitives();
    test_composites();
    test_table_growth();
    test_table_growth_mixed_kinds();
    test_e0203();
    test_e0201_and_reuse();
    test_reset_primitives();
    test_dump_expressions();
    test_dump_functions_and_statements();
    test_dump_module_and_determinism();
    test_ast_height_assertion();
    test_gate();
    test_build_empty_functions();
    test_unimplemented();
    test_build_e0201_and_reuse();
    test_stack_shapes();
    return 0;
}
