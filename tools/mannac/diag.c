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

Diagnostic diag_mixed_comparison(Arena *a, Span second_operator_span, Span pair_left, Span pair_right) {
    DiagFix *fix = arena_alloc(a, sizeof(DiagFix), alignof(DiagFix));
    fix->title = "add parentheses";
    fix->n_edits = 2;
    fix->edits[0].span.start_line = pair_left.start_line;
    fix->edits[0].span.start_col = pair_left.start_col;
    fix->edits[0].span.end_line = pair_left.start_line;
    fix->edits[0].span.end_col = pair_left.start_col;
    fix->edits[0].text = "(";
    fix->edits[1].span.start_line = pair_right.end_line;
    fix->edits[1].span.start_col = pair_right.end_col;
    fix->edits[1].span.end_line = pair_right.end_line;
    fix->edits[1].span.end_col = pair_right.end_col;
    fix->edits[1].text = ")";

    Diagnostic d = {0};
    d.code = "E0108";
    d.severity = DIAG_ERROR;
    d.message = "comparison and equality can't be mixed without parentheses; use (a < b) == c";
    d.span = second_operator_span;
    d.expected = NULL;
    d.found = NULL;
    d.fix = fix;
    return d;
}

Diagnostic diag_return_unit(Arena *a, Span arrow, Span unit, bool blanks_only_between) {
    DiagFix *fix = arena_alloc(a, sizeof(DiagFix), alignof(DiagFix));
    fix->title = "remove the return type";
    if (blanks_only_between) {
        fix->n_edits = 1;
        fix->edits[0].span = arrow;
        fix->edits[0].span.end_line = unit.end_line;
        fix->edits[0].span.end_col = unit.end_col;
        fix->edits[0].text = "";
    } else {
        fix->n_edits = 2;
        fix->edits[0].span = arrow;
        fix->edits[0].text = "";
        fix->edits[1].span = unit;
        fix->edits[1].text = "";
    }

    Diagnostic d = {0};
    d.code = "E0109";
    d.severity = DIAG_ERROR;
    d.message = "'-> unit' is not written; omit the return type";
    d.span = arrow;
    d.span.end_line = unit.end_line;
    d.span.end_col = unit.end_col;
    d.expected = NULL;
    d.found = NULL;
    d.fix = fix;
    return d;
}

