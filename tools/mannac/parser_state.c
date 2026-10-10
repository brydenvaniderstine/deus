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
    parser_state_reset_sync(ps);
}

void parser_state_init(ParserState *ps, size_t arena_max_size) {
    ps->arena = arena_create(arena_max_size);
    diag_list_init(&ps->diags);
    parser_state_reset_for_parse(ps);
    arena_set_overflow_target(ps->arena, &ps->fatal_target);
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
