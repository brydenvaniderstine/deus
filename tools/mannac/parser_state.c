#include "parser_state.h"

void parser_state_reset_sync(ParserState *ps) {
    ps->sync_top_level = 0;
    ps->sync_block = 0;
    ps->sync_statement = 0;
    ps->sync_header = 0;
    ps->sync_for_header = 0;
    ps->recovered_past_body = false;
}

void parser_state_reset_for_parse(ParserState *ps) {
    ps->depth = 0;
    ps->error_count = 0;
    ps->unclosed_line = 0;
    ps->unclosed_col = 0;
    ps->progress_line = 1;
    ps->progress_col = 1;
    parser_state_reset_sync(ps);
}

/* Arena exhaustion used to unwind with nothing recorded: an empty error
 * list and no tree (external audit of v0, finding 2). The hook runs just
 * before that unwind and records E0110 in the arena's reserved tail. */
static void on_arena_overflow(void *ctx) {
    ParserState *ps = ctx;
    Span at = {ps->progress_line, ps->progress_col, ps->progress_line, ps->progress_col};
    diag_add(ps->arena, &ps->diags, diag_input_too_large(at));
}

void parser_state_init(ParserState *ps, size_t arena_max_size) {
    ps->arena = arena_create(arena_max_size);
    diag_list_init(&ps->diags);
    parser_state_reset_for_parse(ps);
    arena_set_overflow_target(ps->arena, &ps->fatal_target);
    /* Hold back room for the E0110 diagnostic (and the growth of the list
     * it joins): a diagnostic plus a doubled list of MAX_ERRORS entries
     * is a few KiB. A tiny arena reserves a quarter of itself instead. */
    size_t reserve = arena_max_size >= (1u << 20) ? (16u << 10) : arena_max_size / 4;
    arena_set_overflow_hook(ps->arena, on_arena_overflow, ps, reserve);
}

void parser_state_destroy(ParserState *ps) {
    arena_destroy(ps->arena);
}

void depth_enter(ParserState *ps, Span span) {
    ps->depth++;
    if (ps->depth > MAX_NESTING_DEPTH) {
        diag_add(ps->arena, &ps->diags, diag_max_depth_exceeded(span));
        longjmp(ps->fatal_target, 1);
    }
}

void depth_exit(ParserState *ps) {
    ps->depth--;
}

void parser_error(ParserState *ps, Diagnostic d) {
    diag_add(ps->arena, &ps->diags, d);
    ps->error_count++;
    if (ps->error_count >= MAX_ERRORS) {
        diag_add(ps->arena, &ps->diags, diag_too_many_errors(d.span));
        longjmp(ps->fatal_target, 1);
    }
}
