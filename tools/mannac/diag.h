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

/* "assignment is a statement; did you mean '=='?" - raised when a
 * condition is followed by '=' where its terminator was expected.
 * `expected` names that terminator for the JSON `expected` field: "'{'"
 * for if/while (the block opens), "';'" for a for header's condition
 * clause. Carries a fix: replace the single '=' with '=='. This is E0102
 * in docs/M2_PLAN.md's own worked JSON-schema example - reused here, not
 * reinvented. */
Diagnostic diag_assignment_as_condition(Arena *a, Span equals_span, const char *expected);

/* "maximum nesting depth (256 units) exceeded" - raised by parser_state.h's
 * depth_enter() when a recursive parse path goes past MAX_NESTING_DEPTH. */
Diagnostic diag_max_depth_exceeded(Span span);

/* "too many errors, stopping" - raised by parser_state.h's parser_error()
 * once the error count reaches MAX_ERRORS; a safety valve against
 * cascading noise on badly malformed input, not a precision mechanism. */
Diagnostic diag_too_many_errors(Span span);

/* Generic "expected X, found Y" - every parse error that isn't one of the
 * two dedicated messages above goes through this one constructor, so the
 * message format (docs/M2_PLAN.md's own golden-file example: "expected
 * ';' after expression, found '}'") lives in one place. `expected` is a
 * bare syntactic-category name ("expression") or a quoted literal
 * ("')'") exactly as the caller wants it to read; `found` is normally
 * built by the caller via a token-describe helper. `context` is an
 * optional "after ..." clause (NULL omits it entirely, e.g. for primary's
 * own "expected expression, found 'X'" with no "after" clause at all).
 * E0105 - the first non-dedicated parser diagnostic (E0101-E0104 above are
 * all dedicated; see docs/error_codes.md). */
Diagnostic diag_expected(Arena *a, Span span, const char *expected, const char *found, const char *context);

/* "unsupported manna version 'vN' (expected 'v0')" - raised by the file-
 * level version_decl check (MANNA_PRINCIPLES.md Part 4 item 1: a missing
 * or wrong version is a compile error) when 'manna' IS present and
 * followed by a well-formed identifier, but that identifier isn't "v0".
 * E0106. Three distinct version_decl mistakes, three distinct treatments:
 * 'manna' keyword altogether absent is diag_missing_version_header
 * (E0107, below); 'manna' present but malformed after that (no
 * identifier, no ';') is the generic diag_expected (E0105) - a plain
 * syntax error; 'manna' present and well-formed but the wrong version is
 * this one (E0106). */
Diagnostic diag_unsupported_version(Arena *a, Span span, const char *version, size_t version_len);

/* "missing version header; add 'manna v0;' as the first line" - raised
 * when the first token of the file isn't 'manna' at all (as opposed to
 * 'manna' being present but malformed or wrong - see diag_unsupported_
 * version above). Carries a fix-it: insert "manna v0;\n" at the very
 * start of the file (1:1), regardless of where `found_span` (the
 * diagnostic's own display location - wherever the file's actual first
 * token is) points. E0107. */
Diagnostic diag_missing_version_header(Arena *a, Span found_span);
