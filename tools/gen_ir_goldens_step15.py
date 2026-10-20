# Generates the typed-IR step 15 goldens tests/ir/one_error_*: for each poisoning path of the builder (the places a
# mistake becomes an ERROR node or a failed declaration, see tools/audit_ir_poison.py) ONE program with ONE mistake whose
# golden holds exactly one diagnostic. Columns come from the source text, independent of the C builder; run from the
# repo root.
import gen_ir_goldens_step7 as g
import gen_ir_goldens_step12 as s12

e = s12.e
PRELUDE = ["manna v0;", "struct P { x: u8, y: u8, }", "fn nothing() {", "}", "fn one(a: u8) -> u8 {", "    return a;", "}"]
unit = "a 'unit' value can't be used here"
infer = "cannot infer the type of this integer literal; annotate it"
mism = lambda a, b: "type mismatch: expected %s, found %s" % (a, b)
unk = lambda n: "unknown name '%s'" % n

# name: ([statements of fn t()], [(statement index, needle, occurrence, code, message)])
CASES = {
    "mismatch_int_literal_for_bool": (["let v: bool = 5;"], [(0, "5", 0, "E0223", mism("bool", "integer literal"))]),
    "mismatch_array_literal_for_u8": (["let v: u8 = [1, 2];"], [(0, "[1, 2]", 0, "E0223", mism("u8", "array literal"))]),
    "not_of_an_integer_literal": (["let v = !5;"], [(0, "5", 0, "E0223", mism("bool", "integer literal"))]),
    "logical_with_an_integer_literal": (["let v = 1 && true;"], [(0, "1", 0, "E0223", mism("bool", "integer literal"))]),
    "bare_len": (["let v = len;"], [(0, "len", 0, "E0256", "len is a builtin function; call it as len(x)")]),
    "negate_a_unit_call": (["let v = -nothing();"], [(0, "nothing()", 0, "E0226", unit)]),
    "negate_a_bool": (["let v = -true;"], [(0, "-true", 0, "E0260", "operator '-' is not defined for bool")]),
    "compare_literal_with_bool": (["let v = 1 == true;"], [(0, "1", 0, "E0223", mism("bool", "integer literal"))]),
    "logical_with_an_unknown_name": (["let v = true && nope;"], [(0, "nope", 0, "E0230", unk("nope"))]),
    "logical_with_a_typed_non_bool": (["let a: u8 = 1;", "let v = a && true;"], [(1, "a && true", 0, "E0260", "operator '&&' is not defined for u8")]),
    "binary_with_a_unit_call": (["let v = nothing() + 1;"], [(0, "nothing()", 0, "E0226", unit)]),
    "named_argument_to_a_function": (["let v = one(a: 1);"], [(0, "a: 1", 0, "E0234", "'one' is a function; named arguments are for struct construction")]),
    "wrong_argument_count": (["let v = one(1, 2);"], [(0, "one(1, 2)", 0, "E0233", "'one' takes 1 argument, found 2")]),
    "positional_construction": (["let v = P(1, 2);"], [(0, "1", 0, "E0240", "struct construction names every field: use 'field: value'")]),
    "construction_name_in_parentheses": (["let v = P((x): 1, y: 2);"], [(0, "(x)", 0, "E0241", "left of ':' must be a field name")]),
    "construction_unknown_field": (["let v = P(x: 1, z: 2, y: 3);"], [(0, "z", 0, "E0242", "struct 'P' has no field 'z'")]),
    "construction_field_twice": (["let v = P(x: 1, x: 2, y: 3);"], [(0, "x", 1, "E0244", "field 'x' given twice")]),
    "construction_out_of_order": (["let v = P(y: 1, x: 2);"], [(0, "x", 0, "E0245", "fields must be given in declaration order: 'x' before 'y'")]),
    "construction_missing_field": (["let v = P(x: 1);"], [(0, "P(x: 1)", 0, "E0243", "missing field 'y' in construction of 'P'")]),
    "len_without_arguments": (["let v = len();"], [(0, "len()", 0, "E0254", "len takes exactly one argument, found 0")]),
    "len_of_an_integer_literal": (["let v = len(5);"], [(0, "5", 0, "E0225", infer)]),
    "len_of_a_non_array": (["let a: u8 = 1;", "let v = len(a);"], [(1, "a", 0, "E0255", "len needs an array or a slice, found u8")]),
    "len_of_a_unit_call": (["let v = len(nothing());"], [(0, "nothing()", 0, "E0226", unit)]),
    "field_of_a_non_struct": (["let a: u8 = 1;", "let v = a.x;"], [(1, "a", 0, "E0251", "type u8 has no fields")]),
    "field_that_does_not_exist": (["let p = P(x: 1, y: 2);", "let v = p.zz;"], [(1, "zz", 0, "E0242", "struct 'P' has no field 'zz'")]),
    "field_of_an_integer_literal": (["let v = (1).x;"], [(0, "(1)", 0, "E0225", infer)]),
    "field_of_a_unit_call": (["let v = nothing().x;"], [(0, "nothing()", 0, "E0226", unit)]),
    "field_of_an_unknown_name": (["let v = nope.x;"], [(0, "nope", 0, "E0230", unk("nope"))]),
    "convert_bool_to_integer": (["let v = u8(true);"], [(0, "u8(true)", 0, "E0265", "can't convert bool to u8; conversions are between integer types only")]),
    "convert_a_unit_call": (["let v = u8(nothing());"], [(0, "nothing()", 0, "E0226", unit)]),
    "index_a_non_array": (["let a: u8 = 1;", "let v = a[0];"], [(1, "a", 0, "E0252", "can't index/slice type u8")]),
    "index_with_a_bool": (["let a: [2]u8 = [1, 2];", "let v = a[true];"], [(1, "true", 0, "E0253", "index/bound must be an integer, found bool")]),
    "index_with_a_unit_call": (["let a: [2]u8 = [1, 2];", "let v = a[nothing()];"], [(1, "nothing()", 0, "E0226", unit)]),
    "index_an_integer_literal": (["let v = (1)[0];"], [(0, "(1)", 0, "E0225", infer)]),
    "index_a_unit_call": (["let v = nothing()[0];"], [(0, "nothing()", 0, "E0226", unit)]),
    "index_an_unknown_name": (["let v = nope[0];"], [(0, "nope", 0, "E0230", unk("nope"))]),
    "slice_an_integer_literal": (["let v = (1)[0..1];"], [(0, "(1)", 0, "E0225", infer)]),
    "slice_a_non_array": (["let a: u8 = 1;", "let v = a[0..1];"], [(1, "a", 0, "E0252", "can't slice type u8".replace("can't slice", "can't index/slice"))]),
    "array_of_bool_for_u8": (["let v: u8 = [true];"], [(0, "[true]", 0, "E0223", mism("u8", "[1]bool"))]),
    "array_for_an_unknown_type": (["let v: Nope = [1, 2];"], [(0, "Nope", 0, "E0220", "unknown type 'Nope'")]),
    "array_of_the_wrong_length": (["let v: [3]u8 = [1, 2];"], [(0, "[1, 2]", 0, "E0223", mism("[3]u8", "[2]u8"))]),
    "repeat_too_large_for_the_expected_array": (["let v: [2]bool = [true; 2147483648];"], [(0, "[true; 2147483648]", 0, "E0222", "type is too large (size exceeds the limit)")]),
    "unit_call_for_an_expected_type": (["let v: u8 = nothing();"], [(0, "nothing()", 0, "E0226", unit)]),
    "unit_call_bound_without_a_type": (["let v = nothing();"], [(0, "nothing()", 0, "E0226", unit)]),
    "integer_literal_as_a_statement": (["5;"], [(0, "5", 0, "E0225", infer)]),
    "assign_to_a_call": (["one(1) = 2;"], [(0, "one(1)", 0, "E0270", "can't assign to this expression")]),
    "break_outside_a_loop": (["break;"], [(0, "break", 0, "E0275", "'break' outside a loop")]),
    "array_of_arrays_over_the_size_limit": (["let v = [[true; 2147483647], [true; 2147483647]];"], [(0, "[[true; 2147483647], [true; 2147483647]]", 0, "E0222", "type is too large (size exceeds the limit)")]),
    "array_whose_first_element_is_a_unit_call": (["let v = [nothing(), 1];"], [(0, "nothing()", 0, "E0226", unit)]),
    "array_of_integer_literals": (["let v = [1, 2];"], [(0, "1", 0, "E0225", infer)]),
    "repeat_of_a_unit_call": (["let v = [nothing(); 2];"], [(0, "nothing()", 0, "E0226", unit)]),
    "repeat_of_an_unknown_name": (["let v = [nope; 2];"], [(0, "nope", 0, "E0230", unk("nope"))]),
    "array_whose_first_element_is_unknown": (["let v = [nope, 1];"], [(0, "nope", 0, "E0230", unk("nope"))]),
}

if __name__ == "__main__":
    for name, (stmts, errs) in CASES.items():
        L = PRELUDE + ["fn t() {"] + ["    " + s for s in stmts] + ["}"]
        base = len(PRELUDE) + 2
        g.errors("one_error_" + name, L, [e(L, base + i, text, code, msg, occ) for i, text, occ, code, msg in errs])
