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
    Field *param_a = field_new(a, mk(2, 8, 2, 17), "a", 1, type_a);

    Type *type_b = type_ident(a, mk(2, 25, 2, 28), "u32", 3);
    Field *param_b = field_new(a, mk(2, 19, 2, 28), "b", 1, type_b);

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

    Decl *fn = decl_function(a, mk(2, 1, 5, 1), attributes, "add", 3, params, return_type, block);

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
    Field *field_a = field_new(a, mk(25, 15, 25, 20), "a", 1, u8);
    Type *u16 = type_ident(a, mk(25, 25, 25, 28), "u16", 3);
    Field *field_b = field_new(a, mk(25, 22, 25, 28), "b", 1, u16);

    NodeList fields;
    node_list_init(&fields);
    node_list_push(a, &fields, field_a);
    node_list_push(a, &fields, field_b);

    Decl *st = decl_struct(a, mk(25, 5, 25, 32), "Foo", 3, fields);

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
    node_list_push(a, &attrs, (void *)"kernel_hook");

    Type *u64 = type_ident(a, mk(24, 18, 24, 21), "u64", 3);
    Field *p_addr = field_new(a, mk(24, 13, 24, 21), "addr", 4, u64);
    Type *u8b = type_ident(a, mk(24, 28, 24, 30), "u8", 2);
    Field *p_val = field_new(a, mk(24, 24, 24, 30), "val", 3, u8b);
    NodeList params2;
    node_list_init(&params2);
    node_list_push(a, &params2, p_addr);
    node_list_push(a, &params2, p_val);

    Expr *one = expr_number(a, mk(24, 43, 24, 44), 1);
    Stmt *let_x = stmt_let(a, mk(24, 35, 24, 45), "x", 1, NULL, one);
    NodeList body_stmts;
    node_list_init(&body_stmts);
    node_list_push(a, &body_stmts, let_x);
    Stmt *body = stmt_block(a, mk(24, 33, 24, 47), body_stmts);

    Decl *fn2 = decl_function(a, mk(23, 1, 24, 47), attrs, "raw_poke", 8, params2, NULL, body);

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
        "  (return_type unit)\n"
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

int main(void) {
    test_basic_function_matches_spec();
    test_struct_and_attributed_function();
    printf("ALL PASS\n");
    return 0;
}
