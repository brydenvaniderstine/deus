#include "parser_state.h"

void parser_state_init(ParserState *ps, size_t arena_max_size) {
    ps->arena = arena_create(arena_max_size);
    diag_list_init(&ps->diags);
    ps->depth = 0;
    ps->error_count = 0;
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
