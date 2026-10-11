/* fmemopen is POSIX.1-2008, not ISO C: under glibc with -std=c11 nothing
 * declares it unless this is defined before the first include (clang: a
 * compile error; gcc: an implicit declaration returning int, which truncates
 * the FILE * and crashes). macOS declares it regardless, which is why this
 * went unnoticed. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arena.h"
#include "ast.h"

static Span mk(int sl, int sc, int el, int ec) {
    Span s = { sl, sc, el, ec };
    return s;
}

/* Hand-constructs the exact tree for docs/M2_PLAN.md's documented example
 * (fn add(a: u32, b: u32) -> u32 { return a + b; }) and asserts the
 * pretty-printer's output matches the spec's own S-expression dump
 * byte-for-byte - the spec and the implementation checked against each
 * other directly, not just "looks plausible." */
static void test_basic_function_matches_spec(void) {
    Arena *a = arena_create(1 << 20);

    Type *type_a = type_ident(a, mk(2, 14, 2, 17), "u32", 3);
    Field *param_a = field_new(a, mk(2, 8, 2, 17), mk(2, 8, 2, 9), "a", 1, type_a);

    Type *type_b = type_ident(a, mk(2, 25, 2, 28), "u32", 3);
    Field *param_b = field_new(a, mk(2, 19, 2, 28), mk(2, 19, 2, 20), "b", 1, type_b);

    NodeList params;
    node_list_init(&params);
    node_list_push(a, &params, param_a);
    node_list_push(a, &params, param_b);

    Type *return_type = type_ident(a, mk(2, 33, 2, 36), "u32", 3);

    Expr *ident_a = expr_ident(a, mk(3, 12, 3, 13), "a", 1);
    Expr *ident_b = expr_ident(a, mk(3, 16, 3, 17), "b", 1);
    Expr *sum = expr_binary(a, mk(3, 12, 3, 17), BINOP_ADD, ident_a, ident_b);
    Stmt *ret = stmt_return(a, mk(3, 5, 3, 14), sum);

    NodeList block_stmts;
    node_list_init(&block_stmts);
    node_list_push(a, &block_stmts, ret);
    Stmt *block = stmt_block(a, mk(2, 31, 5, 1), block_stmts);

    NodeList attributes;
    node_list_init(&attributes);

    Decl *fn = decl_function(a, mk(2, 1, 5, 1), attributes, mk(2, 4, 2, 7), "add", 3, params, return_type, block);

    char buf[4096];
    FILE *mem = fmemopen(buf, sizeof(buf), "w");
    decl_print(mem, fn);
    fclose(mem);

    const char *expected =
        "(function_decl@2:1-5:1 add\n"
        "  (params\n"
        "    (param@2:8-2:17 a u32)\n"
        "    (param@2:19-2:28 b u32))\n"
        "  (return_type u32)\n"
        "  (block@2:31-5:1\n"
        "    (return_stmt@3:5-3:14\n"
        "      (binary@3:12-3:17 +\n"
        "        (ident@3:12-3:13 a)\n"
        "        (ident@3:16-3:17 b)))))";

    if (strcmp(buf, expected) != 0) {
        fprintf(stderr, "FAIL: output does not match docs/M2_PLAN.md's documented example\n");
        fprintf(stderr, "--- expected ---\n%s\n--- actual ---\n%s\n", expected, buf);
        exit(1);
    }
    printf("PASS: matches docs/M2_PLAN.md's documented example byte-for-byte\n");

    arena_destroy(a);
}

/* A second case exercising what the first one doesn't: an attributed
 * function (the [kernel_hook] case), a struct with fields, and a let
 * with no explicit type (inferred - the common case). */
