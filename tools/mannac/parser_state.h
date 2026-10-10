#pragma once
#include <setjmp.h>
#include <stdbool.h>
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
/* Step 10b: Wirth-style follow-set error recovery. Each syntactic level
 * that owns a resume point (docs/M2_PLAN.md's "Error recovery" table)
 * increments its own counter on entry and decrements it on exit - a
 * counter, not a stack of distinct sets, because every instance of a
 * given level (e.g. two nested blocks) declares the identical resume set
 * regardless of nesting depth, so "is this level active at all right
 * now" is all skipping ever needs to know. top_level is incremented
 * exactly once, for the whole file, by parser_parse_file; it stays at 0
 * (inactive) when parser_parse_block/parser_parse_expression are used
 * standalone (as most golden tests do) - there is no "outer" context to
 * hand an orphaned fn/struct off to in that case, so it's correctly
 * treated as ordinary garbage instead. See tools/mannac/parser.c's
 * sync_* functions for the actual skip/ownership mechanism.
 *
 * sync_for_header is not a fifth level - it's a declared MASK, incremented
 * and decremented in lockstep with sync_header only by for_stmt's own
 * header (never by if/while's). While active, it hides statement's ';'
 * from the active-set search (docs/M2_PLAN.md's "Error recovery" table
 * has the level -> resume-tokens -> masks mapping): a for-header's own
 * init/cond separators are bare ';' tokens, indistinguishable by kind
 * alone from the enclosing statement's own resume point, so without the
 * mask, an unresolved for-header's search stops at its OWN internal
 * separator instead of finding '{', reinterpreting the remaining clauses
 * as fresh statements. if/while's headers declare no such mask because
 * their condition is a bare `expression`, which can never contain a ';' -
 * so ';' always safely means "the enclosing statement already ended"
 * for them, exactly as it does outside any header.
 *
 * recovered_past_body is a one-shot signal, not a counter: set by
 * handle_header_failure (parser.c) exactly when a header's recovery
 * search found its own '{' and parsed the dropped statement's body to
 * surface its own errors. Without it, parse_block's statement loop has
 * no way to tell "this header fully recovered past a real body - cur is
 * already sitting wherever that left it" apart from "this statement
 * failed outright and cur is still at the point of failure" - both look
 * identical from the loop's side (parse_statement returned NULL), but
 * only the second case should trigger another sync_skip. Treating the
 * first case the same way could re-skip across content that was never
 * part of the mistake (a block-level resume token happening to follow
 * immediately, matched the first time only by chance - see the fixed
 * for-init cascade's own commit for the general shape of this hazard).
 * Set, then read and cleared (never left true across a loop iteration)
 * by the one call site that consumes it - see parse_block in parser.c.
 * recover_else_branch clears it while the dropped `if`'s else branch parses
 * (so the branch's own blocks cannot consume it) and sets it again after. */
typedef struct {
    Arena   *arena;
    DiagList diags;
    int      depth;
    int      error_count;
    jmp_buf  fatal_target;
    int      sync_top_level;
    int      sync_block;
    int      sync_statement;
    int      sync_header;
    int      sync_for_header;
    bool     recovered_past_body;
    /* Position (line, col) of the `fn`/`struct` token at which a block was
     * last reported unclosed ("expected '}' to close block"), 0 when none.
     * Every enclosing block sees the same token and would repeat the same
     * diagnostic; only the innermost reports (parse_block, parser.c). */
    int      unclosed_line;
    int      unclosed_col;
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

/* Zeroes every sync_* counter and recovered_past_body - parser_state_init
 * already calls this once, but a ParserState that outlives a single
 * top-level parse call (reused across more than one parser_parse_file/
 * block/expression call, or recovered into after a longjmp to
 * fatal_target - which unwinds the C stack without running any of the
 * sync_*-- decrements a normal return would have) could otherwise start
 * its NEXT parse with stale, non-zero counters left over from whatever
 * was active when the previous one stopped. No caller in this codebase
 * does either of those things today (parser_main.c always makes a fresh
 * ParserState per invocation), but parser.c's three public entry points
 * call this defensively anyway, rather than depending on that staying
 * true. */
void parser_state_reset_sync(ParserState *ps);

/* Resets ALL per-parse state: depth, error_count, and everything
 * parser_state_reset_sync zeroes. Every public parse entry point calls
 * this first. depth and error_count belong here too, not just the sync
 * counters: a fatal longjmp (E0103, E0104, arena overflow) skips every
 * depth_exit, so a ParserState reused afterwards started its next parse
 * at the stale depth and hit a spurious E0103 on a valid file (external
 * audit of da9f01c, R6). diags is deliberately NOT cleared - it is the
 * accumulated result list, owned by the caller. */
void parser_state_reset_for_parse(ParserState *ps);

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
