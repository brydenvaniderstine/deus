/* POSIX feature macro first: tmpfile/ftell are C11, but keep the strict-header
 * lesson of the parser's G6 (glibc -std=c11 hides POSIX declarations). */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ir.h"
#include "ir_internal.h"
#include "lexer.h"
#include "parser.h"
#include "parser_state.h"

/* Unit tests for the typed IR (docs/history/TYPED_IR.md step 3): types and interning,
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
    expect(capture(dump_e, ir_expr_len(&st, sp, ir_type_int(&st, 64, false), larr)), "(len@1:1-1:4 u64\n  (local@1:1-1:4 arr #1 [3]u8))");

    ir_state_destroy(&st);
    pass("dump: every expression kind, every operator, integer edges (i64::MIN, u64::MAX, i8 -128)");
}

static IrFunc *build_add(IrState *st) {
    /* M2_PLAN "IR shape", the documented example, byte for byte. Its spans are what the
     * parser really produces for
     *   fn add(a: u32, b: u32) -> u32 {\n    return a + b;\n}
     * on line 2 (checked against parser_dump and ir_dump); until step 7 builds statements the
     * body cannot be produced by ir_build, so it is built by hand here. */
    const IrType *u32 = ir_type_int(st, 32, false);
    IrLocal *a = ir_local_new(st, 0, "a", u32, mk(2, 8, 2, 14), true);
    IrLocal *b = ir_local_new(st, 1, "b", u32, mk(2, 16, 2, 22), true);
    IrExpr *sum = ir_expr_binary(st, mk(3, 12, 3, 17), u32, BINOP_ADD, ir_expr_local(st, mk(3, 12, 3, 13), a),
                                 ir_expr_local(st, mk(3, 16, 3, 17), b));
    IrStmt *ret = ir_stmt_return(st, mk(3, 5, 3, 18), sum);
    IrStmt *body = ir_stmt_block(st, mk(2, 31, 4, 2), list1(st, ret));
    return ir_func_new(st, mk(2, 1, 4, 2), "add", false, list2(st, a, b), 2, u32, body);
}

static const char *const ADD_DUMP =
    "(fn@2:1-4:2 fn:add\n"
    "  (params (param@2:8-2:14 a #0 u32) (param@2:16-2:22 b #1 u32))\n"
    "  (returns u32)\n"
    "  (block@2:31-4:2\n"
    "    (return@3:5-3:18\n"
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
    assert(strncmp(dumps[0], "(struct@1:1-1:15 Empty size=0 align=1)\n(fn@2:1-4:2 fn:add\n", 58) == 0);
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

/* Parses on a thread with a 256 MB stack, then returns: the parser's frames are gone before the caller builds.
 * The builder-stack harness uses it so the stack the builder is measured on is the builder's own, not the
 * parser's (parsing 254 nested calls needs 4-8 MB under ASan by itself: measured, reported in
 * docs/history/TYPED_IR.md step 10). Everything the parse produces lives in statics and the parser's arena. */
static const char *g_big_src;
static void *parse_thread(void *unused) {
    (void)unused;
    parse_src(g_big_src);
    return NULL;
}
static void parse_src_big_stack(const char *src) {
    pthread_attr_t attr;
    pthread_t th;
    g_big_src = src;
    assert(pthread_attr_init(&attr) == 0);
    assert(pthread_attr_setstacksize(&attr, (size_t)256 << 20) == 0);
    assert(pthread_create(&th, &attr, parse_thread, NULL) == 0);
    assert(pthread_join(th, NULL) == 0);
    pthread_attr_destroy(&attr);
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

/* Fix 2 (review): IR_UNIMPLEMENTED is a status, never a diagnostic and never a dump. Since step 13 no
 * construct the parser can build reaches it: what remains are two states the parser never produces, so the
 * trees are made by moving a node (a named argument out of its call) or by hand. */
/* The one remaining IR_UNIMPLEMENTED raiser (ir_expr.c unimplemented_kind): an ExprKind no case handles. The parser
 * cannot produce one, so the state is built by overwriting a node's kind. (A named argument outside a call and a
 * bare block statement are asserts now: the parser never builds them, parser.c:632 and parser.c:1755,1765.) */
static void corrupt_let_init_kind(const Decl *d) {
    Stmt *let = d->as.function_decl.body->as.block.stmts.items[0];
    ((Expr *)let->as.let_stmt.init)->kind = (ExprKind)99;
}

static void test_unimplemented(void) {
    static IrState st;
    ir_state_init(&st, 1 << 20);
    parse_src("manna v0;\nfn f() { let x = 1; }\n");
    assert(g_ps.diags.len == 0);
    corrupt_let_init_kind(g_items.items[0]);
    IrModule *m = (IrModule *)0x1;
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_UNIMPLEMENTED);
    assert(m == NULL && st.diags.len == 0);
    assert(st.unimplemented && strcmp(st.unimplemented, "this expression") == 0);
    parse_done();

    /* Errors found before the unwind win over IR_UNIMPLEMENTED, and the state is reusable. */
    parse_src("manna v0;\nfn a() -> i8 { return 128; }\nfn b() { let x = 1; }\n");
    assert(g_ps.diags.len == 0);
    corrupt_let_init_kind(g_items.items[1]);
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS && m == NULL);
    assert(st.diags.len == 1 && strcmp(st.diags.items[0].code, "E0224") == 0);
    parse_done();
    parse_src("manna v0;\nfn a() -> i8 { return 127; }\n");
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_BUILT && m != NULL && st.unimplemented == NULL);
    parse_done();
    ir_state_destroy(&st);
    pass("IR_UNIMPLEMENTED: only an ExprKind no case handles (a state the parser never produces) reaches it, no diagnostic, no module; real errors win; state reusable");
}

/* Audit finding 2: the target of a non-assignable assignment is checked muted (call edges and name uses, no diagnostics). The mute
 * is lowered on the way out, and an unwind out of the muted subtree (here: the arena runs out inside it, E0201) leaves it at 0. */
static void test_muted_target(void) {
    enum { N_ARGS = 8000 };
    size_t cap = 64 + N_ARGS * 3;
    char *src = malloc(cap);
    assert(src);
    size_t n = (size_t)snprintf(src, cap, "manna v0;\nfn g(a: u8) {\n}\nfn f(a: u8) {\n    g(a");
    for (int i = 1; i < N_ARGS; i++) n += (size_t)snprintf(src + n, cap - n, ", a");
    snprintf(src + n, cap - n, ") = 1;\n}\n");
    parse_src(src);
    assert(g_ps.diags.len == 0);
    static IrState st;
    ir_state_init(&st, 64 << 10);
    IrModule *m = (IrModule *)0x1;
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS && m == NULL);
    assert(st.diags.len == 2 && strcmp(st.diags.items[0].code, "E0270") == 0 && strcmp(st.diags.items[1].code, "E0201") == 0);
    assert(st.muted == 0);
    ir_state_destroy(&st);
    parse_done();
    free(src);
    /* Without the unwind: a bad target with an error inside it reports the E0270 only, and the next statement's error is not lost. */
    parse_src("manna v0;\nfn f() {\n    nope(1) = 1;\n    let x: u8 = true;\n}\n");
    assert(g_ps.diags.len == 0);
    ir_state_init(&st, 1 << 20);
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS);
    assert(st.diags.len == 2 && strcmp(st.diags.items[0].code, "E0270") == 0 && strcmp(st.diags.items[1].code, "E0223") == 0 && st.muted == 0);
    ir_state_destroy(&st);
    parse_done();
    pass("muted target: no diagnostic from inside it, the mute is lowered on every path (also after an E0201 unwind), later errors still print");
}

/* `fn f() -> T { return <expr>; }` for one case of the range matrix. Returns the build status;
 * the diagnostics are left in st. */
static IrStatus build_return(IrState *st, const char *type, const char *expr, IrModule **m) {
    char src[256];
    snprintf(src, sizeof src, "manna v0;\nfn f() -> %s { return %s; }\n", type, expr);
    parse_src(src);
    assert(g_ps.diags.len == 0);
    IrStatus r = ir_build(st, &g_ps.diags, &g_items, m);
    parse_done();
    return r;
}

/* The literal range matrix, generated: every width and sign, the three values around each
 * boundary, each written bare, directly negated, parenthesized, and negated-parenthesized.
 * The expected outcome is computed here from the rules (not from the checker). */
static void test_literal_matrix(void) {
    static IrState st;
    ir_state_init(&st, 16 << 20);
    static const struct { const char *name; int bits; bool sg; } types[] = {
        {"u8", 8, false}, {"u16", 16, false}, {"u32", 32, false}, {"u64", 64, false},
        {"i8", 8, true}, {"i16", 16, true}, {"i32", 32, true}, {"i64", 64, true},
    };
    int cases = 0;
    for (size_t t = 0; t < 8; t++) {
        int bits = types[t].bits;
        bool sg = types[t].sg;
        uint64_t half = UINT64_C(1) << (bits - 1);
        uint64_t umax = bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
        uint64_t max = sg ? half - 1 : umax;
        /* values around the boundary of this type; skip those the lexer cannot write (>= 2^64) */
        uint64_t vals[] = {0, 1, max - 1, max, max + 1, half, half + 1};
        for (size_t k = 0; k < sizeof vals / sizeof vals[0]; k++) {
            uint64_t v = vals[k];
            if (k >= 4 && v < max) continue; /* wrapped past 2^64 (u64 max + 1) */
            for (int form = 0; form < 4; form++) {
                char expr[64];
                const char *code = NULL; /* expected first diagnostic code, NULL = builds */
                switch (form) {
                    case 0: snprintf(expr, sizeof expr, "%llu", (unsigned long long)v);
                            if (v > max) code = "E0224";
                            break;
                    case 1: snprintf(expr, sizeof expr, "-%llu", (unsigned long long)v);
                            if (!sg) code = "E0260"; else if (v > half) code = "E0224";
                            break;
                    case 2: snprintf(expr, sizeof expr, "(%llu)", (unsigned long long)v);
                            if (v > max) code = "E0224";
                            break;
                    default: snprintf(expr, sizeof expr, "-(%llu)", (unsigned long long)v);
                            if (!sg) code = "E0260"; else if (v > max) code = "E0224";
                            break;
                }
                IrModule *m = NULL;
                size_t before = st.diags.len;
                IrStatus r = build_return(&st, types[t].name, expr, &m);
                if (code == NULL) {
                    assert(r == IR_BUILT && st.diags.len == before);
                } else {
                    if (r != IR_ERRORS || st.diags.len != before + 1 || strcmp(st.diags.items[before].code, code) != 0) {
                        fprintf(stderr, "matrix: %s %s expected %s\n", types[t].name, expr, code);
                        abort();
                    }
                }
                cases++;
            }
        }
    }
    ir_state_destroy(&st);
    printf("PASS: literal matrix: %d generated cases (8 types x boundary values x 4 forms)\n", cases);
}

/* E0223 across expected types and expression kinds (bool or integer literal against bool,
 * integers, arrays, structs), with the exact wording and the once-per-group rule. */
