#pragma once
#include <stddef.h>
#include "arena.h"
#include "ast.h" /* for Span */

/* Only errors exist in v0 - no warnings, no lints. A feature that only
 * warns instead of rejecting isn't enforced (MANNA_PRINCIPLES.md
 * principle 2, explicit over implicit); add severities here only when
 * something genuinely needs one, not preemptively. */
typedef enum { DIAG_ERROR } DiagSeverity;

/* One machine-applicable fix suggestion (error JSON schema, docs/M2_PLAN.md
 * §7 item 4) - replace the text at edit_span with replacement_text. Most
 * diagnostics have none; the two dedicated messages do. At most one fix
 * per diagnostic for now - nothing yet needs more. */
typedef struct {
    const char *title;
    Span        edit_span;
    const char *replacement_text;
} DiagFix;

typedef struct {
    const char    *code;      /* stable, e.g. "E0102" - see the two dedicated
                                * constructors below for the registry so far */
    DiagSeverity   severity;
    const char    *message;
    Span           span;
    const char    *expected;  /* may be NULL */
    const char    *found;     /* may be NULL */
    const DiagFix *fix;       /* may be NULL */
} Diagnostic;

typedef struct {
    Diagnostic *items;
    size_t      len;
    size_t      cap;
} DiagList;

void diag_list_init(DiagList *list);
void diag_add(Arena *a, DiagList *list, Diagnostic d);

/* ---- Dedicated diagnostics (docs/M2_PLAN.md "Dedicated diagnostics") ----
 * Named constructors so the exact wording and error code live in one
 * place, not copy-pasted at every call site. */

/* "comparisons can't be chained; use a < b && b < c" - raised the moment a
 * second comparison/equality operator is seen after one has already been
 * parsed. */
Diagnostic diag_chained_comparison(Span second_operator_span);

/* "assignment is a statement; did you mean '=='?" - raised when if/while
 * is expecting the condition-terminating '{' and finds '=' instead.
 * Carries a fix: replace the single '=' with '=='. This is E0102 in
 * docs/M2_PLAN.md's own worked JSON-schema example - reused here, not
 * reinvented. */
Diagnostic diag_assignment_as_condition(Arena *a, Span equals_span);

/* "maximum nesting depth (256) exceeded" - raised by parser_state.h's
 * depth_enter() when a recursive parse path goes past MAX_NESTING_DEPTH. */
Diagnostic diag_max_depth_exceeded(Span span);

/* "too many errors, stopping" - raised by parser_state.h's parser_error()
 * once the error count reaches MAX_ERRORS; a safety valve against
 * cascading noise on badly malformed input, not a precision mechanism. */
Diagnostic diag_too_many_errors(Span span);
