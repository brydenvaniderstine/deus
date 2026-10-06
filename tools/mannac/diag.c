#include <string.h>
#include <stdalign.h>
#include "diag.h"

void diag_list_init(DiagList *list) {
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

void diag_add(Arena *a, DiagList *list, Diagnostic d) {
    if (list->len == list->cap) {
        size_t new_cap = list->cap == 0 ? 4 : list->cap * 2;
        Diagnostic *new_items = arena_alloc(a, new_cap * sizeof(Diagnostic), alignof(Diagnostic));
        if (list->items) {
            memcpy(new_items, list->items, list->len * sizeof(Diagnostic));
        }
        list->items = new_items;
        list->cap = new_cap;
    }
    list->items[list->len++] = d;
}

Diagnostic diag_chained_comparison(Span second_operator_span) {
    Diagnostic d = {0};
    d.code = "E0101";
    d.severity = DIAG_ERROR;
    d.message = "comparisons can't be chained; use a < b && b < c";
    d.span = second_operator_span;
    d.expected = NULL;
    d.found = NULL;
    d.fix = NULL;
    return d;
}

Diagnostic diag_assignment_as_condition(Arena *a, Span equals_span) {
    DiagFix *fix = arena_alloc(a, sizeof(DiagFix), alignof(DiagFix));
    fix->title = "use comparison";
    fix->edit_span = equals_span;
    fix->replacement_text = "==";

    Diagnostic d = {0};
    d.code = "E0102";
    d.severity = DIAG_ERROR;
    d.message = "assignment is a statement; did you mean '=='?";
    d.span = equals_span;
    d.expected = "'{'";
    d.found = "'='";
    d.fix = fix;
    return d;
}

Diagnostic diag_max_depth_exceeded(Span span) {
    Diagnostic d = {0};
    d.code = "E0103";
    d.severity = DIAG_ERROR;
    d.message = "maximum nesting depth (256) exceeded";
    d.span = span;
    d.expected = NULL;
    d.found = NULL;
    d.fix = NULL;
    return d;
}

Diagnostic diag_too_many_errors(Span span) {
    Diagnostic d = {0};
    d.code = "E0104";
    d.severity = DIAG_ERROR;
    d.message = "too many errors, stopping";
    d.span = span;
    d.expected = NULL;
    d.found = NULL;
    d.fix = NULL;
    return d;
}