static void test_type_mismatch(void) {
    static IrState st;
    ir_state_init(&st, 1 << 20);
    static const struct { const char *prelude, *ret, *expr, *msg; } cases[] = {
        {"", "bool", "true", NULL},
        {"", "bool", "false", NULL},
        {"", "u8", "true", "type mismatch: expected u8, found bool"},
        {"", "i64", "false", "type mismatch: expected i64, found bool"},
        {"", "bool", "1", "type mismatch: expected bool, found integer literal"},
        {"", "bool", "-1", "type mismatch: expected bool, found integer literal"},
        {"", "bool", "1 + 2 * 3", "type mismatch: expected bool, found integer literal"},
        {"", "bool", "256 + 300", "type mismatch: expected bool, found integer literal"},   /* not two range errors */
        {"", "[3]u8", "1", "type mismatch: expected [3]u8, found integer literal"},
        {"", "[3]u8", "true", "type mismatch: expected [3]u8, found bool"},
        {"struct P { x: u8, }\n", "P", "1", "type mismatch: expected P, found integer literal"},
        {"struct P { x: u8, }\n", "P", "true", "type mismatch: expected P, found bool"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char src[256];
        snprintf(src, sizeof src, "manna v0;\n%sfn f() -> %s { return %s; }\n", cases[i].prelude, cases[i].ret, cases[i].expr);
        parse_src(src);
        assert(g_ps.diags.len == 0);
        IrModule *m = NULL;
        size_t before = st.diags.len;
        IrStatus r = ir_build(&st, &g_ps.diags, &g_items, &m);
        if (!cases[i].msg) {
            assert(r == IR_BUILT && st.diags.len == before);
        } else {
            assert(r == IR_ERRORS && st.diags.len == before + 1);
            assert(strcmp(st.diags.items[before].code, "E0223") == 0);
            assert(strcmp(st.diags.items[before].message, cases[i].msg) == 0);
            assert(st.diags.items[before].fix == NULL);
        }
        parse_done();
    }
    ir_state_destroy(&st);
    pass("E0223: bool/integer literal against bool, integers, arrays, structs; one error per group, no range errors under it");
}

/* A literal group is built as written (no constant folding): `1 + 2 * 3` is a binary tree
 * typed u8 at every node, each literal typed u8. */
static void test_group_representation(void) {
    static IrState st;
    ir_state_init(&st, 1 << 20);
    IrModule *m = NULL;
    assert(build_return(&st, "u8", "1 + 2 * 3", &m) == IR_BUILT);
    const IrExpr *e = ((const IrStmt *)((const IrFunc *)m->funcs.items[0])->body->as.block.stmts.items[0])->as.ret.value;
    const IrType *u8 = ir_type_int(&st, 8, false);
    assert(e->kind == IR_E_BINARY && e->as.binary.op == BINOP_ADD && e->type == u8);
    assert(e->as.binary.lhs->kind == IR_E_INT && e->as.binary.lhs->as.integer.bits == 1 && e->as.binary.lhs->type == u8);
    const IrExpr *r = e->as.binary.rhs;
    assert(r->kind == IR_E_BINARY && r->as.binary.op == BINOP_MUL && r->type == u8);
    assert(r->as.binary.lhs->as.integer.bits == 2 && r->as.binary.rhs->as.integer.bits == 3);
    /* operators of every kind in a group: all ten build */
    static const char *ops[] = {"+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>"};
    for (size_t i = 0; i < 10; i++) {
        char expr[32];
        snprintf(expr, sizeof expr, "6 %s 2", ops[i]);
        assert(build_return(&st, "u8", expr, &m) == IR_BUILT);
    }
    ir_state_destroy(&st);
    pass("literal groups: built as written, every node typed by the expected type, all ten operators");
}

/* The folded i64::MIN: one int node, bits sign-extended, spanning the minus; -127 is not folded. */
static void test_min_representation(void) {
    static IrState st;
    ir_state_init(&st, 1 << 20);
    IrModule *m = NULL;
    assert(build_return(&st, "i8", "-128", &m) == IR_BUILT);
    const IrStmt *ret = ((const IrFunc *)m->funcs.items[0])->body->as.block.stmts.items[0];
    const IrExpr *v = ret->as.ret.value;
    assert(v->kind == IR_E_INT && v->as.integer.bits == UINT64_C(0xFFFFFFFFFFFFFF80) && v->type == ir_type_int(&st, 8, true));
    assert(build_return(&st, "i64", "-9223372036854775808", &m) == IR_BUILT);
    v = ((const IrStmt *)((const IrFunc *)m->funcs.items[0])->body->as.block.stmts.items[0])->as.ret.value;
    assert(v->kind == IR_E_INT && v->as.integer.bits == UINT64_C(0x8000000000000000));
    assert(build_return(&st, "i8", "-127", &m) == IR_BUILT);
    v = ((const IrStmt *)((const IrFunc *)m->funcs.items[0])->body->as.block.stmts.items[0])->as.ret.value;
    assert(v->kind == IR_E_UNARY && v->as.unary.operand->kind == IR_E_INT && v->as.unary.operand->as.integer.bits == 127);
    ir_state_destroy(&st);
    pass("i64::MIN folds to one int node (bits sign-extended); -127 stays a unary node");
}

static void test_build_e0201_and_reuse(void) {
    enum { N = 20000 };
    char *src = malloc((size_t)N * 24 + 32);
    assert(src);
    char *w = src + sprintf(src, "manna v0;\n");
    for (int i = 0; i < N; i++) w += sprintf(w, "fn f%d() { }\n", i);

    static IrState st;
    ir_state_init(&st, 4 << 20); /* 4 MiB: the name table (~1.3 MB) fits; 20000 functions' IR does not */
    parse_src(src);
    assert(g_ps.diags.len == 0 && g_items.len == N);
    IrModule *m = (IrModule *)0x1;
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS);
    assert(m == NULL && st.diags.len == 1);
    const Diagnostic *d = &st.diags.items[0];
    assert(strcmp(d->code, "E0201") == 0);
    assert(d->span.start_col == 1 && d->span.start_line > 2 && d->span.start_line <= N + 1);
    /* Pinned (like the parser's G5): the line of the function being built when the cap is hit. */
    assert(d->span.start_line == 13503); /* function #13501, in the SIGNATURE pass (one earlier again at step 14: Ctx grew by the rename tables, ~56 bytes; before that 13504, one earlier than before step 7: the body context' Ctx, ~104 bytes, is now allocated first): the 4 MiB cap less the 16 KiB reserve, the 2.6 MB name table and the 160 KB signature array leaves ~1.4 MB, and each signature (IrFunc 88 + "fn:fNNNN" + alignment) is ~103 bytes: 1.4 MB / 103 ~ 13500 */

    /* The state stays exhausted (the arena is not reset): a second build reports E0201
     * again - the hook is re-armed, so reuse is never silent (the parser's G4 lesson). */
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS);
    assert(m == NULL && st.diags.len == 2 && strcmp(st.diags.items[1].code, "E0201") == 0);
    parse_done();
    ir_state_destroy(&st);
    free(src);
    pass("build: arena exhaustion gives E0201 at the declaration being built; reuse reports it again");
}

/* Exhaustion while sizing the name table happens before any declaration is reached:
 * E0201 is then located at 1:1 (the reset position), not at some declaration. */
static void test_e0201_in_name_table(void) {
    enum { N = 5000 };
    char *src = malloc((size_t)N * 24 + 32);
    assert(src);
    char *w = src + sprintf(src, "manna v0;\n");
    for (int i = 0; i < N; i++) w += sprintf(w, "fn f%d() { }\n", i);
    static IrState st;
    ir_state_init(&st, 64 << 10); /* the table alone is 16384 slots * 40 bytes */
    parse_src(src);
    IrModule *m = NULL;
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS && m == NULL);
    assert(st.diags.len == 1 && strcmp(st.diags.items[0].code, "E0201") == 0);
    assert(st.diags.items[0].span.start_line == 1 && st.diags.items[0].span.start_col == 1);
    parse_done();
    ir_state_destroy(&st);
    free(src);
    pass("E0201 while sizing the name table is located at 1:1");
}

/* ---- top-level declarations (step 4) ---- */

static void test_decls_many_names(void) {
    /* 20000 distinct functions and 20000 repeats: the name table is sized for its keys
     * and must find every first declaration; nothing quadratic or order-dependent. */
    enum { N = 20000 };
    char *src = malloc((size_t)N * 2 * 24 + 32);
    assert(src);
    char *w = src + sprintf(src, "manna v0;\n");
    for (int i = 0; i < N; i++) w += sprintf(w, "fn f%d() { }\n", i);
    static IrState st;
    ir_state_init(&st, 16 << 20);
    parse_src(src);
    assert(g_ps.diags.len == 0 && g_items.len == N);
    IrModule *m = NULL;
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_BUILT && m->funcs.len == N);
    parse_done();

    /* the same N again: each repeat is E0210 naming the FIRST declaration's position */
    w += sprintf(w, "fn f%d() { }\n", 7); /* line N+2, repeats f7 declared at line 9 */
    parse_src(src);
    assert(g_ps.diags.len == 0);
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS && m == NULL);
    assert(st.diags.len == 1 && strcmp(st.diags.items[0].code, "E0210") == 0);
    assert(strcmp(st.diags.items[0].message, "'f7' is already declared at 9:4") == 0);
    assert(st.diags.items[0].span.start_line == N + 2 && st.diags.items[0].span.start_col == 4);
    parse_done();
    ir_state_destroy(&st);
    free(src);
    pass("decls: 20000 names resolved through the table; a repeat names the first declaration");
}

/* ---- function signatures (step 5) ---- */

static void test_signatures(void) {
    static IrState st;
    ir_state_init(&st, 1 << 20);
    parse_src("manna v0;\n[kernel_hook]\nfn f(a: u8, b: [2]u8, c: []u8) -> u32 { return 0; }\nfn g(d: []u8) { }\n");
    assert(g_ps.diags.len == 0);
    IrModule *m = NULL;
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_BUILT && m->funcs.len == 2);

    const IrFunc *f = m->funcs.items[0], *g = m->funcs.items[1];
    assert(f->kernel_hook && !g->kernel_hook);
    assert(f->n_params == 3 && f->locals.len == 3);
    for (size_t i = 0; i < 3; i++) {
        const IrLocal *l = f->locals.items[i];
        assert(l->slot == i && l->is_param);
    }
    const IrType *u8 = ir_type_int(&st, 8, false);
    assert(((const IrLocal *)f->locals.items[0])->type == u8);
    assert(((const IrLocal *)f->locals.items[1])->type == ir_type_array(&st, 2, u8));
    const IrType *sl = ((const IrLocal *)f->locals.items[2])->type;
    assert(sl == ir_type_slice(&st, u8));                                /* interned: pointer equality */
    assert(((const IrLocal *)g->locals.items[0])->type == sl);            /* the same slice type in another function */
    assert(f->ret == ir_type_int(&st, 32, false) && g->ret == ir_type_unit(&st));
    /* a return type prints; unit (no '-> type') prints (returns) */
    expect(capture(dump_f, f),
           "(fn@2:1-3:52 fn:f kernel_hook\n"
           "  (params (param@3:6-3:11 a #0 u8) (param@3:13-3:21 b #1 [2]u8) (param@3:23-3:30 c #2 []u8))\n"
           "  (returns u32)\n"
           "  (block@3:39-3:52\n"
           "    (return@3:41-3:50\n"
           "      (int@3:48-3:49 0 u32))))");
    parse_done();
    ir_state_destroy(&st);
    pass("signatures: slots, is_param, interned param/return types, kernel_hook flag, (returns T)");
}

/* E0215's fix deletes the repeated attribute: pinned edit, and applying it yields a
 * file with no E02xx (the registry says "delete it"). */
static void test_duplicate_attribute_fix(void) {
    static IrState st;
    ir_state_init(&st, 1 << 20);
    const char *src = "manna v0;\n[kernel_hook] [kernel_hook] fn f() { }\n";
    parse_src(src);
    IrModule *m = NULL;
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS);
    assert(st.diags.len == 1 && strcmp(st.diags.items[0].code, "E0215") == 0);
    const Diagnostic *d = &st.diags.items[0];
    assert(d->span.start_line == 2 && d->span.start_col == 16);          /* the repeat's name */
    assert(d->fix && d->fix->n_edits == 1 && strcmp(d->fix->edits[0].text, "") == 0);
    Span e = d->fix->edits[0].span;                                      /* the whole `[kernel_hook]` */
    assert(e.start_line == 2 && e.start_col == 15 && e.end_line == 2 && e.end_col == 28);
    parse_done();

    /* apply: line 2 is `[kernel_hook] [kernel_hook] fn f() { }`; drop columns 15..27 */
    char fixed[128];
    const char *line = "[kernel_hook] [kernel_hook] fn f() { }\n";
    snprintf(fixed, sizeof fixed, "manna v0;\n%.*s%s", e.start_col - 1, line, line + (e.end_col - 1));
    assert(strcmp(fixed, "manna v0;\n[kernel_hook]  fn f() { }\n") == 0);
    parse_src(fixed);
    assert(g_ps.diags.len == 0);
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_BUILT);
    assert(((const IrFunc *)m->funcs.items[0])->kernel_hook);
    parse_done();
    ir_state_destroy(&st);
    pass("E0215: the fix is one edit deleting the repeat; applying it leaves a clean file");
}

