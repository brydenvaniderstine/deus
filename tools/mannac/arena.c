#include <stdio.h>
#include <stdlib.h>
#include "arena.h"

struct Arena {
    unsigned char *base;
    size_t         max_size;
    size_t         used;
    jmp_buf       *overflow_target;
};

static size_t align_up(size_t offset, size_t align) {
    return (offset + align - 1) & ~(align - 1);
}

Arena *arena_create(size_t max_size) {
    Arena *a = malloc(sizeof(Arena));
    unsigned char *base = malloc(max_size);
    if (!a || !base) {
        /* Host out-of-memory - a different, much rarer failure mode than
         * the designed "input too large" cap below. Nothing meaningful to
         * return to a caller that itself has nowhere to store an error. */
        fprintf(stderr, "mannac: out of memory creating a %zu-byte arena\n", max_size);
        exit(1);
    }
    a->base = base;
    a->max_size = max_size;
    a->used = 0;
    a->overflow_target = NULL;
    return a;
}

void arena_destroy(Arena *a) {
    free(a->base);
    free(a);
}

void arena_set_overflow_target(Arena *a, jmp_buf *target) {
    a->overflow_target = target;
}

void *arena_alloc(Arena *a, size_t size, size_t align) {
    size_t offset = align_up(a->used, align);

    /* Checked without computing offset + size directly, so a pathological
     * (e.g. fuzzer-supplied) `size` can't wrap size_t and falsely pass. */
    if (offset > a->max_size || size > a->max_size - offset) {
        if (a->overflow_target) {
            longjmp(*a->overflow_target, 1); /* never returns */
        }
        /* Programmer error (no target registered) - not the designed
         * path, but still must not allocate past the cap, return
         * garbage, or longjmp to an unset jmp_buf (undefined behavior).
         * abort(), not exit(): this is a caller bug, not a clean "input
         * too large" outcome, and should look like one. */
        fprintf(stderr, "mannac: arena cap exceeded with no overflow target registered\n");
        abort();
    }

    a->used = offset + size;
    return a->base + offset;
}

size_t arena_used(const Arena *a) {
    return a->used;
}
