/* POSIX feature macro first (the strict-header lesson of ir_test.c). */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ir.h"
#include "ir_internal.h"
#include "lexer.h"
#include "parser.h"
#include "parser_state.h"

#ifndef MANNA_COUNT_FIELD_PROBES
#error "ir_field_lookup_test.c needs -DMANNA_COUNT_FIELD_PROBES (and every IR source built with it)"
#endif

/* Register C3: a field is found by name in O(log n) comparisons, not by a scan. No wall-clock time anywhere: a counter built into the lookup (ir.h's
 * MANNA_COUNT_FIELD_PROBES, absent from normal builds) counts the name comparisons ir_field_find makes, and the test asserts the most any ONE lookup made is at most
 * ceil(log2 N) + 1, and that the number of lookups is exactly the number the program makes (so a lookup that silently went elsewhere fails the test). Structs of
 * 1000, 2000, 4000 and 8000 fields; fields are declared in a scrambled order, so the order of the declarations is not what makes a search short.
 *   A: every field accessed once (N lookups).
 *   B: a construction of every field, written in REVERSE order (N lookups; one E0245).
 *   C: the rename collision check, one lookup per misnamed field. The build stops at the 20th error (E0203), so at most 19 fields can be misnamed in one build.
 *   D: unknown names (E0242), also at most 19 in one build. */

static ParserState g_ps;
static NodeList g_items;

typedef struct { char *p; size_t len, cap; } Buf;