static void test_struct_and_attributed_function(void) {
    Arena *a = arena_create(1 << 20);

    Type *u8 = type_ident(a, mk(25, 18, 25, 20), "u8", 2);
    Field *field_a = field_new(a, mk(25, 15, 25, 20), mk(25, 15, 25, 16), "a", 1, u8);
    Type *u16 = type_ident(a, mk(25, 25, 25, 28), "u16", 3);
    Field *field_b = field_new(a, mk(25, 22, 25, 28), mk(25, 22, 25, 23), "b", 1, u16);

    NodeList fields;
    node_list_init(&fields);
    node_list_push(a, &fields, field_a);
    node_list_push(a, &fields, field_b);

    Decl *st = decl_struct(a, mk(25, 5, 25, 32), mk(25, 12, 25, 15), "Foo", 3, fields);

    char buf[4096];
    FILE *mem = fmemopen(buf, sizeof(buf), "w");
    decl_print(mem, st);
    fclose(mem);
    const char *expected_struct =
        "(struct_decl@25:5-25:32 Foo\n"
        "  (fields\n"
        "    (field@25:15-25:20 a u8)\n"
        "    (field@25:22-25:28 b u16)))";
    if (strcmp(buf, expected_struct) != 0) {
        fprintf(stderr, "FAIL: struct_decl mismatch\n--- expected ---\n%s\n--- actual ---\n%s\n",
                expected_struct, buf);
        exit(1);
    }
    printf("PASS: struct_decl with fields\n");

    /* [kernel_hook] fn raw_poke(addr: u64, val: u8) { let x = 1; } */
    NodeList attrs;
    node_list_init(&attrs);
    node_list_push(a, &attrs, attribute_new(a, mk(23, 1, 23, 14), mk(23, 2, 23, 13), "kernel_hook", 11));

    Type *u64 = type_ident(a, mk(24, 18, 24, 21), "u64", 3);
    Field *p_addr = field_new(a, mk(24, 13, 24, 21), mk(24, 13, 24, 17), "addr", 4, u64);
    Type *u8b = type_ident(a, mk(24, 28, 24, 30), "u8", 2);
    Field *p_val = field_new(a, mk(24, 24, 24, 30), mk(24, 24, 24, 27), "val", 3, u8b);
    NodeList params2;
    node_list_init(&params2);
    node_list_push(a, &params2, p_addr);
    node_list_push(a, &params2, p_val);

    Expr *one = expr_number(a, mk(24, 43, 24, 44), 1);
    Stmt *let_x = stmt_let(a, mk(24, 35, 24, 45), mk(24, 39, 24, 40), "x", 1, NULL, one);
    NodeList body_stmts;
    node_list_init(&body_stmts);
    node_list_push(a, &body_stmts, let_x);
    Stmt *body = stmt_block(a, mk(24, 33, 24, 47), body_stmts);

    Decl *fn2 = decl_function(a, mk(23, 1, 24, 47), attrs, mk(24, 4, 24, 12), "raw_poke", 8, params2, NULL, body);

    char buf2[4096];
    FILE *mem2 = fmemopen(buf2, sizeof(buf2), "w");
    decl_print(mem2, fn2);
    fclose(mem2);
    const char *expected_fn2 =
        "(function_decl@23:1-24:47 raw_poke\n"
        "  (attributes kernel_hook)\n"
        "  (params\n"
        "    (param@24:13-24:21 addr u64)\n"
        "    (param@24:24-24:30 val u8))\n"
        "  (return_type)\n"
        "  (block@24:33-24:47\n"
        "    (let_stmt@24:35-24:45 x\n"
        "      (number@24:43-24:44 1))))";
    if (strcmp(buf2, expected_fn2) != 0) {
        fprintf(stderr, "FAIL: attributed function mismatch\n--- expected ---\n%s\n--- actual ---\n%s\n",
                expected_fn2, buf2);
        exit(1);
    }
    printf("PASS: [kernel_hook] function, unit return, inferred-type let\n");

    arena_destroy(a);
}

/* #4 step 2: the convert node renders as (convert@span TARGET operand) - the
 * dump shape decided in the Phase B review. */
static void test_convert_node_dump(void) {
    Arena *a = arena_create(1 << 20);
    Expr *x = expr_ident(a, mk(1, 5, 1, 6), "x", 1);
    Expr *inner = expr_convert(a, mk(1, 5, 1, 12), mk(1, 5, 1, 8), "u32", 3, x);
    Expr *outer = expr_convert(a, mk(1, 1, 1, 13), mk(1, 1, 1, 4), "u64", 3, inner);

    char buf[1024];
    FILE *mem = fmemopen(buf, sizeof(buf), "w");
    expr_print(mem, outer, 0);
    fclose(mem);
    const char *expected =
        "(convert@1:1-1:13 u64\n"
        "  (convert@1:5-1:12 u32\n"
        "    (ident@1:5-1:6 x)))";
    if (strcmp(buf, expected) != 0) {
        fprintf(stderr, "FAIL: convert dump mismatch\n--- expected ---\n%s\n--- actual ---\n%s\n", expected, buf);
        exit(1);
    }
    printf("PASS: convert node renders as (convert@span TARGET operand), nested\n");
    arena_destroy(a);
}

/* #4 step 3: (array@span e...) and (array_repeat@span N value). The count is
 * a scalar attribute, so it sits in the header like a unary's operator or a
 * convert's target; the value is the one child. */
static void test_array_node_dumps(void) {
    Arena *a = arena_create(1 << 20);
    Expr *one = expr_number(a, mk(1, 2, 1, 3), 1);
    Expr *two = expr_number(a, mk(1, 5, 1, 6), 2);
    NodeList elems;
    node_list_init(&elems);
    node_list_push(a, &elems, one);
    node_list_push(a, &elems, two);
    Expr *arr = expr_array(a, mk(1, 1, 1, 7), elems);
    Expr *v = expr_ident(a, mk(2, 2, 2, 3), "v", 1);
    Expr *rep = expr_array_repeat(a, mk(2, 1, 2, 9), v, 16);

    char buf[1024];
    FILE *mem = fmemopen(buf, sizeof(buf), "w");
    expr_print(mem, arr, 0);
    fputc('|', mem);
    expr_print(mem, rep, 0);
    fclose(mem);
    const char *expected =
        "(array@1:1-1:7\n"
        "  (number@1:2-1:3 1)\n"
        "  (number@1:5-1:6 2))|"
        "(array_repeat@2:1-2:9 16\n"
        "  (ident@2:2-2:3 v))";
    if (strcmp(buf, expected) != 0) {
        fprintf(stderr, "FAIL: array dump mismatch\n--- expected ---\n%s\n--- actual ---\n%s\n", expected, buf);
        exit(1);
    }
    printf("PASS: array and array_repeat nodes render as (array@span e...) and (array_repeat@span N value)\n");
    arena_destroy(a);
}