/* ---- scopes (step 7) ---- */

static IrFunc *fake_function(IrState *st, const char *const *params, size_t n) {
    NodeList locals;
    node_list_init(&locals);
    for (size_t i = 0; i < n; i++) {
        node_list_push(st->arena, &locals, ir_local_new(st, (uint32_t)i, params[i], ir_type_int(st, 8, false), mk(1, 1, 1, 2), true));
    }
    return ir_func_new(st, mk(1, 1, 1, 2), "f", false, locals, n, ir_type_unit(st), NULL);
}

/* The visible-name table directly: push/pop, revival of a name in a sibling scope, growth, deep
 * nesting. After the last pop exactly the parameters are visible and every local still has its
 * own slot (storage is not scoped). */
static void test_scope_api(void) {
    static IrState st;
    ir_state_init(&st, 16 << 20);
    const char *params[] = {"a", "b"};
    IrFunc *f = fake_function(&st, params, 2);
    BodyCtx bc;
    body_init(&bc, &st, NULL, f);
    const IrType *u8 = ir_type_int(&st, 8, false);
    assert(body_lookup(&bc, "a") && body_lookup(&bc, "b") && !body_lookup(&bc, "c"));

    body_push_scope(&bc);                              /* the function body block */
    IrLocal *c = body_declare(&bc, "c", u8, mk(1, 1, 1, 2));
    assert(c->slot == 2 && !c->is_param && body_lookup(&bc, "c") == c);
    body_push_scope(&bc);                              /* an inner block */
    IrLocal *d = body_declare(&bc, "d", u8, mk(1, 1, 1, 2));
    assert(d->slot == 3 && body_lookup(&bc, "d") == d && body_lookup(&bc, "c") == c);
    body_pop_scope(&bc);
    assert(body_lookup(&bc, "d") == NULL && body_lookup(&bc, "c") == c && f->locals.len == 4);
    /* a sibling block reuses the dead name: a NEW local, a NEW slot */
    body_push_scope(&bc);
    IrLocal *d2 = body_declare(&bc, "d", u8, mk(1, 1, 1, 2));
    assert(d2 != d && d2->slot == 4 && body_lookup(&bc, "d") == d2);
    body_pop_scope(&bc);
    assert(body_lookup(&bc, "d") == NULL);
    body_pop_scope(&bc);
    assert(body_lookup(&bc, "c") == NULL && body_lookup(&bc, "a") && body_lookup(&bc, "b"));
    assert(bc.depth == 0 && f->locals.len == 5);

    /* growth: 5000 names in one scope, all visible, all gone after the pop */
    enum { N = 5000 };
    static char names[N][12];
    body_push_scope(&bc);
    for (int i = 0; i < N; i++) {
        snprintf(names[i], sizeof names[i], "n%d", i);
        IrLocal *l = body_declare(&bc, names[i], u8, mk(1, 1, 1, 2));
        assert(l->slot == (uint32_t)(5 + i));
    }
    for (int i = 0; i < N; i++) assert(body_lookup(&bc, names[i]) && strcmp(body_lookup(&bc, names[i])->name, names[i]) == 0);
    body_pop_scope(&bc);
    for (int i = 0; i < N; i++) assert(body_lookup(&bc, names[i]) == NULL);
    assert(body_lookup(&bc, "a") && f->locals.len == (size_t)(5 + N));

    /* deep nesting: 10000 scopes, one fresh name in each, unwound completely */
    enum { DEEP = 10000 };
    static char deep[DEEP][12];
    for (int i = 0; i < DEEP; i++) {
        body_push_scope(&bc);
        snprintf(deep[i], sizeof deep[i], "d%d", i);
        body_declare(&bc, deep[i], u8, mk(1, 1, 1, 2));
    }
    assert(bc.depth == DEEP && body_lookup(&bc, deep[0]) && body_lookup(&bc, deep[DEEP - 1]));
    for (int i = DEEP - 1; i >= 0; i--) {
        body_pop_scope(&bc);
        assert(body_lookup(&bc, deep[i]) == NULL);
        if (i > 0) assert(body_lookup(&bc, deep[i - 1]) != NULL); /* the enclosing scope's name survives */
    }
    assert(bc.depth == 0);
    for (int i = 0; i < DEEP; i++) assert(body_lookup(&bc, deep[i]) == NULL);
    assert(body_lookup(&bc, "a") && body_lookup(&bc, "b") && f->locals.len == (size_t)(5 + N + DEEP));
    ir_state_destroy(&st);
    pass("scopes: push/pop, a sibling revives a dead name with a new slot, growth to 5000, 10000 deep, parameters stay");
}

/* The same through the real builder: 3000 sibling blocks all declaring `y` (each its own slot),
 * and 120 levels of nesting each declaring a fresh name. */
static void test_scopes_through_the_builder(void) {
    enum { N = 3000 };
    char *src = malloc((size_t)N * 40 + 256);
    assert(src);
    static IrState st;
    ir_state_init(&st, 16 << 20);
    char *w = src + sprintf(src, "manna v0;\nfn f(c: bool) {\n");
    for (int i = 0; i < N; i++) w += sprintf(w, "    if c { let y: u8 = 1; }\n");
    sprintf(w, "}\n");
    parse_src(src);
    assert(g_ps.diags.len == 0);
    IrModule *m = NULL;
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_BUILT);
    const IrFunc *f = m->funcs.items[0];
    assert(f->locals.len == 1 + N && f->n_params == 1);
    for (size_t i = 1; i <= N; i++) assert(((const IrLocal *)f->locals.items[i])->slot == i);
    parse_done();

    w = src + sprintf(src, "manna v0;\nfn f(c: bool) {\n");
    for (int i = 0; i < 120; i++) w += sprintf(w, "if c { let v%d: u8 = %d;\n", i, i % 200);
    for (int i = 0; i < 120; i++) w += sprintf(w, "}\n");
    sprintf(w, "}\n");
    parse_src(src);
    assert(g_ps.diags.len == 0);
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_BUILT);
    assert(((const IrFunc *)m->funcs.items[0])->locals.len == 121);
    parse_done();
    ir_state_destroy(&st);
    free(src);
    pass("scopes through the builder: 3000 sibling blocks reuse a name, 120 nested blocks");
}

/* ---- operators (step 8) ---- */

/* Builds `fn f(a: TA, b: TB) { let r = a OP b; }` (struct P declared) and returns the status; the
 * diagnostics are left in st. */
static IrStatus build_binary(IrState *st, const char *ta, const char *tb, const char *op, IrModule **m) {
    char src[256];
    snprintf(src, sizeof src, "manna v0;\nstruct P { x: u8, }\nfn f(a: %s, b: %s) { let r = a %s b; }\n", ta, tb, op);
    parse_src(src);
    assert(g_ps.diags.len == 0);
    IrStatus r = ir_build(st, &g_ps.diags, &g_items, m);
    parse_done();
    return r;
}

/* The operator rules, written out again here as a table and compared with the checker on EVERY
 * operator x EVERY pair of operand types (integers, bool, a struct, an array): E0260 when an
 * operand type is not allowed (the left one first), E0261 when two allowed types differ, else it
 * builds. The python generator covers the same ground with spans; this covers more types. */
static void test_operator_matrix(void) {
    static IrState st;
    ir_state_init(&st, 32 << 20);
    static const char *const types[] = {"u8", "i8", "u32", "u64", "i64", "bool", "P", "[2]u8"};
    static const char *const ops[] = {"+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>",
                                      "<", ">", "<=", ">=", "==", "!=", "&&", "||"};
    int cases = 0;
    for (size_t o = 0; o < 18; o++) {
        bool logical = o >= 16, equality = o == 14 || o == 15;
        for (size_t l = 0; l < 8; l++) {
            for (size_t r = 0; r < 8; r++) {
                bool lint = l < 5, rint = r < 5, lbool = l == 5, rbool = r == 5;
                bool lok = logical ? lbool : (lint || (equality && lbool));
                bool rok = logical ? rbool : (rint || (equality && rbool));
                const char *want = NULL;
                if (!lok || !rok) want = "E0260";
                else if (l != r) want = "E0261";
                IrModule *m = NULL;
                size_t before = st.diags.len;
                IrStatus status = build_binary(&st, types[l], types[r], ops[o], &m);
                if (!want) {
                    assert(status == IR_BUILT && st.diags.len == before);
                } else {
                    if (status != IR_ERRORS || st.diags.len != before + 1 || strcmp(st.diags.items[before].code, want) != 0) {
                        fprintf(stderr, "operator matrix: %s %s %s expected %s\n", types[l], ops[o], types[r], want);
                        abort();
                    }
                }
                cases++;
            }
        }
    }
    ir_state_destroy(&st);
    printf("PASS: operator matrix: %d cases (18 operators x 8 x 8 operand types)\n", cases);
}

/* Result types: arithmetic/bitwise/shift keep the operand type, everything else is bool. */
static void test_operator_result_types(void) {
    static IrState st;
    ir_state_init(&st, 1 << 20);
    static const struct { const char *op, *want; } cases[] = {
        {"+", "u32"}, {"-", "u32"}, {"*", "u32"}, {"/", "u32"}, {"%", "u32"}, {"&", "u32"}, {"|", "u32"}, {"^", "u32"},
        {"<<", "u32"}, {">>", "u32"}, {"<", "bool"}, {">", "bool"}, {"<=", "bool"}, {">=", "bool"}, {"==", "bool"}, {"!=", "bool"},
    };
    for (size_t i = 0; i < 16; i++) {
        IrModule *m = NULL;
        assert(build_binary(&st, "u32", "u32", cases[i].op, &m) == IR_BUILT);
        const IrFunc *f = m->funcs.items[0];
        const IrStmt *let = f->body->as.block.stmts.items[0];
        assert(strcmp(ir_type_text(&st, let->as.let.init->type), cases[i].want) == 0);
        assert(let->as.let.init->kind == IR_E_BINARY && let->as.let.init->as.binary.op == (BinOp)i);
    }
    IrModule *m = NULL;
    assert(build_binary(&st, "bool", "bool", "&&", &m) == IR_BUILT);
    ir_state_destroy(&st);
    pass("operator result types: arithmetic keeps the operand type, comparisons and logic give bool");
}

/* E0261's fix (D4), pinned: which operand is wrapped, in what, and the exact edits - two
 * zero-width inserts, `T(` before the operand and `)` after it. Applied to the source the text is
 * the expected one (the file then PARSES; re-checking it waits for conversions, step 13). */
/* Step 11: the FULL span (start and end) of two diagnostics whose goldens print only the start:
 * E0243 covers the whole construction, E0251 the base expression (not the whole access). */
static void test_construction_and_access_spans(void) {
    static IrState st;
    ir_state_init(&st, 1 << 20);
    parse_src("manna v0;\nstruct P { x: u8, y: u8, }\nfn f(n: u8) {\n    let a = P(x: 1);\n    let b = (n + 1).v;\n}\n");
    assert(g_ps.diags.len == 0);
    IrModule *m = NULL;
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS && st.diags.len == 2);
    const Diagnostic *miss = &st.diags.items[0], *nofields = &st.diags.items[1];
    assert(strcmp(miss->code, "E0243") == 0);
    assert(miss->span.start_line == 4 && miss->span.start_col == 13 && miss->span.end_line == 4 && miss->span.end_col == 20); /* `P(x: 1)` */
    assert(strcmp(nofields->code, "E0251") == 0);
    assert(nofields->span.start_line == 5 && nofields->span.start_col == 13 && nofields->span.end_line == 5 && nofields->span.end_col == 20); /* `(n + 1)`, not `(n + 1).v` */
    parse_done();
    ir_state_destroy(&st);
    pass("E0243 spans the whole construction, E0251 the base expression (start and end)");
}

