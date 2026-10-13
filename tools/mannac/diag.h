#pragma once
#include <stddef.h>
#include "arena.h"
#include "ast.h" /* for Span */

/* Only errors exist in v0 - no warnings, no lints. A feature that only
 * warns instead of rejecting isn't enforced (MANNA_PRINCIPLES.md
 * principle 2, explicit over implicit); add severities here only when
 * something genuinely needs one, not preemptively. */
typedef enum { DIAG_ERROR } DiagSeverity;

/* One edit of a fix: replace the text at `span` with `text`. A zero-width
 * span (start == end) is a pure insertion at that position. */
typedef struct {
    Span        span;
    const char *text;
} DiagEdit;

/* One machine-applicable fix suggestion (error JSON schema, docs/M2_PLAN.md
 * §7 item 4: a fix carries an `edits` array) - one to MAX_FIX_EDITS edits
 * applied together. Most diagnostics have no fix. At most one fix per
 * diagnostic for now. E0108 is the only two-edit fix (the `(` and `)` of its
 * parenthesization); the rest have one. */
#define MAX_FIX_EDITS 2
typedef struct {
    const char *title;
    int         n_edits;
    DiagEdit    edits[MAX_FIX_EDITS];
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

/* E0108: "comparison and equality can't be mixed without parentheses; use (a < b) == c".
 * Reported at the second operator. The fix is two zero-width inserts that
 * parenthesize the comparison-class (<, <=, >, >=) pair - `(` at the start of
 * `pair_left` (the pair's left operand), `)` at the end of `pair_right` (its
 * right operand) - so no source text is needed, only spans. */
Diagnostic diag_mixed_comparison(Arena *a, Span second_operator_span, Span pair_left, Span pair_right);

/* E0109: "'-> unit' is not written; omit the return type". `unit` is reserved
 * but is not a type; "returns nothing" is spelled by omitting the annotation.
 * Reported over the whole `-> unit` (arrow start to unit end). The fix deletes
 * it: when `->` and `unit` sit on one line with only blanks between, ONE edit
 * deletes exactly the text "-> unit" (arrow start to unit end); otherwise
 * (a newline or a comment between) TWO edits delete the `->` token and the
 * `unit` token separately, so a comment or line break between them is kept,
 * never silently destroyed. */
Diagnostic diag_return_unit(Arena *a, Span arrow, Span unit, bool blanks_only_between);

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

/* E0110: "input too large: the compiler's memory limit was exceeded" - the
 * arena's hard cap was hit (parser_state.c registers this via
 * arena_set_overflow_hook, so the unwind that follows is never silent).
 * Reported at the last token the parser had read. No fix-it: the remedy
 * (a smaller input) is not an edit at a place. */
Diagnostic diag_input_too_large(Span span);

/* E0111: "expression too long: more than N chained operations in one
 * expression; split it into several statements" (N = MAX_EXPR_HEIGHT) - an
 * expression tree grew taller than the bound that keeps every later walker
 * inside the host stack (ast.h, Expr.height). Reported at the token that
 * follows the node whose construction crossed the limit (the same
 * convention E0103 uses for its own check). No fix-it: the remedy (several
 * statements) is not an edit at a place. */
Diagnostic diag_expr_too_tall(Span span);

/* E0201: "input too large: the type checker's memory limit was exceeded" - the
 * typed-IR arena's hard cap was hit (ir.c registers this via the arena's
 * overflow hook, so the unwind that follows is never silent). Reported at the
 * start of the declaration being built. No fix-it, like E0110. */
Diagnostic diag_ir_input_too_large(Span span);

/* E0203: "too many errors, stopping" - the typed-IR stage reached
 * IR_MAX_ERRORS (20) in one file (ir.h); a cascade guard like E0104. Reported
 * at the 20th error's span. No fix-it. */
Diagnostic diag_ir_too_many_errors(Span span);

/* ---- Typed IR (E02xx) ---- */

/* E0210: "'X' is already declared at L:C" - a struct or function name already
 * taken (structs and functions share one top-level namespace). Reported at the
 * SECOND declaration's name; L:C is the first declaration's name. */
Diagnostic diag_ir_duplicate_decl(Arena *a, Span at, const char *name, Span first_name);

/* E0211: "duplicate field 'x' in struct 'S'" - at the second field's name. */
Diagnostic diag_ir_duplicate_field(Arena *a, Span at, const char *field, const char *strukt);

/* E0220: "unknown type 'X'" - an identifier in type position that names no struct
 * (a function's name used as a type is the same error). */
Diagnostic diag_ir_unknown_type(Arena *a, Span at, const char *name);

/* E0227: a slice type where slices are not allowed. Step 4 emits the struct-field
 * wording; later steps add the other placements (return type, a slice's element)
 * under the same code (docs/error_codes.md). */
Diagnostic diag_ir_slice_in_field(Span at);

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
