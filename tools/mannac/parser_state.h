#pragma once
#include <setjmp.h>
#include "arena.h"
#include "diag.h"

#define MAX_NESTING_DEPTH 256
#define MAX_ERRORS 20

/* Shared context threaded through every parse function from Step 4
 * onward - this is deliberately built BEFORE any grammar-rule parsing
 * function exists, so depth-bound and error-recovery hooks go into every
 * one of those functions from the moment it's first written, not
 * retrofitted afterward.
 *
 * One unified escape point (fatal_target) for every "this input can't be
 * processed, stop" condition: the arena's hard cap (Step 1), the nesting-
 * depth bound, and the too-many-errors cap below. All three longjmp here
 * with a diagnostic already recorded in `diags` - the catch site just
 * reports `diags` and stops, the same way regardless of which of the
 * three fired. */
typedef struct {
    Arena   *arena;
    DiagList diags;
    int      depth;
    int      error_count;
    jmp_buf  fatal_target;
} ParserState;

/* Creates the arena (arena_max_size) and registers fatal_target with it.
 * The caller is still responsible for `setjmp(ps->fatal_target)` itself -
 * this can't do that on the caller's behalf, setjmp only behaves
 * correctly called directly in the frame that needs to resume there.
 * Usage:
 *   ParserState ps;
 *   parser_state_init(&ps, 16 * 1024 * 1024);
 *   if (setjmp(ps.fatal_target) != 0) {
 *       // report ps.diags, then:
 *       parser_state_destroy(&ps);
 *       return NULL;
 *   }
 *   ... parsing, using depth_enter/depth_exit/parser_error throughout ...
 *   parser_state_destroy(&ps);
 */
void parser_state_init(ParserState *ps, size_t arena_max_size);
void parser_state_destroy(ParserState *ps);

/* Call at the very start of EVERY recursive parse function - expression,
 * statement, block, type - no exceptions. This explicitly includes
 * unary's self-recursion (`!!!!x`) and nested array types (`[1][1]...T`):
 * anything that can recurse shares this one counter, not a per-construct
 * one. Call depth_exit() before every return from that function,
 * including early returns on error - unless depth_enter() itself already
 * longjmp'd away, in which case there's nothing to balance (see below).
 *
 * If this call would push depth past MAX_NESTING_DEPTH, it records
 * diag_max_depth_exceeded() and longjmps to ps->fatal_target - the call
 * never returns in that case, so the caller's own depth_exit() for THIS
 * level (and every level still on the stack above it) never runs either.
 * That's fine: a fatal longjmp means abandon the whole parse, not resume
 * it, so the exact counter value afterward doesn't matter. */
void depth_enter(ParserState *ps, Span span);
void depth_exit(ParserState *ps);

/* Records a diagnostic. If this is the MAX_ERRORS'th one, additionally
 * records diag_too_many_errors() and longjmps to ps->fatal_target - the
 * safety valve against cascading noise on badly malformed input, not a
 * precision mechanism. */
void parser_error(ParserState *ps, Diagnostic d);