/* Step 12: the FULL span of E0252 (the base expression, not the whole index), E0253 (the index) and E0222
 * on a literal (the whole literal): the goldens print the start only. */
static void test_array_spans(void) {
    static IrState st;
    ir_state_init(&st, 1 << 20);
    parse_src("manna v0;\nfn f(a: [2]u8, n: u8, b: bool) {\n    let x = (n + 1)[0];\n    let y = a[b];\n    let z = [n; 2147483648];\n}\n");
    assert(g_ps.diags.len == 0);
    IrModule *m = NULL;
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS && st.diags.len == 3);
    const Diagnostic *d0 = &st.diags.items[0], *d1 = &st.diags.items[1], *d2 = &st.diags.items[2];
    assert(strcmp(d0->code, "E0252") == 0 && d0->span.start_line == 3 && d0->span.start_col == 13 && d0->span.end_line == 3 && d0->span.end_col == 20); /* `(n + 1)` */
    assert(strcmp(d1->code, "E0253") == 0 && d1->span.start_line == 4 && d1->span.start_col == 15 && d1->span.end_line == 4 && d1->span.end_col == 16); /* `b` */
    assert(strcmp(d2->code, "E0222") == 0 && d2->span.start_line == 5 && d2->span.start_col == 13 && d2->span.end_line == 5 && d2->span.end_col == 28); /* `[n; 2147483648]` */
    parse_done();
    ir_state_destroy(&st);
    pass("E0252 spans the base, E0253 the index, E0222 the whole literal (start and end)");
}

/* The checker stays linear on array shapes: every node visited at most twice, on 512-high chains. */
static void test_array_checker_is_linear(void) {
    static IrState st;
    ir_state_init(&st, 16 << 20);
    char *src = malloc(8192);
    char *w;
    w = src + sprintf(src, "manna v0;\nfn f(a: [1][1][1][1][1][1][1][1]u64) -> u64 { return a");
    for (int i = 0; i < 8; i++) w += sprintf(w, "[0]");
    sprintf(w, "; }\n");
    parse_src(src);
    assert(g_ps.diags.len == 0);
    IrModule *m = NULL;
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_BUILT);
    /* return value: 1 local + 8 indexes + 8 literal indexes; each visited once by synth, each literal index
     * resolved once more by group_against */
    assert(st.expr_visits <= 2 * 17);
    parse_done();
    free(src);
    ir_state_destroy(&st);
    pass("array checker: index chains are visited at most twice per node");
}

/* Step 12b: full spans of E0254 (the call), E0255 (the argument), E0256 (the name), E0257 (the name). */
static void test_len_spans(void) {
    static IrState st;
    ir_state_init(&st, 1 << 20);
    parse_src("manna v0;\nfn f(a: [2]u8, n: u8) {\n    let x = len(a, a);\n    let y = len(n);\n    let z = len;\n    let len: u8 = 1;\n}\n");
    assert(g_ps.diags.len == 0);
    IrModule *m = NULL;
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS && st.diags.len == 4);
    static const struct { const char *code; int line, c0, c1; } want[] = {
        {"E0254", 3, 13, 22}, /* `len(a, a)` */
        {"E0255", 4, 17, 18}, /* `n` */
        {"E0256", 5, 13, 16}, /* `len` */
        {"E0257", 6, 9, 12},  /* the declared name */
    };
    for (int i = 0; i < 4; i++) {
        const Diagnostic *d = &st.diags.items[i];
        assert(strcmp(d->code, want[i].code) == 0);
        assert(d->span.start_line == want[i].line && d->span.end_line == want[i].line);
        assert(d->span.start_col == want[i].c0 && d->span.end_col == want[i].c1);
    }
    parse_done();
    ir_state_destroy(&st);
    pass("E0254-E0257: full spans (call, argument, name, declared name)");
}

/* Step 13: the FULL span of E0265 (the whole conversion) and of E0225 on a conversion's literal operand (the operand). */
static void test_convert_spans(void) {
    static IrState st;
    ir_state_init(&st, 1 << 20);
    parse_src("manna v0;\nfn f(x: u8, b: bool) {\n    let a = bool(x);\n    let c = u8(300);\n    let d = u16(b);\n}\n");
    assert(g_ps.diags.len == 0);
    IrModule *m = NULL;
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS && st.diags.len == 3);
    static const struct { const char *code; int c0, c1; const char *msg; } want[] = {
        {"E0265", 13, 20, "can't convert u8 to bool; conversions are between integer types only (write 'x != 0')"}, /* `bool(x)` */
        {"E0225", 16, 19, "cannot infer the type of this integer literal; annotate it"},                                /* `300` */
        {"E0265", 13, 19, "can't convert bool to u16; conversions are between integer types only"},                    /* `u16(b)` */
    };
    for (int i = 0; i < 3; i++) {
        const Diagnostic *d = &st.diags.items[i];
        assert(strcmp(d->code, want[i].code) == 0 && strcmp(d->message, want[i].msg) == 0);
        assert(d->span.start_line == 3 + i && d->span.end_line == 3 + i);
        assert(d->span.start_col == want[i].c0 && d->span.end_col == want[i].c1);
    }
    parse_done();
    ir_state_destroy(&st);
    pass("E0265 spans the whole conversion, E0225 its literal operand (start and end, exact messages)");
}

/* Step 14a: the predicates and the case converter (ir_names.c). */
static void test_naming_predicates_and_converter(void) {
    static const struct { const char *name; bool snake, pascal; } preds[] = {
        {"x", true, false}, {"foo", true, false}, {"foo_bar", true, false}, {"x1", true, false}, {"a_1", true, false}, {"a_1_b", true, false},
        {"foo2", true, false}, {"X", false, true}, {"Foo", false, true}, {"FooBar", false, true}, {"HTTPServer", false, true}, {"POINT", false, true}, {"A1b", false, true},
        {"_", false, false}, {"_x", false, false}, {"x_", false, false}, {"a__b", false, false}, {"fooBar", false, false}, {"Foo_bar", false, false},
        {"foo_Bar", false, false}, {"1x", false, false}, {"_X", false, false}, {"X_", false, false}, {"x_Y", false, false},
    };
    for (size_t i = 0; i < sizeof preds / sizeof preds[0]; i++) {
        assert(ir_is_snake_case(preds[i].name) == preds[i].snake);
        assert(ir_is_pascal_case(preds[i].name) == preds[i].pascal);
    }
    Arena *a = arena_create(1 << 20);
    static const struct { const char *in, *snake, *pascal; } conv[] = {
        {"fooBar", "foo_bar", "FooBar"}, {"HTTPServer", "http_server", "HTTPServer"}, {"X", "x", "X"}, {"getHTTPResponse2", "get_http_response2", "GetHTTPResponse2"},
        {"FOO_BAR", "foo_bar", "FOOBAR"}, {"a__b", "a_b", "AB"}, {"_x", "x", "X"}, {"__x", "x", "X"}, {"x_", "x", "X"}, {"foo_Bar", "foo_bar", "FooBar"},
        {"ABC", "abc", "ABC"}, {"x1Y", "x1_y", "X1Y"}, {"my_struct", "my_struct", "MyStruct"}, {"point", "point", "Point"}, {"my_HTTP_server", "my_http_server", "MyHTTPServer"},
        {"HTTP_server", "http_server", "HTTPServer"}, {"X1_y", "x1_y", "X1Y"}, {"XMLHttp2", "xml_http2", "XMLHttp2"}, {"a_1b", "a_1b", "A1b"}, {"ab12cd", "ab12cd", "Ab12cd"},
        {"foo2Bar", "foo2_bar", "Foo2Bar"}, {"_point", "point", "Point"}, {"point_", "point", "Point"}, {"Foo", "foo", "Foo"}, {"fooBarBaz", "foo_bar_baz", "FooBarBaz"},
    };
    for (size_t i = 0; i < sizeof conv / sizeof conv[0]; i++) {
        const char *sn = ir_to_snake_case(a, conv[i].in), *pa = ir_to_pascal_case(a, conv[i].in);
        if (!sn || !pa || strcmp(sn, conv[i].snake) != 0 || strcmp(pa, conv[i].pascal) != 0) {
            fprintf(stderr, "convert %s: snake %s pascal %s\n", conv[i].in, sn ? sn : "(null)", pa ? pa : "(null)");
            abort();
        }
    }
    /* no valid name: empty after dropping underscores, or it would start with a digit */
    static const char *const none[] = {"_", "__", "___", "_1x", "__9"};
    for (size_t i = 0; i < sizeof none / sizeof none[0]; i++) {
        assert(ir_to_snake_case(a, none[i]) == NULL && ir_to_pascal_case(a, none[i]) == NULL);
    }
    /* reserved: the lexer's keywords and the builtin */
    static const char *const reserved[] = {"fn", "let", "if", "else", "for", "while", "return", "struct", "true", "false", "break", "continue", "manna",
                                           "u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64", "bool", "unit", "len"};
    for (size_t i = 0; i < sizeof reserved / sizeof reserved[0]; i++) assert(ir_is_reserved_name(reserved[i]));
    static const char *const free_names[] = {"x", "length", "u8x", "bool_", "fnn", "Len", "lenx", "unit_"};
    for (size_t i = 0; i < sizeof free_names / sizeof free_names[0]; i++) assert(!ir_is_reserved_name(free_names[i]));
    arena_destroy(a);

    /* Exhaustive property test: every identifier over {a, B, _, 1} up to length 7. A non-NULL result satisfies
     * its predicate; converting it again returns it unchanged (idempotent); a conforming name converts to
     * itself; the two results never disagree on emptiness. */
    static const char alphabet[4] = {'a', 'B', '_', '1'};
    Arena *pa = arena_create(64 << 20);
    long checked = 0;
    for (int len = 1; len <= 7; len++) {
        long total = 1;
        for (int k = 0; k < len; k++) total *= 4;
        for (long code = 0; code < total; code++) {
            char name[8];
            long c = code;
            for (int k = 0; k < len; k++) { name[k] = alphabet[c % 4]; c /= 4; }
            name[len] = '\0';
            if (name[0] >= '0' && name[0] <= '9') continue; /* the lexer never produces a leading digit */
            const char *sn = ir_to_snake_case(pa, name), *pc = ir_to_pascal_case(pa, name);
            assert((sn == NULL) == (pc == NULL));
            if (sn) {
                assert(ir_is_snake_case(sn) && strcmp(ir_to_snake_case(pa, sn), sn) == 0);
                assert(ir_is_pascal_case(pc) && strcmp(ir_to_pascal_case(pa, pc), pc) == 0);
            }
            if (ir_is_snake_case(name)) assert(sn && strcmp(sn, name) == 0);
            if (ir_is_pascal_case(name)) assert(pc && strcmp(pc, name) == 0);
            checked++;
        }
    }
    arena_destroy(pa);
    printf("PASS: naming: predicates and the case converter (%ld identifiers checked exhaustively: valid, idempotent, conforming names unchanged)\n", checked);
}

/* Step 14b: the fix's edit list is a flexible array: any number of edits in one allocation; the six old
 * constructors keep their edits. */
