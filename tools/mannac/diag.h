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

/* E0221: "struct 'S' contains itself" - a containment cycle by value (directly,
 * through other structs, or through arrays). Once per cycle, at the name of its
 * lowest-declaration-index member. */
Diagnostic diag_ir_struct_contains_itself(Arena *a, Span at, const char *name);

/* E0222: "type is too large (size exceeds the limit)" - a type whose size would
 * exceed 2^31 - 1 bytes (arithmetic overflow while computing it included). At the
 * array type that overflowed, or the struct's name when the struct as a whole does. */
Diagnostic diag_ir_type_too_large(Span at);

/* E0212: "duplicate parameter 'x'" - at the repeat's name. */
Diagnostic diag_ir_duplicate_param(Arena *a, Span at, const char *name);

/* E0214: "unknown attribute 'x'; the only v0 attribute is 'kernel_hook'" - at the
 * attribute's name. Supersedes any naming-convention check on it (one mistake,
 * one error). */
Diagnostic diag_ir_unknown_attribute(Arena *a, Span at, const char *name);

/* E0215: "duplicate attribute 'x'" - at the repeat's name. Fix: delete the
 * repeated attribute (one edit removing `attribute_span`, the whole `[ IDENT ]`). */
Diagnostic diag_ir_duplicate_attribute(Arena *a, Span at, Span attribute_span, const char *name);

/* E0213: "'x' is already declared in this function" - a `let` whose name is already visible
 * (a parameter, or a local of this or an enclosing block): no shadowing. At the new name. */
Diagnostic diag_ir_already_declared(Arena *a, Span at, const char *name);

/* E0225: "cannot infer the type of this integer literal; annotate it" - an integer literal
 * expression with no expected type (`let i = 0;`). At the whole expression. */
Diagnostic diag_ir_cannot_infer(Arena *a, Span at);

/* E0230: "unknown name 'x'". */
Diagnostic diag_ir_unknown_name(Arena *a, Span at, const char *name);

/* E0231: "'f' is a function, not a value" / "'S' is a struct, not a value". */
Diagnostic diag_ir_not_a_value(Arena *a, Span at, const char *name, bool is_struct);

/* E0261: "operands have different types: T and U; convert one explicitly" - at the whole operator
 * expression. When BOTH are integer types a fix is attached: wrap one operand in the conversion
 * to the other's type, as two zero-width inserts (`T(` before the operand, `)` after it - the
 * E0108 mechanism; no source text needed). `wrap_left` picks the operand, `to_type` the target
 * (docs/TYPED_IR.md D4). With a non-integer operand (bool, a struct) no conversion exists and
 * there is no fix (`fix_to_type` NULL). */
Diagnostic diag_ir_operand_types_differ(Arena *a, Span at, const char *left, const char *right,
                                        Span left_span, Span right_span, bool wrap_left, const char *fix_to_type);

/* E0276: a `return` whose value does not match the function. `return;` in a function that returns a
 * type: "missing return value: function 'f' returns T", at the statement. `return v;` in a function
 * that returns nothing: "unexpected return value: function 'f' returns nothing", at the VALUE. */
Diagnostic diag_ir_missing_return_value(Arena *a, Span at, const char *func, const char *ret_type);
Diagnostic diag_ir_unexpected_return_value(Arena *a, Span at, const char *func);

/* E0277: "function 'f' may end without returning a value" - located at the body's closing `}`, the
 * place where control runs off the end. */
Diagnostic diag_ir_may_end_without_return(Arena *a, Span at, const char *func);

/* E0226: "a 'unit' value can't be used here" - a call to a function that returns nothing used as a value
 * (bound, passed, returned, compared, assigned, a condition). At the call. A unit call on its own as
 * a statement is fine. */
Diagnostic diag_ir_unit_value(Span at);

/* E0232: "unknown function 'f'" - at the callee's name. */
Diagnostic diag_ir_unknown_function(Arena *a, Span at, const char *name);

/* E0233: "'f' takes N argument(s), found M" - at the whole call. */
Diagnostic diag_ir_wrong_arity(Arena *a, Span at, const char *name, size_t params, size_t args);

/* E0234: "'f' is a function; named arguments are for struct construction" - at the first named
 * argument of the call. */
Diagnostic diag_ir_named_args_to_function(Arena *a, Span at, const char *name);

/* E0240: "struct construction names every field: use 'field: value'" - at the first positional
 * argument of a construction (alone or mixed with named ones). */
Diagnostic diag_ir_positional_construction(Span at);

/* E0241: "left of ':' must be a field name" - at the expression written left of the ':' (anything
 * but a bare, unparenthesized identifier). */
Diagnostic diag_ir_not_a_field_name(Span at);

/* E0242: "struct 'S' has no field 'x'" - at the field's name, in a construction and in an access. */
Diagnostic diag_ir_no_such_field(Arena *a, Span at, const char *strukt, const char *field);

/* E0243: "missing field 'x' in construction of 'S'" - at the whole construction, one per missing field. */
Diagnostic diag_ir_missing_field(Arena *a, Span at, const char *field, const char *strukt);

/* E0244: "field 'x' given twice" - at the repeat's name. */
Diagnostic diag_ir_field_given_twice(Arena *a, Span at, const char *field);

/* E0245: "fields must be given in declaration order: 'x' before 'y'" - at the field written too
 * late (x), naming the field written before it that comes later in the declaration (y). */
Diagnostic diag_ir_field_out_of_order(Arena *a, Span at, const char *field, const char *before);

/* E0251: "type T has no fields" - at the base of a field access whose type is not a struct. */
Diagnostic diag_ir_type_has_no_fields(Arena *a, Span at, const char *type);

/* E0235: "recursion is not allowed in v0" - once per call-graph cycle (docs/error_codes.md for where). */
Diagnostic diag_ir_recursion(Span at);

/* E0275: "'break' outside a loop" / "'continue' outside a loop" - at the statement. */
Diagnostic diag_ir_outside_loop(Span at, const char *word);

/* E0270: "can't assign to this expression" - at the whole target. */
Diagnostic diag_ir_not_assignable(Span at);

/* E0223: "type mismatch: expected T, found U" - U is a type name, or `integer literal` for an
 * untyped integer literal expression (it has no type until one is expected of it). At the
 * whole offending expression. */
Diagnostic diag_ir_type_mismatch(Arena *a, Span at, const char *expected, const char *found);

/* E0224: "integer literal N does not fit in T" - N is the literal as written, in decimal.
 * `negated` prints it with its sign (`-129`) for a literal directly under unary minus,
 * located over the whole `-129`; a parenthesized literal under minus, `-(129)`, is the
 * plain form at the parenthesized literal. */
Diagnostic diag_ir_literal_range(Arena *a, Span at, uint64_t value, bool negated, const char *type);

/* E0260: "operator 'op' is not defined for T" - at the whole operator expression. */
Diagnostic diag_ir_operator_undefined(Arena *a, Span at, const char *op, const char *type);

/* E0227, return-type and slice-element wordings (the struct-field one is below). */
Diagnostic diag_ir_slice_in_return(Span at);
Diagnostic diag_ir_slice_of_slice(Span at);

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
