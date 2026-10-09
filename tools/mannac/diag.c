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

Diagnostic diag_assignment_as_condition(Arena *a, Span equals_span, const char *expected) {
    DiagFix *fix = arena_alloc(a, sizeof(DiagFix), alignof(DiagFix));
    fix->title = "use comparison";
    fix->edit_span = equals_span;
    fix->replacement_text = "==";

    Diagnostic d = {0};
    d.code = "E0102";
    d.severity = DIAG_ERROR;
    d.message = "assignment is a statement; did you mean '=='?";
    d.span = equals_span;
    d.expected = expected;
    d.found = "'='";
    d.fix = fix;
    return d;
}

Diagnostic diag_max_depth_exceeded(Span span) {
    Diagnostic d = {0};
    d.code = "E0103";
    d.severity = DIAG_ERROR;
    d.message = "maximum nesting depth (256 units) exceeded";
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

Diagnostic diag_expected(Arena *a, Span span, const char *expected, const char *found, const char *context) {
    Diagnostic d = {0};
    d.code = "E0105";
    d.severity = DIAG_ERROR;
    d.message = context
        ? arena_sprintf(a, "expected %s %s, found %s", expected, context, found)
        : arena_sprintf(a, "expected %s, found %s", expected, found);
    d.span = span;
    d.expected = expected;
    d.found = found;
    d.fix = NULL;
    return d;
}

Diagnostic diag_unsupported_version(Arena *a, Span span, const char *version, size_t version_len) {
    const char *found = arena_sprintf(a, "'%.*s'", (int)version_len, version);
    Diagnostic d = {0};
    d.code = "E0106";
    d.severity = DIAG_ERROR;
    d.message = arena_sprintf(a, "unsupported manna version %s (expected 'v0')", found);
    d.span = span;
    d.expected = "'v0'";
    d.found = found;
    d.fix = NULL;
    return d;
}

Diagnostic diag_missing_version_header(Arena *a, Span found_span) {
    static const Span insert_at_start = {1, 1, 1, 1};

    DiagFix *fix = arena_alloc(a, sizeof(DiagFix), alignof(DiagFix));
    fix->title = "add version header";
    fix->edit_span = insert_at_start;
    fix->replacement_text = "manna v0;\n";

    Diagnostic d = {0};
    d.code = "E0107";
    d.severity = DIAG_ERROR;
    d.message = "missing version header; add 'manna v0;' as the first line";
    d.span = found_span;
    d.expected = "'manna v0;'";
    d.found = NULL;
    d.fix = fix;
    return d;
}