static void put(Buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#include <stdarg.h>
static void put(Buf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char tmp[256];
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    assert(n > 0 && (size_t)n < sizeof tmp);
    if (b->len + (size_t)n + 1 > b->cap) {
        b->cap = (b->len + (size_t)n + 1) * 2;
        b->p = realloc(b->p, b->cap);
        assert(b->p);
    }
    memcpy(b->p + b->len, tmp, (size_t)n + 1);
    b->len += (size_t)n;
}

static unsigned ceil_log2(unsigned n) {
    unsigned k = 0;
    while ((1u << k) < n) k++;
    return k;
}

/* What the build of one source did. */
typedef struct {
    IrStatus status;
    size_t   ndiags;
    char     first_code[8];
    unsigned long lookups, probes, probe_max;
} Run;

static Run run(const char *src) {
    static IrState st;
    parser_state_init(&g_ps, 16 * 1024 * 1024);
    node_list_init(&g_items);
    if (setjmp(g_ps.fatal_target) == 0) {
        Lexer lx;
        lexer_init(&lx, src, strlen(src));
        (void)parser_parse_file(&g_ps, &lx, &g_items);
    }
    if (g_ps.diags.len) fprintf(stderr, "parse: %s %s\n", g_ps.diags.items[0].code, g_ps.diags.items[0].message);
    assert(g_ps.diags.len == 0); /* the generated source is parsed clean: the IR stage runs */
    ir_state_init(&st, IR_ARENA_CAP);
    ir_field_lookups = ir_field_probes = ir_field_probe_max = 0;
    IrModule *m = NULL;
    Run r;
    r.status = ir_build(&st, &g_ps.diags, &g_items, &m);
    r.lookups = ir_field_lookups;
    r.probes = ir_field_probes;
    r.probe_max = ir_field_probe_max;
    r.ndiags = st.diags.len;
    r.first_code[0] = 0;
    if (st.diags.len) snprintf(r.first_code, sizeof r.first_code, "%s", st.diags.items[0].code);
    ir_state_destroy(&st);
    parser_state_destroy(&g_ps);
    return r;
}

/* The k-th field to declare, 0 <= k < n: a permutation of 0..n-1 (7919 is prime and larger than no n tested, so coprime to each). */
static unsigned scramble(unsigned k, unsigned n) {
    return (unsigned)(((unsigned long)k * 7919u) % n);
}

static void declare(Buf *b, unsigned n, unsigned misnamed) {
    put(b, "manna v0;\nstruct S {\n");
    for (unsigned k = 0; k < n; k++) {
        unsigned id = scramble(k, n);
        const char *ty = id % 2 ? "u16" : "u8"; /* two types, so a lookup that returned the wrong field is a type error below */
        if (id < misnamed) put(b, "    aB%05u: %s,\n", id, ty); /* not snake_case: E0280; the converted name is a_b%05u */
        else put(b, "    f%05u: %s,\n", id, ty);
    }
    put(b, "}\n");
}

static void check(const char *what, unsigned n, unsigned long expect_lookups, const Run *r) {
    unsigned bound = ceil_log2(n) + 1;
    printf("  N=%-5u %-28s lookups %-5lu comparisons %-7lu (%.2f per lookup) worst single lookup %lu, bound %u\n", n, what, r->lookups, r->probes,
           r->lookups ? (double)r->probes / (double)r->lookups : 0.0, r->probe_max, bound);
    assert(r->lookups == expect_lookups);
    assert(r->probe_max <= bound);
    assert(r->probes <= r->lookups * bound); /* the same bound, summed: a count that missed a lookup's maximum cannot hide it */
}

static void test_size(unsigned n) {
    Buf b = {0};
    Run r;

    /* A: access every field */
    declare(&b, n, 0);
    put(&b, "fn h8(x: u8) { }\nfn h16(x: u16) { }\nfn g(s: S) {\n");
    for (unsigned id = 0; id < n; id++) put(&b, "    h%s(s.f%05u);\n", id % 2 ? "16" : "8", id); /* the right field has the right type */
    put(&b, "}\n");
    r = run(b.p);
    assert(r.status == IR_BUILT && r.ndiags == 0);
    check("access every field", n, n, &r);
    b.len = 0;

    /* B: construct in reverse order */
    declare(&b, n, 0);
    put(&b, "fn mk(v8: u8, v16: u16) -> S {\n    return S(\n");
    for (unsigned id = n; id-- > 0;) put(&b, "        f%05u: v%s%s\n", id, id % 2 ? "16" : "8", id ? "," : ""); /* a wrong field would be E0223 */
    put(&b, "    );\n}\n");
    r = run(b.p);
    assert(r.status == IR_ERRORS && r.ndiags == 1 && strcmp(r.first_code, "E0245") == 0);
    check("construct in reverse order", n, n, &r);
    b.len = 0;

    /* C: the rename collision check */
    declare(&b, n, 19);
    r = run(b.p);
    assert(r.status == IR_ERRORS && r.ndiags == 19 && strcmp(r.first_code, "E0280") == 0);
    check("rename collision check", n, 19, &r);
    b.len = 0;

    /* D: unknown names */
    declare(&b, n, 0);
    put(&b, "fn g(s: S) {\n");
    for (unsigned i = 0; i < 19; i++) put(&b, "    s.zz%u;\n", i);
    put(&b, "}\n");
    r = run(b.p);
    assert(r.status == IR_ERRORS && r.ndiags == 19 && strcmp(r.first_code, "E0242") == 0);
    check("unknown name (E0242)", n, 19, &r);
    b.len = 0;
    free(b.p);
}

/* A repeated field is not kept (E0211), so it is not in the index either, and a name can only ever find the first declaration. The case cannot be observed through a
 * lookup: E0211 is a declaration error, which stops the build before any body is read (tests/ir/dup_field.mn, dup_field_is_not_a_field.mn pin the rest). Pinned here: the
 * build is E0211 only and makes no lookup at all. */
static void test_repeat_makes_no_lookup(void) {
    Buf b = {0};
    put(&b, "manna v0;\nstruct R { a: u8, b: u16, a: u32, }\nfn f(r: R) -> u8 { return r.a; }\n");
    Run r = run(b.p);
    assert(r.status == IR_ERRORS && r.ndiags == 1 && strcmp(r.first_code, "E0211") == 0 && r.lookups == 0);
    free(b.p);
    printf("  a repeated field: E0211 stops the bodies, no lookup is made\n");
}

int main(void) {
    printf("field lookup (C3): comparisons per lookup, no wall-clock time\n");
    test_repeat_makes_no_lookup();
    static const unsigned sizes[] = {1000, 2000, 4000, 8000};
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) test_size(sizes[i]);
    printf("PASS: field lookup is O(log n) comparisons at 1000, 2000, 4000 and 8000 fields\n");
    return 0;
}
