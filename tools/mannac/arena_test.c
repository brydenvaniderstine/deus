#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <assert.h>
#include "arena.h"

static void test_normal_allocation(void) {
    Arena *a = arena_create(1024);
    jmp_buf env;
    if (setjmp(env) != 0) {
        fprintf(stderr, "FAIL: unexpected overflow in test_normal_allocation\n");
        exit(1);
    }
    arena_set_overflow_target(a, &env);

    int *x = arena_alloc(a, sizeof(int), sizeof(int));
    int *y = arena_alloc(a, sizeof(int), sizeof(int));
    *x = 111;
    *y = 222;
    assert(*x == 111 && *y == 222);
    assert(arena_used(a) >= 2 * sizeof(int));

    arena_destroy(a);
    printf("PASS: normal allocation\n");
}

static void test_alignment(void) {
    Arena *a = arena_create(1024);
    jmp_buf env;
    if (setjmp(env) != 0) {
        fprintf(stderr, "FAIL: unexpected overflow in test_alignment\n");
        exit(1);
    }
    arena_set_overflow_target(a, &env);

    char *c = arena_alloc(a, 1, 1);
    long *l = arena_alloc(a, sizeof(long), sizeof(long));
    (void)c;
    assert(((size_t)l % sizeof(long)) == 0);
    *l = 0x1122334455667788L;
    assert(*l == 0x1122334455667788L);

    arena_destroy(a);
    printf("PASS: alignment respected\n");
}

/* Set only if control reaches the line right after the cap-exceeding
 * call - which must never happen, since that call is supposed to
 * longjmp away instead of returning. */
static volatile int reached_after_overflow_call = 0;

static void test_cap_hit(void) {
    Arena *a = arena_create(64);
    jmp_buf env;
    if (setjmp(env) != 0) {
        assert(reached_after_overflow_call == 0);
        printf("PASS: cap hit -> clean longjmp, the over-cap call never returned\n");
        arena_destroy(a);
        return;
    }
    arena_set_overflow_target(a, &env);

    void *p1 = arena_alloc(a, 32, 1); /* within the 64-byte cap */
    assert(p1 != NULL);

    arena_alloc(a, 64, 1); /* 32 already used + 64 more blows the cap */
    reached_after_overflow_call = 1; /* must NEVER execute */
    fprintf(stderr, "FAIL: reached code after an over-cap allocation\n");
    exit(1);
}

static void test_no_allocation_after_cap(void) {
    Arena *a = arena_create(64);
    jmp_buf env;
    static size_t used_before_overflow = 0;
    if (setjmp(env) != 0) {
        assert(arena_used(a) == used_before_overflow);
        printf("PASS: arena_used() unchanged by the failed (longjmp'd) allocation\n");
        arena_destroy(a);
        return;
    }
    arena_set_overflow_target(a, &env);

    arena_alloc(a, 32, 1);
    used_before_overflow = arena_used(a);
    arena_alloc(a, 64, 1); /* triggers longjmp - nothing after this runs */
    fprintf(stderr, "FAIL: unreachable\n");
    exit(1);
}

/* Invoked as a separate process (see the Makefile's test-arena target):
 * overflowing with no overflow target registered at all must abort(),
 * not silently continue or longjmp to garbage. Run out-of-process
 * because abort() ends the whole program - it can't be one case among
 * others in the normal in-process suite above. */
static void no_target_overflow(void) {
    Arena *a = arena_create(64);
    arena_alloc(a, 32, 1);
    arena_alloc(a, 64, 1); /* overflows; no target set -> must abort() here */
    fprintf(stderr, "FAIL: did not abort on overflow with no target registered\n");
    exit(1); /* only reached if arena_alloc wrongly returned */
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--no-target-overflow") == 0) {
        no_target_overflow();
        return 1; /* unreachable if arena_alloc behaved correctly */
    }

    test_normal_allocation();
    test_alignment();
    test_cap_hit();
    test_no_allocation_after_cap();
    printf("ALL PASS\n");
    return 0;
}
