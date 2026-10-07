#pragma once
#include <stddef.h>
#include <setjmp.h>

/* Bump allocator backing all AST storage for one compilation. One arena per
 * file; destroyed in a single call when that file's compilation is done
 * (success or failure) - no per-node free(), nothing to leak or
 * double-free, consistent with v1's planned arena/region memory model
 * (docs/MANNA_PRINCIPLES.md item 7): the compiler eating its own cooking,
 * even though v0 itself (and this C implementation) has no such model yet.
 *
 * Hard size cap, enforced by a hard abort, not a soft flag: a pathological
 * or adversarial input (deeply nested but individually-small constructs,
 * or just an enormous file) must produce a clean "input too large" compile
 * error, never unbounded memory growth, never a crash, and never two
 * "live" objects aliasing the same bytes. The first version of this file
 * tried to keep serving allocations past the cap from a fixed scratch
 * region plus a malloc'd fallback for oversized requests - that didn't
 * actually cap anything (the fallback mallocs were unbounded) and let
 * unrelated nodes alias the same scratch memory when it wrapped. Fixed:
 * arena_alloc() does ZERO allocations once the cap would be exceeded. It
 * longjmps straight back to the setjmp point arena_set_overflow_target()
 * registered - there is no "serve something anyway and hope nothing reads
 * it" path left at all.
 *
 * Usage:
 *   Arena *a = arena_create(max_size);
 *   jmp_buf env;
 *   if (setjmp(env) != 0) {
 *       fprintf(stderr, "error: input too large\n");
 *       arena_destroy(a);
 *       return NULL;
 *   }
 *   arena_set_overflow_target(a, &env);
 *   ... arena_alloc(a, ...) as needed ...
 *   arena_destroy(a);
 *
 * HARD RULE for anything calling this between setjmp and either normal
 * completion or the longjmp: every single allocation must go through
 * arena_alloc(). No stray malloc()/strdup()/calloc() calls anywhere in
 * that span - not even "just this once, it's small." If the cap is hit
 * and arena_alloc() longjmps, execution jumps straight back to the
 * setjmp point; any stray malloc'd block made in between has no owner
 * left to free it; it's unreachably leaked, full stop, no matter how the
 * rest of the parser is structured. This is what makes arena_destroy()
 * sufficient on both the success path and the overflow path - it's only
 * true if nothing outside the arena was ever allocated in the first
 * place.
 *
 * ASan limitation, still worth documenting even with the cap now hard:
 * allocations *within* the cap still share one underlying malloc'd block,
 * so ASan's redzone/use-after-free detection (which operates per
 * underlying allocation, not per sub-region within one) still can't see a
 * one-byte overflow from AST node A into adjacent node B. Manual poisoning
 * (__asan_poison_memory_region around each allocation's unused alignment
 * padding) would close this gap; not implemented in this pass - flagged
 * as a future hardening step, not silently assumed safe. */

typedef struct Arena Arena;

Arena *arena_create(size_t max_size);
void   arena_destroy(Arena *a);

/* Must be called once, right after arena_create and before any
 * arena_alloc, with the address of a jmp_buf the caller has already
 * setjmp'd. Every later arena_alloc that would exceed the cap longjmps to
 * it with a nonzero value instead of returning - the call that triggered
 * it never returns at all. */
void arena_set_overflow_target(Arena *a, jmp_buf *target);

void  *arena_alloc(Arena *a, size_t size, size_t align);
size_t arena_used(const Arena *a);

/* Formats into an arena-allocated, exactly-sized, NUL-terminated buffer -
 * printf-family allocation with the same lifetime/no-free rules as every
 * other arena_alloc result. Subject to the same HARD RULE above: only call
 * this between a ParserState's setjmp and completion, same as any other
 * arena_alloc. */
const char *arena_sprintf(Arena *a, const char *fmt, ...);