static void test_diag_fix_edit_list(void) {
    Arena *a = arena_create(16 << 20);
    _Static_assert(sizeof(DiagFix) <= 16, "DiagFix is a header; its edits follow it");
    enum { N = 20000 };
    DiagFix *fix = diag_fix_new(a, N);
    assert(fix->n_edits == 0);
    fix->title = "many";
    for (int i = 0; i < N; i++) {
        fix->edits[i].span = (Span){i + 1, 1, i + 1, 4};
        fix->edits[i].text = "x";
    }
    fix->n_edits = N;
    for (int i = 0; i < N; i++) assert(fix->edits[i].span.start_line == i + 1 && fix->edits[i].span.end_col == 4);
    assert((char *)&fix->edits[N] - (char *)fix == (long)(sizeof(DiagFix) + N * sizeof(DiagEdit))); /* contiguous, no hidden padding */
    /* the old constructors */
    Span s1 = {1, 1, 1, 2}, s2 = {1, 5, 1, 6};
    Diagnostic d = diag_mixed_comparison(a, s1, s1, s2);
    assert(d.fix && d.fix->n_edits == 2 && strcmp(d.fix->edits[0].text, "(") == 0 && strcmp(d.fix->edits[1].text, ")") == 0);
    d = diag_assignment_as_condition(a, s1, "'{'");
    assert(d.fix && d.fix->n_edits == 1 && strcmp(d.fix->edits[0].text, "==") == 0);
    d = diag_return_unit(a, s1, s2, true);
    assert(d.fix && d.fix->n_edits == 1);
    d = diag_return_unit(a, s1, s2, false);
    assert(d.fix && d.fix->n_edits == 2);
    arena_destroy(a);
    pass("DiagFix: a flexible edit list (20,000 edits in one allocation); the old constructors keep their edits");
}

/* Step 14c: naming errors. Full spans; they alone do not stop the build; a `len`-named struct (E0257) still does,
 * even next to a naming error: the bare-`len` `assert(!top)` in check_ident depends on that. */
static void test_naming_errors(void) {
    static IrState st;
    ir_state_init(&st, 1 << 20);
    IrModule *m = NULL;
    /* spans: the declared name only */
    parse_src("manna v0;\nstruct my_point { fooBar: u8, }\nfn doIt(Count: u8) {\n    let myVar: u8 = 1;\n}\n");
    assert(g_ps.diags.len == 0);
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS && st.diags.len == 5);
    static const struct { const char *code; int line, c0, c1; } want[] = {
        {"E0281", 2, 8, 16}, {"E0280", 3, 4, 8}, {"E0280", 2, 19, 25}, {"E0280", 3, 9, 14}, {"E0280", 4, 9, 14}, /* my_point, doIt (the names pass), fooBar (fields), Count (signatures), myVar (bodies) */
    };
    for (int i = 0; i < 5; i++) {
        const Diagnostic *d = &st.diags.items[i];
        assert(strcmp(d->code, want[i].code) == 0 && d->span.start_line == want[i].line && d->span.end_line == want[i].line);
        assert(d->span.start_col == want[i].c0 && d->span.end_col == want[i].c1);
    }
    parse_done();
    /* naming-only declaration errors: the bodies are still checked */
    size_t before = st.diags.len;
    parse_src("manna v0;\nstruct bad_name { }\nfn f() {\n    let x = nope;\n}\n");
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS);
    assert(st.diags.len == before + 2 && strcmp(st.diags.items[before].code, "E0281") == 0 && strcmp(st.diags.items[before + 1].code, "E0230") == 0);
    parse_done();
    /* any other declaration error stops it, E0257 included, also next to a naming error */
    before = st.diags.len;
    parse_src("manna v0;\nstruct len { }\nstruct bad_name { }\nfn f() {\n    let x = nope;\n    let y = len;\n}\n");
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS);
    assert(st.diags.len == before + 2 && strcmp(st.diags.items[before].code, "E0257") == 0 && strcmp(st.diags.items[before + 1].code, "E0281") == 0);
    parse_done();
    before = st.diags.len;
    parse_src("manna v0;\nstruct P { x: u8, x: u8, }\nstruct bad_name { }\nfn f() {\n    let x = nope;\n}\n");
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS);
    assert(st.diags.len == before + 2 && strcmp(st.diags.items[before].code, "E0281") == 0 && strcmp(st.diags.items[before + 1].code, "E0211") == 0); /* the names pass comes first */
    parse_done();
    ir_state_destroy(&st);
    pass("naming: E0280/E0281 spans; naming-only errors keep the bodies in; E0257 or any other declaration error stops the build");
}

/* Step 14d: the rename fixes. */

/* 1-based line and column of the `occ`-th occurrence of `needle` on 1-based `line` of `src`, as a Span. */
static Span span_in(const char *src, int line, const char *needle, int occ) {
    const char *p = src;
    for (int l = 1; l < line; l++) p = strchr(p, '\n') + 1;
    const char *eol = strchr(p, '\n');
    const char *hit = p - 1;
    for (int k = 0; k <= occ; k++) {
        hit = strstr(hit + 1, needle);
        assert(hit && hit < eol);
    }
    int col = (int)(hit - p) + 1;
    return (Span){line, col, line, col + (int)strlen(needle)};
}

static bool same_span(Span a, Span b) {
    return a.start_line == b.start_line && a.start_col == b.start_col && a.end_line == b.end_line && a.end_col == b.end_col;
}

static void test_rename_fixes(void) {
    static IrState st;
    ir_state_init(&st, 4 << 20);
    IrModule *m = NULL;
    const char *src = "manna v0;\nstruct my_point { fooBar: u8, }\nfn doIt(Count: u8) -> u8 {\n    let p: my_point = my_point(fooBar: Count);\n    return p.fooBar + Count;\n}\nfn g() -> u8 {\n    return doIt(1);\n}\n";
    parse_src(src);
    assert(g_ps.diags.len == 0);
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS && st.diags.len == 4);
    /* order: the names pass (my_point, doIt), the fields (fooBar), the signatures (Count) */
    struct { const char *code, *title, *text; int n; Span spans[6]; } want[] = {
        {"E0281", "rename 'my_point' to 'MyPoint'", "MyPoint", 3, {span_in(src, 2, "my_point", 0), span_in(src, 4, "my_point", 0), span_in(src, 4, "my_point", 1)}},
        {"E0280", "rename 'doIt' to 'do_it'", "do_it", 2, {span_in(src, 3, "doIt", 0), span_in(src, 8, "doIt", 0)}},
        {"E0280", "rename 'fooBar' to 'foo_bar'", "foo_bar", 3, {span_in(src, 2, "fooBar", 0), span_in(src, 4, "fooBar", 0), span_in(src, 5, "fooBar", 0)}},
        {"E0280", "rename 'Count' to 'count'", "count", 3, {span_in(src, 3, "Count", 0), span_in(src, 4, "Count", 0), span_in(src, 5, "Count", 0)}},
    };
    for (int i = 0; i < 4; i++) {
        const Diagnostic *d = &st.diags.items[i];
        assert(strcmp(d->code, want[i].code) == 0 && d->fix);
        assert(strcmp(d->fix->title, want[i].title) == 0 && d->fix->n_edits == want[i].n);
        for (int k = 0; k < want[i].n; k++) {
            assert(same_span(d->fix->edits[k].span, want[i].spans[k]) && strcmp(d->fix->edits[k].text, want[i].text) == 0);
            if (k) { /* sorted, non-overlapping */
                Span a = d->fix->edits[k - 1].span, b = d->fix->edits[k].span;
                assert(a.end_line < b.start_line || (a.end_line == b.start_line && a.end_col <= b.start_col));
            }
        }
    }
    /* applying all of them gives the expected source, which then checks clean */
    char *fixed = ir_apply_fixes(src, strlen(src), &st.diags);
    assert(strstr(fixed, "struct MyPoint { foo_bar: u8, }") && strstr(fixed, "fn do_it(count: u8) -> u8 {"));
    assert(strstr(fixed, "let p: MyPoint = MyPoint(foo_bar: count);") && strstr(fixed, "return p.foo_bar + count;") && strstr(fixed, "return do_it(1);"));
    parse_done();
    parse_src(fixed);
    size_t before = st.diags.len;
    assert(g_ps.diags.len == 0 && ir_build(&st, &g_ps.diags, &g_items, &m) == IR_BUILT && st.diags.len == before);
    parse_done();
    free(fixed);
    /* a use before the declaration: the edit list is still in source order (use first, declaration second) */
    {
        const char *src2 = "manna v0;\nfn g() -> u8 {\n    return doIt(1);\n}\nfn doIt(x: u8) -> u8 {\n    return x;\n}\n";
        parse_src(src2);
        assert(g_ps.diags.len == 0);
        size_t b2 = st.diags.len;
        assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS && st.diags.len == b2 + 1);
        const DiagFix *fx = st.diags.items[b2].fix;
        assert(fx && fx->n_edits == 2);
        assert(same_span(fx->edits[0].span, span_in(src2, 3, "doIt", 0)) && same_span(fx->edits[1].span, span_in(src2, 5, "doIt", 0)));
        parse_done();
    }

    /* no fix: another declaration error hides the uses; a keyword target; the error cap */
    before = st.diags.len;
    parse_src("manna v0;\n[kernel_hook] [kernel_hook] fn doIt() {\n}\n");
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS && st.diags.len == before + 2);
    assert(strcmp(st.diags.items[before].code, "E0280") == 0 && st.diags.items[before].fix == NULL);   /* its uses are unknown */
    assert(strcmp(st.diags.items[before + 1].code, "E0215") == 0 && st.diags.items[before + 1].fix);   /* the other fix stays */
    parse_done();
    before = st.diags.len;
    parse_src("manna v0;\nfn Let() {\n}\n");
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS && st.diags.len == before + 1 && st.diags.items[before].fix == NULL);
    parse_done();
    /* the cap: 25 misnamed functions; the build unwinds at the 20th error and offers no fix; the state is reusable */
    char big[4096];
    char *w = big + sprintf(big, "manna v0;\n");
    for (int i = 0; i < 25; i++) w += sprintf(w, "fn bad%c() {\n}\n", 'A' + i);
    before = st.diags.len;
    parse_src(big);
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS && st.diags.len == before + 21);
    for (size_t i = before; i < st.diags.len; i++) assert(st.diags.items[i].fix == NULL);
    assert(strcmp(st.diags.items[st.diags.len - 1].code, "E0203") == 0);
    parse_done();
    before = st.diags.len;
    parse_src("manna v0;\nfn badName() {\n}\n");
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS && st.diags.len == before + 1 && st.diags.items[before].fix && st.diags.items[before].fix->n_edits == 1);
    parse_done();
    ir_state_destroy(&st);
    pass("rename fixes: declaration + every use (sorted, non-overlapping, exact spans and titles), applied source checks clean; none when the uses are unknown, the target is a keyword or the cap unwound; state reusable");
}

/* The finalizer sorts the edits and drops an exact duplicate. The checker never records a use twice and never two
 * spans with one start, so this feeds ir_note_use directly (the defensive tie-breaks of cmp_span). */