Diagnostic diag_assignment_as_condition(Arena *a, Span equals_span, const char *expected) {
    DiagFix *fix = arena_alloc(a, sizeof(DiagFix), alignof(DiagFix));
    fix->title = "use comparison";
    fix->n_edits = 1;
    fix->edits[0].span = equals_span;
    fix->edits[0].text = "==";

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

#define MANNA_STR_(x) #x
#define MANNA_STR(x) MANNA_STR_(x)

Diagnostic diag_expr_too_tall(Span span) {
    Diagnostic d = {0};
    d.code = "E0111";
    d.severity = DIAG_ERROR;
    d.message = "expression too long: more than " MANNA_STR(MAX_EXPR_HEIGHT)
                " chained operations in one expression; split it into several statements";
    d.span = span;
    d.expected = NULL;
    d.found = NULL;
    d.fix = NULL;
    return d;
}

Diagnostic diag_input_too_large(Span span) {
    Diagnostic d = {0};
    d.code = "E0110";
    d.severity = DIAG_ERROR;
    d.message = "input too large: the compiler's memory limit was exceeded";
    d.span = span;
    d.expected = NULL;
    d.found = NULL;
    d.fix = NULL;
    return d;
}

Diagnostic diag_ir_input_too_large(Span span) {
    Diagnostic d = {0};
    d.code = "E0201";
    d.severity = DIAG_ERROR;
    d.message = "input too large: the type checker's memory limit was exceeded";
    d.span = span;
    return d;
}

Diagnostic diag_ir_too_many_errors(Span span) {
    Diagnostic d = {0};
    d.code = "E0203";
    d.severity = DIAG_ERROR;
    d.message = "too many errors, stopping";
    d.span = span;
    return d;
}

Diagnostic diag_ir_duplicate_decl(Arena *a, Span at, const char *name, Span first_name) {
    Diagnostic d = {0};
    d.code = "E0210";
    d.severity = DIAG_ERROR;
    d.message = arena_sprintf(a, "'%s' is already declared at %d:%d", name, first_name.start_line, first_name.start_col);
    d.span = at;
    return d;
}

Diagnostic diag_ir_duplicate_field(Arena *a, Span at, const char *field, const char *strukt) {
    Diagnostic d = {0};
    d.code = "E0211";
    d.severity = DIAG_ERROR;
    d.message = arena_sprintf(a, "duplicate field '%s' in struct '%s'", field, strukt);
    d.span = at;
    return d;
}

Diagnostic diag_ir_unknown_type(Arena *a, Span at, const char *name) {
    Diagnostic d = {0};
    d.code = "E0220";
    d.severity = DIAG_ERROR;
    d.message = arena_sprintf(a, "unknown type '%s'", name);
    d.span = at;
    return d;
}

Diagnostic diag_ir_struct_contains_itself(Arena *a, Span at, const char *name) {
    Diagnostic d = {0};
    d.code = "E0221";
    d.severity = DIAG_ERROR;
    d.message = arena_sprintf(a, "struct '%s' contains itself", name);
    d.span = at;
    return d;
}

Diagnostic diag_ir_type_too_large(Span at) {
    Diagnostic d = {0};
    d.code = "E0222";
    d.severity = DIAG_ERROR;
    d.message = "type is too large (size exceeds the limit)";
    d.span = at;
    return d;
}

Diagnostic diag_ir_duplicate_param(Arena *a, Span at, const char *name) {
    Diagnostic d = {0};
    d.code = "E0212";
    d.severity = DIAG_ERROR;
    d.message = arena_sprintf(a, "duplicate parameter '%s'", name);
    d.span = at;
    return d;
}

Diagnostic diag_ir_unknown_attribute(Arena *a, Span at, const char *name) {
    Diagnostic d = {0};
    d.code = "E0214";
    d.severity = DIAG_ERROR;
    d.message = arena_sprintf(a, "unknown attribute '%s'; the only v0 attribute is 'kernel_hook'", name);
    d.span = at;
    return d;
}

Diagnostic diag_ir_duplicate_attribute(Arena *a, Span at, Span attribute_span, const char *name) {
    DiagFix *fix = arena_alloc(a, sizeof(DiagFix), alignof(DiagFix));
    fix->title = "delete the repeated attribute";
    fix->n_edits = 1;
    fix->edits[0].span = attribute_span;
    fix->edits[0].text = "";

    Diagnostic d = {0};
    d.code = "E0215";
    d.severity = DIAG_ERROR;
    d.message = arena_sprintf(a, "duplicate attribute '%s'", name);
    d.span = at;
    d.fix = fix;
    return d;
}

Diagnostic diag_ir_already_declared(Arena *a, Span at, const char *name) {
    Diagnostic d = {0};
    d.code = "E0213";
    d.severity = DIAG_ERROR;
    d.message = arena_sprintf(a, "'%s' is already declared in this function", name);
    d.span = at;
    return d;
}

Diagnostic diag_ir_cannot_infer(Arena *a, Span at) {
    (void)a;
    Diagnostic d = {0};
    d.code = "E0225";
    d.severity = DIAG_ERROR;
    d.message = "cannot infer the type of this integer literal; annotate it";
    d.span = at;
    return d;
}

Diagnostic diag_ir_unknown_name(Arena *a, Span at, const char *name) {
    Diagnostic d = {0};
    d.code = "E0230";
    d.severity = DIAG_ERROR;
    d.message = arena_sprintf(a, "unknown name '%s'", name);
    d.span = at;
    return d;
}

Diagnostic diag_ir_not_a_value(Arena *a, Span at, const char *name, bool is_struct) {
    Diagnostic d = {0};
    d.code = "E0231";
    d.severity = DIAG_ERROR;
    d.message = arena_sprintf(a, "'%s' is a %s, not a value", name, is_struct ? "struct" : "function");
    d.span = at;
    return d;
}

Diagnostic diag_ir_operand_types_differ(Arena *a, Span at, const char *left, const char *right,
                                        Span left_span, Span right_span, bool wrap_left, const char *fix_to_type) {
    Diagnostic d = {0};
    d.code = "E0261";
    d.severity = DIAG_ERROR;
    d.message = arena_sprintf(a, "operands have different types: %s and %s; convert one explicitly", left, right);
    d.span = at;
    if (fix_to_type) {
        Span op = wrap_left ? left_span : right_span;
        DiagFix *fix = arena_alloc(a, sizeof(DiagFix), alignof(DiagFix));
        fix->title = arena_sprintf(a, "convert the %s operand to %s", wrap_left ? "left" : "right", fix_to_type);
        fix->n_edits = 2;
        fix->edits[0].span = (Span){op.start_line, op.start_col, op.start_line, op.start_col}; /* zero-width, before */
        fix->edits[0].text = arena_sprintf(a, "%s(", fix_to_type);
        fix->edits[1].span = (Span){op.end_line, op.end_col, op.end_line, op.end_col};         /* zero-width, after */
        fix->edits[1].text = ")";
        d.fix = fix;
    }
    return d;
}

Diagnostic diag_ir_outside_loop(Span at, const char *word) {
    Diagnostic d = {0};
    d.code = "E0275";
    d.severity = DIAG_ERROR;
    d.message = word[0] == 'b' ? "'break' outside a loop" : "'continue' outside a loop";
    d.span = at;
    return d;
}

Diagnostic diag_ir_not_assignable(Span at) {
    Diagnostic d = {0};
    d.code = "E0270";
    d.severity = DIAG_ERROR;
    d.message = "can't assign to this expression";
    d.span = at;
    return d;
}

Diagnostic diag_ir_type_mismatch(Arena *a, Span at, const char *expected, const char *found) {
    Diagnostic d = {0};
    d.code = "E0223";
    d.severity = DIAG_ERROR;
    d.message = arena_sprintf(a, "type mismatch: expected %s, found %s", expected, found);
    d.span = at;
    return d;
}

Diagnostic diag_ir_literal_range(Arena *a, Span at, uint64_t value, bool negated, const char *type) {
    Diagnostic d = {0};
    d.code = "E0224";
    d.severity = DIAG_ERROR;
    d.message = arena_sprintf(a, "integer literal %s%llu does not fit in %s", negated ? "-" : "",
                              (unsigned long long)value, type);
    d.span = at;
    return d;
}

Diagnostic diag_ir_operator_undefined(Arena *a, Span at, const char *op, const char *type) {
    Diagnostic d = {0};
    d.code = "E0260";
    d.severity = DIAG_ERROR;
    d.message = arena_sprintf(a, "operator '%s' is not defined for %s", op, type);
    d.span = at;
    return d;
}

Diagnostic diag_ir_slice_in_return(Span at) {
    Diagnostic d = {0};
    d.code = "E0227";
    d.severity = DIAG_ERROR;
    d.message = "a return type can't contain a slice type; slices are allowed only as parameter and local types";
    d.span = at;
    return d;
}

Diagnostic diag_ir_slice_of_slice(Span at) {
    Diagnostic d = {0};
    d.code = "E0227";
    d.severity = DIAG_ERROR;
    d.message = "a slice's element type can't contain a slice type";
    d.span = at;
    return d;
}

Diagnostic diag_ir_slice_in_field(Span at) {
    Diagnostic d = {0};
    d.code = "E0227";
    d.severity = DIAG_ERROR;
    d.message = "a struct field can't contain a slice type; slices are allowed only as parameter and local types";
    d.span = at;
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
    fix->n_edits = 1;
    fix->edits[0].span = insert_at_start;
    fix->edits[0].text = "manna v0;\n";

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
