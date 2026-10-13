/* POSIX feature macro first: tmpfile/ftell are C11, but keep the strict-header
 * lesson of the parser's G6 (glibc -std=c11 hides POSIX declarations). */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ir.h"

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

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0); /* a hang or abort must not swallow the PASS lines before it */
    test_primitives();
    test_composites();
    test_table_growth();
    test_e0203();
    test_e0201_and_reuse();
    test_reset_primitives();
    return 0;
}