static void test_rename_edit_order_and_dedupe(void) {
    static IrState st;
    ir_state_init(&st, 1 << 20);
    Ctx cx;
    memset(&cx, 0, sizeof cx);
    symtab_init(&st, &cx.top, 1);
    Rename *r = ir_check_naming(&st, &cx, REN_TOP, 0, false, "doIt", (Span){3, 4, 3, 8});
    assert(r && st.diags.len == 1);
    ir_note_use(&st, r, (Span){7, 1, 8, 3});  /* same start as the next two, ends later */
    ir_note_use(&st, r, (Span){5, 5, 5, 9});
    ir_note_use(&st, r, (Span){7, 1, 7, 9});
    ir_note_use(&st, r, (Span){5, 5, 5, 9});  /* the exact duplicate */
    ir_note_use(&st, r, (Span){7, 1, 7, 5});  /* same start, earliest end */
    ir_note_use(&st, NULL, (Span){1, 1, 1, 2}); /* a no-op */
    ir_finalize_renames(&st, &cx);
    const DiagFix *fx = st.diags.items[0].fix;
    assert(fx && fx->n_edits == 5);
    Span want[5] = {{3, 4, 3, 8}, {5, 5, 5, 9}, {7, 1, 7, 5}, {7, 1, 7, 9}, {7, 1, 8, 3}};
    for (int i = 0; i < 5; i++) assert(same_span(fx->edits[i].span, want[i]) && strcmp(fx->edits[i].text, "do_it") == 0);
    /* The three edits that start at 7:1 overlap each other. The builder never produces that (the uses of one name are
     * disjoint), but ir_apply_fixes takes any DiagList: it applies the first and skips the other two rather than
     * corrupt the text (ir_rename.c, the `a < pos` skip). */
    const char *src = "manna v0;\nxx\nabcdoItZ\nl4\n1234doIt9\nl6\ndoItzzzz\nl8\n";
    char *fixed = ir_apply_fixes(src, strlen(src), &st.diags);
    assert(strcmp(fixed, "manna v0;\nxx\nabcdo_itZ\nl4\n1234do_it9\nl6\ndo_itzzzz\nl8\n") == 0);
    free(fixed);
    ir_state_destroy(&st);
    pass("rename edits: sorted by start then end, an exact duplicate use dropped (ir_note_use fed directly); overlapping edits are skipped, not applied");
}

static void test_e0261_fix(void) {
    static IrState st;
    ir_state_init(&st, 1 << 20);
    static const struct { const char *ta, *tb, *op, *expr, *fixed; bool left; const char *to; } cases[] = {
        {"u8", "u32", "+", "a + b", "u32(a) + b", true, "u32"},      /* the narrower converts losslessly: wrap it */
        {"u32", "u8", "+", "a + b", "a + u32(b)", false, "u32"},
        {"u8", "i32", "*", "a * b", "i32(a) * b", true, "i32"},      /* unsigned -> wider signed is lossless */
        {"i32", "u8", "<", "a < b", "a < i32(b)", false, "i32"},
        {"u32", "i64", "==", "a == b", "i64(a) == b", true, "i64"},
        {"i8", "i64", "/", "a / b", "i64(a) / b", true, "i64"},
        {"u32", "u8", ">>", "a >> b", "a >> u32(b)", false, "u32"},
        {"u8", "u32", "&", "(a) & b", "u32((a)) & b", true, "u32"},  /* the operand's span includes its parentheses */
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char src[256];
        snprintf(src, sizeof src, "manna v0;\nfn f(a: %s, b: %s) { let r = %s; }\n", cases[i].ta, cases[i].tb, cases[i].expr);
        parse_src(src);
        assert(g_ps.diags.len == 0);
        IrModule *m = NULL;
        size_t before = st.diags.len;
        assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS && st.diags.len == before + 1);
        const Diagnostic *d = &st.diags.items[before];
        assert(strcmp(d->code, "E0261") == 0 && d->fix && d->fix->n_edits == 2);
        const DiagEdit *e0 = &d->fix->edits[0], *e1 = &d->fix->edits[1];
        char to_open[16];
        snprintf(to_open, sizeof to_open, "%s(", cases[i].to);
        assert(strcmp(e0->text, to_open) == 0 && strcmp(e1->text, ")") == 0);
        assert(e0->span.start_line == e0->span.end_line && e0->span.start_col == e0->span.end_col); /* zero-width */
        assert(e1->span.start_line == e1->span.end_line && e1->span.start_col == e1->span.end_col);
        /* apply: both edits are on the function's source line (line 2) */
        const char *ln2 = strchr(src, '\n') + 1;
        size_t c0 = (size_t)e0->span.start_col - 1, c1 = (size_t)e1->span.start_col - 1;
        char fixed[256];
        snprintf(fixed, sizeof fixed, "%.*s%s%.*s%s%s", (int)c0, ln2, e0->text, (int)(c1 - c0), ln2 + c0, e1->text, ln2 + c1);
        char want[256];
        snprintf(want, sizeof want, "fn f(a: %s, b: %s) { let r = %s; }\n", cases[i].ta, cases[i].tb, cases[i].fixed);
        if (strcmp(fixed, want) != 0) { fprintf(stderr, "fix applied: %s want: %s", fixed, want); abort(); }
        parse_done();
        char fsrc[300];
        snprintf(fsrc, sizeof fsrc, "manna v0;\n%s", fixed);
        parse_src(fsrc);
        assert(g_ps.diags.len == 0); /* the fixed file parses cleanly */
        /* ... and the type checker accepts it: a fix that leaves the program rejected is no fix (audit finding 10) */
        size_t before_fixed = st.diags.len;
        IrModule *mf = NULL;
        assert(ir_build(&st, &g_ps.diags, &g_items, &mf) == IR_BUILT && mf && st.diags.len == before_fixed);
        parse_done();
    }
    /* With a type expected of the whole expression (a return, an annotated let, an argument, an assignment, an `if`), the fix must also make
     * the result that type, by one lossless widening: it is applied and re-checked; where no single wrap can, there is no fix. */
    static const struct { const char *line2, *fixed; } ctx[] = {
        {"fn f(a: u8, b: u32) -> u32 { return a + b; }\n", "fn f(a: u8, b: u32) -> u32 { return u32(a) + b; }\n"},
        {"fn f(a: u32, b: u8) -> u32 { return a + b; }\n", "fn f(a: u32, b: u8) -> u32 { return a + u32(b); }\n"},
        {"fn f(a: u8, b: u32) { let r: u32 = a * b; }\n", "fn f(a: u8, b: u32) { let r: u32 = u32(a) * b; }\n"},
        {"fn g(x: u32) -> u32 { return x; } fn f(a: u8, b: u32) -> u32 { return g(a + b); }\n", "fn g(x: u32) -> u32 { return x; } fn f(a: u8, b: u32) -> u32 { return g(u32(a) + b); }\n"},
        {"fn f(a: u8, b: u32) { let r: u32 = 0; r = a & b; }\n", "fn f(a: u8, b: u32) { let r: u32 = 0; r = u32(a) & b; }\n"},
        {"fn f(a: u32, b: u8) -> u32 { return a << b; }\n", "fn f(a: u32, b: u8) -> u32 { return a << u32(b); }\n"},
        {"fn f(a: u8, b: u32) { if a < b { } }\n", "fn f(a: u8, b: u32) { if u32(a) < b { } }\n"},
        {"fn f(a: i8, b: i32) { if a == b { } }\n", "fn f(a: i8, b: i32) { if i32(a) == b { } }\n"},
        {"fn f(a: u32, b: u8) { if a < b { } }\n", "fn f(a: u32, b: u8) { if a < u32(b) { } }\n"}, /* the right operand widens */
    };
    for (size_t i = 0; i < sizeof ctx / sizeof ctx[0]; i++) {
        char src[300];
        snprintf(src, sizeof src, "manna v0;\n%s", ctx[i].line2);
        parse_src(src);
        assert(g_ps.diags.len == 0);
        IrModule *m = NULL;
        size_t before = st.diags.len;
        assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS && st.diags.len == before + 1);
        const Diagnostic *d = &st.diags.items[before];
        assert(strcmp(d->code, "E0261") == 0 && d->fix && d->fix->n_edits == 2);
        const char *ln2 = strchr(src, '\n') + 1;
        size_t c0 = (size_t)d->fix->edits[0].span.start_col - 1, c1 = (size_t)d->fix->edits[1].span.start_col - 1;
        char fixed[300];
        snprintf(fixed, sizeof fixed, "%.*s%s%.*s%s%s", (int)c0, ln2, d->fix->edits[0].text, (int)(c1 - c0), ln2 + c0, d->fix->edits[1].text, ln2 + c1);
        if (strcmp(fixed, ctx[i].fixed) != 0) { fprintf(stderr, "fix applied: %s want: %s", fixed, ctx[i].fixed); abort(); }
        parse_done();
        char fsrc[320];
        snprintf(fsrc, sizeof fsrc, "manna v0;\n%s", fixed);
        parse_src(fsrc);
        assert(g_ps.diags.len == 0);
        before = st.diags.len;
        assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_BUILT && m && st.diags.len == before); /* accepted */
        parse_done();
    }
    static const char *const nofix_ctx[] = {
        "fn f(a: u8, b: u8) -> u8 { return u32(a) + b; }\n",  /* the audit's case: no lossless wrap gives u8 */
        "fn f(a: u32, b: u8) -> u8 { return a + b; }\n",      /* the result would have to be narrowed */
        "fn f(a: u8, b: u32) -> u8 { return a + b; }\n",
        "fn f(a: u8, b: i8) -> u8 { return a + b; }\n",       /* neither direction is lossless */
        "fn f(a: u8, b: u32) -> u8 { return a << b; }\n",     /* the count would be narrowed */
        "fn f(a: u8, b: i8) { if a < b { } }\n",              /* a comparison with no widening either way */
        "fn f(a: u8, b: u32) { let r: bool = a + b; }\n",     /* nothing wrapped makes a u8 + u32 a bool */
        /* with nothing expected, the fix is still one LOSSLESS widening (these three had a narrowing fix before step 18) */
        "fn f(a: u8, b: i8) { let r = a + b; }\n",
        "fn f(a: i8, b: u8) { let r = a - b; }\n",
        "fn f(a: u8, b: u32) { let r = a << b; }\n",
        /* a wrapped operand must not make an enclosing operator fail, or lose the type the context expects (step 18: audit findings 1 and 4) */
        "fn g(x: u8) -> u8 { return x; } fn f(a: u8, b: u8, c: u16) -> u8 { return g(a + b) + c; }\n",
        "fn g(x: u8) -> u8 { return x; } fn f(a: u8, b: u8, c: u32) -> u8 { return g(a + b) << c; }\n",
        "fn f(a: u8, b: u16, c: u8) -> u8 { return a + b + c; }\n",
        "fn f(a: i8, b: i16) -> i8 { return -(a + b); }\n",
        "fn f(a: u8, b: u16, c: u8) { let r = a + b + c; }\n",   /* the fixed outer operator would fail again */
    };
    for (size_t i = 0; i < sizeof nofix_ctx / sizeof nofix_ctx[0]; i++) {
        char src[200];
        snprintf(src, sizeof src, "manna v0;\n%s", nofix_ctx[i]);
        parse_src(src);
        assert(g_ps.diags.len == 0);
        IrModule *m = NULL;
        size_t before = st.diags.len;
        assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS && st.diags.len == before + 1);
        assert(strcmp(st.diags.items[before].code, "E0261") == 0 && st.diags.items[before].fix == NULL);
        parse_done();
    }
    /* no integer conversion exists with a bool or a struct: no fix */
    static const struct { const char *ta, *tb; } nofix[] = {{"u8", "bool"}, {"bool", "u8"}};
    for (size_t i = 0; i < 2; i++) {
        char src[200];
        snprintf(src, sizeof src, "manna v0;\nfn f(a: %s, b: %s) { let r = a == b; }\n", nofix[i].ta, nofix[i].tb);
        parse_src(src);
        IrModule *m = NULL;
        size_t before = st.diags.len;
        assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS);
        assert(strcmp(st.diags.items[before].code, "E0261") == 0 && st.diags.items[before].fix == NULL);
        parse_done();
    }
    ir_state_destroy(&st);
    pass("E0261: the D4 fix pinned (which operand, what type, two zero-width inserts), the fixed source is accepted; with an expected type only a lossless widening to it, else no fix; no fix for bool");
}

/* The checker is linear: the single bottom-up pass visits every node at most twice (once to
 * classify and type it, once more if it is a literal group resolved against its sibling). On
 * 512-high chains that would be ~n*h/2 visits for a predicate re-run at each node; here
 * visits <= 2 * nodes at every height, and the ratio does not grow with height. */
