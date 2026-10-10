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

/* Overflow hook (E0110 support): it runs once before the longjmp, may
 * allocate from the reserved tail, and ordinary allocations cannot touch
 * that tail. */
static int hook_calls = 0;
static void *hook_block = NULL;
static void hook_allocates(void *ctx) {
    Arena *a = ctx;
    hook_calls++;
    hook_block = arena_alloc(a, 24, 8); /* only fits in the reserve */
}

static void test_hook_runs_once_and_uses_reserve(void) {
    Arena *a = arena_create(128);
    jmp_buf env;
    if (setjmp(env) != 0) {
        assert(hook_calls == 1);
        assert(hook_block != NULL);
        printf("PASS: overflow hook runs once, before the longjmp, and can allocate from the reserve\n");
        arena_destroy(a);
        return;
    }
    arena_set_overflow_target(a, &env);
    arena_set_overflow_hook(a, hook_allocates, a, 32);
    arena_alloc(a, 90, 1);          /* 90 <= 128 - 32: fits */
    arena_alloc(a, 10, 1);          /* 100 > 96: would fit the 128 cap, but not without the reserve */
    fprintf(stderr, "FAIL: the reserve was handed to an ordinary allocation\n");
    exit(1);
}

static int rec_calls = 0;
static void hook_overflows_too(void *ctx) {
    rec_calls++;
    arena_alloc((Arena *)ctx, 1000, 1); /* far beyond the cap, from inside the hook */
}

static void test_hook_overflow_does_not_recurse(void) {
    Arena *a = arena_create(128);
    jmp_buf env;
    if (setjmp(env) != 0) {
        assert(rec_calls == 1);
        printf("PASS: an overflow inside the hook unwinds without calling it again\n");
        arena_destroy(a);
        return;
    }
    arena_set_overflow_target(a, &env);
    arena_set_overflow_hook(a, hook_overflows_too, a, 32);
    arena_alloc(a, 200, 1);
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

/* Invoked as a separate process: a request no host can satisfy makes
 * malloc return NULL, which arena_create must report and exit(1) on
 * (the "host out-of-memory" branch, distinct from the designed cap).
 * SIZE_MAX / 2 bytes fails without any fault injection. Out-of-process
 * because exit() ends the program. */
static void oom_create(void) {
    arena_create((size_t)-1 / 2);
    fprintf(stderr, "FAIL: arena_create returned despite an unsatisfiable size\n");
    exit(2); /* distinct from the expected exit(1) */
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--oom") == 0) {
        oom_create();
        return 2;
    }
    if (argc > 1 && strcmp(argv[1], "--no-target-overflow") == 0) {
        no_target_overflow();
        return 1; /* unreachable if arena_alloc behaved correctly */
    }

    test_normal_allocation();
    test_alignment();
    test_cap_hit();
    test_no_allocation_after_cap();
    test_hook_runs_once_and_uses_reserve();
    test_hook_overflow_does_not_recurse();
    printf("ALL PASS\n");
    return 0;
}