/* #4 step 4: (named_arg@span name_expr value) - both children, in order. */
static void test_named_arg_dump(void) {
    Arena *a = arena_create(1 << 20);
    Expr *name = expr_ident(a, mk(1, 7, 1, 8), "x", 1);
    Expr *value = expr_number(a, mk(1, 10, 1, 11), 1);
    Expr *na = expr_named_arg(a, mk(1, 7, 1, 11), name, value);
    char buf[512];
    FILE *mem = fmemopen(buf, sizeof(buf), "w");
    expr_print(mem, na, 0);
    fclose(mem);
    const char *expected =
        "(named_arg@1:7-1:11\n"
        "  (ident@1:7-1:8 x)\n"
        "  (number@1:10-1:11 1))";
    if (strcmp(buf, expected) != 0) {
        fprintf(stderr, "FAIL: named_arg dump mismatch\n--- expected ---\n%s\n--- actual ---\n%s\n", expected, buf);
        exit(1);
    }
    printf("PASS: named_arg renders as (named_arg@span name_expr value)\n");
    arena_destroy(a);
}


/* typed-IR step 2b: Expr.height. The field must sit in existing padding -
 * sizeof(Expr) was 72 after step 2a (name spans widened the union) and the
 * E0110 location pins in parser_fixes_test.c depend on it, so adding height
 * must not move it. The compile-time check is 64-bit only (LP64 layout). */
#if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFu
_Static_assert(sizeof(Expr) == 72, "Expr.height must fit in padding: sizeof(Expr) must stay 72");
#endif

static void test_expr_height(void) {
    Arena *a = arena_create(1 << 24);
    Expr *one = expr_number(a, mk(1, 1, 1, 2), 1);
    assert(one->height == 1);
    Expr *id = expr_ident(a, mk(1, 1, 1, 2), "x", 1);
    assert(id->height == 1 && expr_bool(a, mk(1, 1, 1, 5), true)->height == 1);

    /* 1 + 1 + 1 : left chain, height 3; the tallest child decides. */
    Expr *sum = expr_binary(a, mk(1, 1, 1, 6), BINOP_ADD, one, one);
    Expr *sum3 = expr_binary(a, mk(1, 1, 1, 10), BINOP_ADD, sum, one);
    assert(sum->height == 2 && sum3->height == 3);
    Expr *wide = expr_binary(a, mk(1, 1, 1, 10), BINOP_ADD, one, sum3); /* rhs taller */
    assert(wide->height == 4);

    /* every other node kind: 1 + max(children) */
    assert(expr_unary(a, mk(1, 1, 1, 3), UNOP_NEG, sum3)->height == 4);
    assert(expr_convert(a, mk(1, 1, 1, 9), mk(1, 1, 1, 4), "u64", 3, sum3)->height == 4);
    assert(expr_field_access(a, mk(1, 1, 1, 9), sum3, mk(1, 8, 1, 9), "f", 1)->height == 4);
    assert(expr_index(a, mk(1, 1, 1, 9), one, sum3)->height == 4);
    assert(expr_slice(a, mk(1, 1, 1, 9), one, sum, sum3)->height == 4);
    assert(expr_array_repeat(a, mk(1, 1, 1, 9), sum3, 4)->height == 4);
    assert(expr_named_arg(a, mk(1, 1, 1, 9), id, sum3)->height == 4);
    NodeList args;
    node_list_init(&args);
    assert(expr_call(a, mk(1, 1, 1, 4), mk(1, 1, 1, 2), "f", 1, args)->height == 1 + 0); /* no args: 1 + max() = 1 */
    node_list_push(a, &args, one);
    node_list_push(a, &args, sum3);
    node_list_push(a, &args, sum);
    assert(expr_call(a, mk(1, 1, 1, 9), mk(1, 1, 1, 2), "f", 1, args)->height == 4);
    assert(expr_array(a, mk(1, 1, 1, 9), args)->height == 4);

    /* Saturation: a tree built by the constructors WITHOUT the parser's E0111
     * guard (a future AST reader, a test) must never wrap to a small height.
     * 70,000 levels built iteratively (no recursion) pins UINT16_MAX. */
    Expr *tall = one;
    for (int i = 0; i < 70000; i++) tall = expr_unary(a, mk(1, 1, 1, 2), UNOP_NEG, tall);
    assert(tall->height == UINT16_MAX);
    arena_destroy(a);
    printf("PASS: Expr.height - leaf 1, 1 + tallest child for every node kind, saturates at UINT16_MAX; sizeof(Expr) == %zu\n", sizeof(Expr));
}

int main(void) {
    test_expr_height();
    test_named_arg_dump();
    test_array_node_dumps();
    test_convert_node_dump();
    test_basic_function_matches_spec();
    test_struct_and_attributed_function();
    printf("ALL PASS\n");
    return 0;
}