static void test_checker_is_linear(void) {
    static IrState st;
    ir_state_init(&st, 32 << 20);
    static const char *const shapes[] = {"x_first", "x_last", "alternating"};
    for (size_t sh = 0; sh < 3; sh++) {
        for (int h = 128; h <= 512; h *= 2) {
            char *src = malloc((size_t)h * 12 + 128);
            assert(src);
            char *w = src + sprintf(src, "manna v0;\nfn f(x: u64) { let r: u64 = ");
            size_t nodes;
            if (sh == 0) {            /* x + 1 + 1 + ... : the typed operand at the bottom of the left spine */
                w += sprintf(w, "x");
                for (int i = 0; i < h - 1; i++) w += sprintf(w, " + 1");
                nodes = (size_t)(h - 1) * 2 + 1;
            } else if (sh == 1) {     /* 1 + 1 + ... + x : a literal chain, typed only at the root */
                w += sprintf(w, "1");
                for (int i = 0; i < h - 2; i++) w += sprintf(w, " + 1");
                w += sprintf(w, " + x");
                nodes = (size_t)(h - 1) * 2 + 1;
            } else {                  /* 1 + x + 1 + x + ... */
                w += sprintf(w, "1");
                for (int i = 0; i < h - 1; i++) w += sprintf(w, i % 2 ? " + 1" : " + x");
                nodes = (size_t)(h - 1) * 2 + 1;
            }
            sprintf(w, "; }\n");
            parse_src(src);
            assert(g_ps.diags.len == 0);
            IrModule *m = NULL;
            assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_BUILT);
            size_t visits = st.expr_visits;
            /* nodes counts the expression only; the let's annotation etc. add nothing to the counter */
            if (visits > 2 * nodes + 4) { fprintf(stderr, "%s h=%d: %zu visits for %zu nodes\n", shapes[sh], h, visits, nodes); abort(); }
            parse_done();
            free(src);
        }
    }
    ir_state_destroy(&st);
    pass("the checker is linear: <= 2 visits per node on 128/256/512-high chains with the typed operand at the bottom, the root, or alternating");
}

/* ---- calls (step 10) ---- */

/* The IR of a call: the callee is the module's own IrFunc (pointer identity), the type is its declared
 * return type, and the arguments are in WRITTEN order, each typed by its parameter. */
static void test_call_ir(void) {
    static IrState st;
    ir_state_init(&st, 1 << 20);
    parse_src("manna v0;\nfn mix(a: u8, b: bool, c: i64) -> i64 { return c; }\nfn f() -> i64 { return mix(1, true, 3); }\nfn g() { mix(2, false, 4); }\n");
    IrModule *m = NULL;
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_BUILT && m->funcs.len == 3);
    const IrFunc *mix = m->funcs.items[0], *f = m->funcs.items[1], *g = m->funcs.items[2];
    const IrExpr *c = ((const IrStmt *)f->body->as.block.stmts.items[0])->as.ret.value;
    assert(c->kind == IR_E_CALL && c->as.call.func == mix && c->type == ir_type_int(&st, 64, true));
    assert(c->as.call.args.len == 3);
    const IrExpr *a0 = c->as.call.args.items[0], *a1 = c->as.call.args.items[1], *a2 = c->as.call.args.items[2];
    assert(a0->kind == IR_E_INT && a0->as.integer.bits == 1 && a0->type == ir_type_int(&st, 8, false));
    assert(a1->kind == IR_E_BOOL && a1->as.boolean.value);
    assert(a2->kind == IR_E_INT && a2->as.integer.bits == 3 && a2->type == ir_type_int(&st, 64, true));
    /* a discarded call statement keeps the call (and its value, unused) */
    const IrStmt *es = g->body->as.block.stmts.items[0];
    assert(es->kind == IR_S_EXPR && es->as.expr.expr->kind == IR_E_CALL && es->as.expr.expr->as.call.func == mix);
    parse_done();
    ir_state_destroy(&st);
    pass("call IR: the callee is the module's own function, the type its return type, arguments in written order");
}

/* The call graph is analysed iteratively: a chain of 20000 functions each calling the next is fine; the
 * same chain closed into a ring is ONE E0235, at the first call made from the lowest function. Also run
 * under the 2 MB gate (--stack): a recursive walk 20000 deep would not fit. */
static void test_call_graph_scale(void) {
    enum { N = 20000 };
    char *src = malloc((size_t)N * 40 + 64);
    assert(src);
    static IrState st;
    for (int ring = 0; ring < 2; ring++) {
        char *w = src + sprintf(src, "manna v0;\n");
        for (int i = 0; i < N; i++) {
            if (i + 1 < N) w += sprintf(w, "fn f%d() { f%d(); }\n", i, i + 1);
            else if (ring) w += sprintf(w, "fn f%d() { f0(); }\n", i);
            else w += sprintf(w, "fn f%d() { }\n", i);
        }
        ir_state_init(&st, 32 << 20);
        parse_src(src);
        assert(g_ps.diags.len == 0 && g_items.len == N);
        IrModule *m = NULL;
        IrStatus r = ir_build(&st, &g_ps.diags, &g_items, &m);
        if (!ring) {
            assert(r == IR_BUILT && m->funcs.len == N);
        } else {
            assert(r == IR_ERRORS && st.diags.len == 1 && strcmp(st.diags.items[0].code, "E0235") == 0);
            assert(st.diags.items[0].span.start_line == 2 && st.diags.items[0].span.start_col == 11); /* f0's call `f1()` */
        }
        parse_done();
        ir_state_destroy(&st);
    }
    free(src);
    pass("call graph: a 20000-long chain builds (iterative), the same chain as a ring is one E0235 at f0's first call");
}

/* ---- structs at scale: nothing recursive over a chain of structs ---- */

/* A chain of 20000 structs, each holding the next (S0 holds S1 holds ...), and a ring
 * of 20000 (the last holds S0). The containment analysis must be iterative: a
 * recursive walk 20000 deep overflows the 2 MB stack this runs under in --stack mode
 * (the Makefile's 4x gate), and the chain is as long as the input allows, not as long
 * as any bound the parser sets. */
static void test_struct_chain_and_ring(void) {
    enum { N = 20000 };
    char *src = malloc((size_t)N * 48 + 64);
    assert(src);
    static IrState st;
    for (int ring = 0; ring < 2; ring++) {
        char *w = src + sprintf(src, "manna v0;\n");
        for (int i = 0; i < N; i++) {
            if (i + 1 < N) w += sprintf(w, "struct S%d { next: S%d, tag: u8, }\n", i, i + 1);
            else if (ring) w += sprintf(w, "struct S%d { next: S0, tag: u8, }\n", i);
            else w += sprintf(w, "struct S%d { v: u32, }\n", i);
        }
        ir_state_init(&st, 16 << 20);
        parse_src(src);
        assert(g_ps.diags.len == 0 && g_items.len == N);
        IrModule *m = NULL;
        if (!ring) {
            assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_BUILT && m->structs.len == N);
            /* S(N-1) = 4; each S(i) = its next, then a byte, padded to 4: 4 more each */
            assert(((const IrStruct *)m->structs.items[N - 1])->size == 4);
            const IrStruct *s0 = m->structs.items[0];
            assert(s0->size == 4u * N && s0->align == 4 && s0->fields[1].offset == 4u * (N - 1));
        } else {
            assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS && m == NULL);
            assert(st.diags.len == 1 && strcmp(st.diags.items[0].code, "E0221") == 0);
            assert(strcmp(st.diags.items[0].message, "struct 'S0' contains itself") == 0); /* the lowest index */
            assert(st.diags.items[0].span.start_line == 2);
        }
        parse_done();
        ir_state_destroy(&st);
    }
    free(src);
    pass("structs: a 20000-long chain lays out (iterative), a 20000-long ring is one E0221");
}

/* The deepest array nesting the parser accepts in a struct field resolves and lays out. */
static void test_deepest_array_type(void) {
    char *src = malloc(4096);
    assert(src);
    int accepted = 0;
    for (int k = 256; k >= 200 && !accepted; k--) {
        char *w = src + sprintf(src, "manna v0;\nstruct Deep { f: ");
        for (int i = 0; i < k; i++) w += sprintf(w, "[1]");
        sprintf(w, "u8, }\n");
        parse_src(src);
        if (g_ps.diags.len == 0) {
            accepted = k;
            static IrState st;
            ir_state_init(&st, 4 << 20);
            IrModule *m = NULL;
            assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_BUILT);
            const IrStruct *d = m->structs.items[0];
            assert(d->size == 1 && d->align == 1 && d->fields[0].type->kind == IR_T_ARRAY);
            ir_state_destroy(&st);
        }
        parse_done();
    }
    assert(accepted >= 250 && accepted <= 255);
    free(src);
    printf("PASS: arrays nested %d deep (the parser's limit in a field) resolve and lay out\n", accepted);
}

/* ---- stack: the real BUILDER on parser-built maximal trees (step 6) ---- */

/* The builder's recursion (check_group / require_group) runs once per tree level, and the
 * parser bounds the height to 512 (E0111). These shapes are generated as SOURCE, parsed
 * by the real parser and built by the real ir_build, at height 512 (the neg chain at the
 * most the nesting bound allows, E0103, near 255). A shape whose constructs a later step
 * adds is listed as pending, with the step, and becomes live then. Two variants each:
 * `int` (the expression is built against an integer return type: check_group) and `bool`
 * (the one-mismatch path: require_group). */
typedef struct { const char *name; int live_step; /* 0 = live now, else the step that makes it live */ } BuildShape;
static const BuildShape build_shapes[] = {
    {"binary", 0}, {"mixed_prec", 0}, {"neg_chain", 0}, {"in_ifs", 0}, {"name_chain", 0}, {"and_chain", 0},
    {"in_calls", 0}, {"field", 0}, {"in_construct", 0}, {"index", 0}, {"postfix_mix", 0}, {"in_index", 0}, {"in_array", 0}, {"in_convert", 0},
};
enum { N_BUILD_SHAPES = sizeof build_shapes / sizeof build_shapes[0] };

/* The shapes whose size is "the deepest the parser accepts" (height 0 in the harness): their depth is
 * bounded by the nesting bound (E0103), not by the height bound. `index` is one of them because its
 * base must be a nested ARRAY TYPE `[1][1]...u64` of the same depth, and a type nests only ~250 deep. */
static bool deepest_shape(const char *shape) {
    return strcmp(shape, "neg_chain") == 0 || strcmp(shape, "in_calls") == 0 || strcmp(shape, "in_construct") == 0 ||
           strcmp(shape, "index") == 0 || strcmp(shape, "in_index") == 0 || strcmp(shape, "in_array") == 0 || strcmp(shape, "in_convert") == 0;
}

/* Struct declarations a shape needs, malloc'd ("" for the others). `field`: s.f.f...f needs a
 * chain of distinct types (a struct cannot contain itself): N0 { f: N1 }, ..., the last { f: u64 }.
 * `in_construct`: one struct per nesting level, the same chain. k = the number of structs. */
static char *gen_decls(const char *shape, int height) {
    bool field = strcmp(shape, "field") == 0, construct = strcmp(shape, "in_construct") == 0, mix = strcmp(shape, "postfix_mix") == 0;
    int k = field ? (height > 2 ? height - 1 : 1) : construct ? height : mix ? (height > 3 ? (height - 1) / 2 : 1) : 0;
    char *out = malloc((size_t)k * 40 + 8);
    assert(out);
    char *w = out;
    *w = 0;
    for (int i = 0; i < k; i++) {
        if (mix) { /* each level an array of the next struct: a[0].f[0].f... */
            if (i + 1 < k) w += sprintf(w, "struct N%d { f: [1]N%d, }\n", i, i + 1);
            else w += sprintf(w, "struct N%d { f: u64, }\n", i);
        } else if (i + 1 < k) w += sprintf(w, "struct N%d { f: N%d, }\n", i, i + 1);
        else w += sprintf(w, "struct N%d { f: u64, }\n", i);
    }
    return out;
}

/* Source of the expression for `shape` with `height` levels (neg_chain: height = number of
 * minus signs). malloc'd. */
