#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <assert.h>
#include "parser_state.h"

static Span mk(int sl, int sc, int el, int ec) {
    Span s = { sl, sc, el, ec };
    return s;
}

static void test_diag_list_and_dedicated_messages(void) {
    Arena *a = arena_create(1 << 16);
    DiagList list;
    diag_list_init(&list);

    diag_add(a, &list, diag_chained_comparison(mk(1, 5, 1, 6)));
    diag_add(a, &list, diag_assignment_as_condition(a, mk(2, 10, 2, 11), "'{'"));

    assert(list.len == 2);
    assert(strcmp(list.items[0].code, "E0101") == 0);
    assert(strcmp(list.items[0].message, "comparisons can't be chained; use a < b && b < c") == 0);
    assert(list.items[0].fix == NULL);

    assert(strcmp(list.items[1].code, "E0102") == 0);
    assert(strcmp(list.items[1].message, "assignment is a statement; did you mean '=='?") == 0);
    assert(list.items[1].fix != NULL);
    assert(list.items[1].fix->n_edits == 1);
    assert(strcmp(list.items[1].fix->edits[0].text, "==") == 0);
    assert(list.items[1].expected && strcmp(list.items[1].expected, "'{'") == 0);
    assert(list.items[1].found && strcmp(list.items[1].found, "'='") == 0);

    arena_destroy(a);
    printf("PASS: diag list accumulates; both dedicated messages correct\n");
}

static void test_depth_bound(void) {
    /* static: read after the longjmp (C11 7.13.2.1; see parser_main.c). */
    static ParserState ps;
    parser_state_init(&ps, 1 << 20);

    if (setjmp(ps.fatal_target) != 0) {
        assert(ps.diags.len == 1);
        assert(strcmp(ps.diags.items[0].code, "E0103") == 0);
        assert(strcmp(ps.diags.items[0].message, "maximum nesting depth (256 units) exceeded") == 0);
        printf("PASS: depth bound - 257th depth_enter longjmps with the right diagnostic\n");
        parser_state_destroy(&ps);
        return;
    }

    for (int i = 0; i < MAX_NESTING_DEPTH; i++) {
        depth_enter(&ps, mk(1, 1, 1, 1));
    }
    assert(ps.depth == MAX_NESTING_DEPTH);
    for (int i = 0; i < MAX_NESTING_DEPTH; i++) {
        depth_exit(&ps);
    }
    assert(ps.depth == 0);

    /* Now actually exceed it - this call must longjmp, never return. */
    for (int i = 0; i < MAX_NESTING_DEPTH + 1; i++) {
        depth_enter(&ps, mk(1, 1, 1, 1));
    }
    fprintf(stderr, "FAIL: depth_enter did not longjmp past MAX_NESTING_DEPTH\n");
    exit(1);
}

static void test_error_cap(void) {
    /* static: read after the longjmp (C11 7.13.2.1; see parser_main.c). */
    static ParserState ps;
    parser_state_init(&ps, 1 << 20);

    if (setjmp(ps.fatal_target) != 0) {
        assert(ps.diags.len == MAX_ERRORS + 1); /* MAX_ERRORS real ones + the cap message */
        assert(strcmp(ps.diags.items[ps.diags.len - 1].code, "E0104") == 0);
        assert(strcmp(ps.diags.items[ps.diags.len - 1].message, "too many errors, stopping") == 0);
        printf("PASS: error cap - the %dth error longjmps with a terminal diagnostic appended\n", MAX_ERRORS);
        parser_state_destroy(&ps);
        return;
    }

    for (int i = 0; i < MAX_ERRORS; i++) {
        parser_error(&ps, diag_chained_comparison(mk(i + 1, 1, i + 1, 2)));
    }
    fprintf(stderr, "FAIL: parser_error did not longjmp at MAX_ERRORS\n");
    exit(1);
}

static void test_arena_overflow_shares_fatal_target(void) {
    /* Proves the unification: a ParserState's arena overflow routes
     * through the SAME fatal_target as depth/error-cap above, not a
     * separate mechanism a caller would need to handle differently - and
     * since the v0 audit fix (finding 2) it leaves E0110 behind, like the
     * other two. */
    static ParserState ps; /* read after the longjmp (C11 7.13.2.1) */
    parser_state_init(&ps, 64 * 1024); /* reserve = a quarter: room for the diagnostic */

    if (setjmp(ps.fatal_target) != 0) {
        assert(ps.diags.len == 1);
        assert(strcmp(ps.diags.items[0].code, "E0110") == 0);
        printf("PASS: arena overflow longjmps to the same ParserState.fatal_target, leaving E0110\n");
        parser_state_destroy(&ps);
        return;
    }

    arena_alloc(ps.arena, 100 * 1024, 1); /* exceeds the 64 KiB cap */
    fprintf(stderr, "FAIL: arena_alloc did not longjmp on overflow\n");
    exit(1);
}

static void test_arena_overflow_too_small_for_diagnostic(void) {
    /* An arena too small to hold even the E0110 diagnostic (its reserved
     * tail is a quarter of 64 bytes): the hook itself overflows, and the
     * unwind still reaches fatal_target - nothing recorded, no crash, no
     * second call of the hook. */
    static ParserState ps;
    parser_state_init(&ps, 64);

    if (setjmp(ps.fatal_target) != 0) {
        assert(ps.diags.len == 0);
        printf("PASS: arena too small for the diagnostic still unwinds cleanly to fatal_target\n");
        parser_state_destroy(&ps);
        return;
    }

    arena_alloc(ps.arena, 128, 1); /* exceeds the 64-byte cap */
    fprintf(stderr, "FAIL: arena_alloc did not longjmp on overflow\n");
    exit(1);
}

int main(void) {
    test_diag_list_and_dedicated_messages();
    test_depth_bound();
    test_error_cap();
    test_arena_overflow_shares_fatal_target();
    test_arena_overflow_too_small_for_diagnostic();
    printf("ALL PASS\n");
    return 0;
}