static char *gen_expr(const char *shape, int height) {
    char *out = malloc((size_t)height * 16 + 16); /* in_construct writes ~10 bytes per level */
    assert(out);
    char *w = out;
    if (strcmp(shape, "neg_chain") == 0) {
        for (int i = 0; i < height; i++) *w++ = '-';
        *w++ = '1';
    } else if (strcmp(shape, "mixed_prec") == 0) {
        w += sprintf(w, "1");
        for (int i = 0; i < height - 2; i++) w += sprintf(w, "+2*3"); /* tree height = adds + 2 */
    } else if (strcmp(shape, "in_calls") == 0) { /* g(g(g(...g(1)...))): `height` nested calls */
        for (int i = 0; i < height; i++) w += sprintf(w, "g(");
        w += sprintf(w, "1");
        for (int i = 0; i < height; i++) *w++ = ')';
    } else if (strcmp(shape, "field") == 0) { /* s.f.f.f...: `height` levels = the name and height-1 accesses */
        w += sprintf(w, height > 2 ? "s" : "x");
        if (height > 2) for (int i = 0; i < height - 1; i++) w += sprintf(w, ".f");
    } else if (strcmp(shape, "in_construct") == 0) { /* N0(f: N1(f: ...N_k(f: 1)...)): `height` constructions */
        for (int i = 0; i < height; i++) w += sprintf(w, "N%d(f: ", i);
        w += sprintf(w, "1");
        for (int i = 0; i < height; i++) *w++ = ')';
    } else if (strcmp(shape, "index") == 0) { /* a[0][0]...[0]: `height` indexings of a nested array */
        w += sprintf(w, "a");
        for (int i = 0; i < height; i++) w += sprintf(w, "[0]");
    } else if (strcmp(shape, "postfix_mix") == 0) { /* a[0].f[0].f...: height = 1 + 2 * pairs */
        int pairs = height > 3 ? (height - 1) / 2 : 1;
        w += sprintf(w, "a");
        for (int i = 0; i < pairs; i++) w += sprintf(w, "[0].f");
    } else if (strcmp(shape, "in_index") == 0) { /* a[a[a[...a[0]...]]]: `height` nested indexings */
        for (int i = 0; i < height; i++) w += sprintf(w, "a[");
        w += sprintf(w, "0");
        for (int i = 0; i < height; i++) *w++ = ']';
    } else if (strcmp(shape, "in_convert") == 0) { /* u64(u64(...u64(x)...)): `height` nested conversions */
        for (int i = 0; i < height; i++) w += sprintf(w, "u64(");
        w += sprintf(w, "x");
        for (int i = 0; i < height; i++) *w++ = ')';
    } else if (strcmp(shape, "in_array") == 0) { /* [[[...[x]...]]]: `height` nested array literals */
        for (int i = 0; i < height; i++) *w++ = '[';
        *w++ = 'x';
        for (int i = 0; i < height; i++) *w++ = ']';
    } else if (strcmp(shape, "name_chain") == 0) { /* x+1+1+... : a typed name under a literal chain */
        w += sprintf(w, "x");
        for (int i = 0; i < height - 1; i++) w += sprintf(w, "+1");
    } else if (strcmp(shape, "and_chain") == 0) { /* p&&p&&... : bool names */
        w += sprintf(w, "p");
        for (int i = 0; i < height - 1; i++) w += sprintf(w, "&&p");
    } else { /* binary */
        w += sprintf(w, "1");
        for (int i = 0; i < height - 1; i++) w += sprintf(w, "+1");
    }
    *w = 0;
    return out;
}

/* The whole program for one shape: the struct declarations it needs, `g`, and `f` returning
 * the expression. The `int` variant matches the expression's type; the `bool` variant
 * mismatches it (one E0223). */
static char *make_src(const char *shape, const char *expr, int levels, bool as_bool) {
    bool neg = strcmp(shape, "neg_chain") == 0, construct = strcmp(shape, "in_construct") == 0;
    bool is_and = strcmp(shape, "and_chain") == 0, field = strcmp(shape, "field") == 0;
    char *decls = gen_decls(shape, levels);
    char *src = malloc(strlen(expr) + strlen(decls) + 4096); /* the 120-deep wrapper alone is ~1.5 KB */
    assert(src);
    const char *rt = construct ? (as_bool ? "bool" : "N0") : is_and ? (as_bool ? "u64" : "bool") : (as_bool ? "bool" : (neg ? "i64" : "u64"));
    char extra_buf[2048], rt_buf[2048];
    const char *extra = field && levels > 2 ? ", s: N0" : "";
    if (strcmp(shape, "index") == 0) { /* a: [1][1]...[1]u64, `levels` deep */
        char *w = extra_buf + sprintf(extra_buf, ", a: ");
        for (int i = 0; i < levels; i++) w += sprintf(w, "[1]");
        sprintf(w, "u64");
        extra = extra_buf;
    } else if (strcmp(shape, "postfix_mix") == 0) {
        extra = ", a: [1]N0";
    } else if (strcmp(shape, "in_index") == 0) {
        extra = ", a: [1]u64";
    } else if (strcmp(shape, "in_array") == 0 && !as_bool) { /* the literal is [1][1]...[1]u64, `levels` deep */
        char *w = rt_buf;
        for (int i = 0; i < levels; i++) w += sprintf(w, "[1]");
        sprintf(w, "u64");
        rt = rt_buf;
    } else if (strcmp(shape, "in_array") == 0) {
        rt = "bool";
    }
    if (strcmp(shape, "in_ifs") == 0) { /* the chain inside 120 nested ifs (the parser allows 127 levels) */
        char *w = src + sprintf(src, "manna v0;\nfn g(a: u64) -> u64 { return a; }\nfn f(x: u64, p: bool) -> %s { ", rt);
        for (int i = 0; i < 120; i++) w += sprintf(w, "if true { ");
        w += sprintf(w, "return %s; ", expr);
        for (int i = 0; i < 120; i++) w += sprintf(w, "} ");
        /* the return inside the ifs is conditional, so the function also needs one at its end (E0277) */
        sprintf(w, "return %s; }\n", strcmp(rt, "bool") == 0 ? "true" : "0");
    } else {
        sprintf(src, "manna v0;\n%sfn g(a: u64) -> u64 { return a; }\nfn f(x: u64, p: bool%s) -> %s { return %s; }\n", decls, extra, rt, expr);
    }
    free(decls);
    return src;
}

/* Runs one shape: parse, then (unless parse_only) build. Returns 0 if everything came out as
 * expected; the caller treats any crash as a stack failure. */
static int run_build_shape(const char *shape, int height, bool as_bool, bool parse_only) {
    bool neg = strcmp(shape, "neg_chain") == 0;
    bool calls = strcmp(shape, "in_calls") == 0 || strcmp(shape, "in_construct") == 0 || strcmp(shape, "index") == 0 ||
                 strcmp(shape, "in_index") == 0 || strcmp(shape, "in_array") == 0 || strcmp(shape, "in_convert") == 0;
    char *expr = NULL;
    int levels = height;
    if ((neg || calls) && height == 0) { /* the deepest the nesting bound accepts */
        for (int k = 255; k >= 200 && !expr; k--) {
            char *e = gen_expr(shape, k);
            char *src = make_src(shape, e, k, false);
            parse_src_big_stack(src);
            bool ok = g_ps.diags.len == 0;
            parse_done();
            free(src);
            if (ok) { expr = e; levels = k; } else free(e);
        }
        assert(expr);
    } else {
        expr = gen_expr(shape, height);
    }
    char *src = make_src(shape, expr, levels, as_bool);
    printf("levels=%d\n", levels); /* neg_chain / in_calls: the deepest nesting the parser accepted */
    /* parse-only measures the PARSER on the process stack; otherwise the parse happens on a big-stack thread
     * so that what is measured afterwards is the builder alone */
    if (parse_only) parse_src(src); else parse_src_big_stack(src);
    assert(g_ps.diags.len == 0);
    if (!parse_only) {
        static IrState st;
        ir_state_init(&st, 16 << 20);
        IrModule *m = NULL;
        IrStatus r = ir_build(&st, &g_ps.diags, &g_items, &m);
        if (as_bool) {
            assert(r == IR_ERRORS && st.diags.len == 1 && strcmp(st.diags.items[0].code, "E0223") == 0);
        } else {
            assert(r == IR_BUILT);
        }
        ir_state_destroy(&st);
    }
    parse_done();
    free(src);
    free(expr);
    return 0;
}

static void test_builder_stack(void) {
    for (int i = 0; i < N_BUILD_SHAPES; i++) {
        if (build_shapes[i].live_step) {
            printf("PENDING: builder stack: %s (becomes live at step %d)\n", build_shapes[i].name, build_shapes[i].live_step);
            continue;
        }
        int height = deepest_shape(build_shapes[i].name) ? 0 : MAX_EXPR_HEIGHT;
        run_build_shape(build_shapes[i].name, height, false, false);
        run_build_shape(build_shapes[i].name, height, true, false);
        printf("PASS: builder stack: %s, built and mismatch-walked at the maximal height\n", build_shapes[i].name);
    }
    /* The `index` shape is bounded by how deep a TYPE nests (~255); the recursion itself is not. A 512-high index
     * chain over an unknown name recurses the full height through the same frames and reports one error. */
    char *src = malloc((size_t)MAX_EXPR_HEIGHT * 3 + 128);
    char *w = src + sprintf(src, "manna v0;\nfn f() -> u64 { return nope");
    for (int i = 0; i < MAX_EXPR_HEIGHT - 1; i++) w += sprintf(w, "[0]");
    sprintf(w, "; }\n");
    parse_src_big_stack(src);
    assert(g_ps.diags.len == 0);
    static IrState st;
    ir_state_init(&st, 16 << 20);
    IrModule *m = NULL;
    assert(ir_build(&st, &g_ps.diags, &g_items, &m) == IR_ERRORS && st.diags.len == 1 && strcmp(st.diags.items[0].code, "E0230") == 0);
    ir_state_destroy(&st);
    parse_done();
    free(src);
    printf("PASS: builder stack: a 512-high index chain over an unknown base recurses the full height, one error\n");
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0); /* a hang or abort must not swallow the PASS lines before it */
    if (argc == 2 && strcmp(argv[1], "--stack") == 0) { /* the Makefile's 2 MB (4x) gate */
        test_stack_shapes();
        test_struct_chain_and_ring();
        test_call_graph_scale();
        test_deepest_array_type();
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--build-stack") == 0) { /* the Makefile's 2 MB (4x) gate */
        test_builder_stack();
        return 0;
    }
    /* tools/measure_ir_build_stack.sh: --build-shape NAME HEIGHT int|bool [parse-only] */
    if (argc >= 5 && strcmp(argv[1], "--build-shape") == 0) {
        return run_build_shape(argv[2], atoi(argv[3]), strcmp(argv[4], "bool") == 0, argc > 5);
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
    test_muted_target();
    test_build_e0201_and_reuse();
    test_e0201_in_name_table();
    test_decls_many_names();
    test_literal_matrix();
    test_min_representation();
    test_type_mismatch();
    test_group_representation();
    test_operator_matrix();
    test_operator_result_types();
    test_e0261_fix();
    test_construction_and_access_spans();
    test_array_spans();
    test_len_spans();
    test_convert_spans();
    test_diag_fix_edit_list();
    test_naming_predicates_and_converter();
    test_naming_errors();
    test_rename_fixes();
    test_rename_edit_order_and_dedupe();
    test_array_checker_is_linear();
    test_checker_is_linear();
    test_call_ir();
    test_call_graph_scale();
    test_scope_api();
    test_scopes_through_the_builder();
    test_signatures();
    test_duplicate_attribute_fix();
    test_struct_chain_and_ring();
    test_deepest_array_type();
    test_builder_stack();
    test_stack_shapes();
    return 0;
}
