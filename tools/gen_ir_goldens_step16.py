# Generates the typed-IR step 16 goldens (tests/ir/*) that close three coverage gaps found by `make coverage`: a call cycle entered
# from a later function (ir_calls.c, the lowest member of the component), and slicing an array whose elements are arrays
# (ir_expr.c contains_slice); plus the one golden Admission record #9 requires that did not exist: an index of each integer type. Columns come from the source text, independent of the C builder; run from the repo root with PYTHONPATH=tools.
import gen_ir_goldens_step7 as g
import gen_ir_goldens_step10 as c
import gen_ir_goldens_step11 as s11
import gen_ir_goldens_step12 as s12

plain, cl, nm = c.plain, c.cl, c.nm
let = s11.let
idx = s12.idx
assign = s11.assign
e = s12.e
INTS = ["u8", "i8", "u16", "i16", "u32", "i32", "u64", "i64"]
rec = "recursion is not allowed in v0"

if __name__ == "__main__":
    # a is outside the cycle and calls c; c calls b; b calls c. The depth-first search enters the cycle at c (index 2), so the first
    # member on the component's stack is not its lowest: the error is still reported at the lowest member, b, at b's call.
    L = ["manna v0;", "fn a() {", "    c();", "}", "fn b() {", "    c();", "}", "fn c() {", "    b();", "}"]
    g.errors("recursion_cycle_entered_from_a_later_function", L, [g.err(L, 6, "c()", "E0235", rec)])

    L = ["manna v0;", "fn f(a: [3][2]u8) {", "    let s = a[1..3];", "}"]
    g.put("slice_of_an_array_of_arrays", L,
          plain(L, 2, 4, "f", [("a: [3][2]u8", "a", "[3][2]u8")], "",
                let(L, 3, "let s = a[1..3];", "s", 1, "[][2]u8",
                    g.N("slice", g.sp(L, 3, "a[1..3]"), "[][2]u8", cl(L, 3, "a", 0, "[3][2]u8"), nm(L, 3, "1", "u64"), nm(L, 3, "3", "u64")))))

    # Admission #9: an index of each integer type (the other index goldens use u8, u32 and i64 only)
    params = ["a: [4]u8"] + ["%s: %s" % ("p%d" % i, t) for i, t in enumerate(INTS)]
    L = ["manna v0;", "fn f(%s) {" % ", ".join(params)] + ["    let x%d = a[p%d];" % (i, i) for i in range(8)] + ["}"]
    lets = [let(L, 3 + i, "let x%d = a[p%d];" % (i, i), "x%d" % i, 9 + i, "u8", idx(L, 3 + i, "a[p%d]" % i, "u8", cl(L, 3 + i, "a", 0, "[4]u8"), cl(L, 3 + i, "p%d" % i, 1 + i, INTS[i]))) for i in range(8)]
    g.put("index_every_integer_type", L, plain(L, 2, len(L), "f", [("a: [4]u8", "a", "[4]u8")] + [("p%d: %s" % (i, t), "p%d" % i, t) for i, t in enumerate(INTS)], "", *lets))

    # Admission #10: an array is copied by `=` (the other copy goldens are `let`, argument and return)
    L = ["manna v0;", "fn f(a: [2]u8, b: [2]u8) {", "    a = b;", "}"]
    g.put("array_is_a_value_assignment_copy", L,
          plain(L, 2, 4, "f", [("a: [2]u8", "a", "[2]u8"), ("b: [2]u8", "b", "[2]u8")], "",
                assign(L, 3, "a = b;", cl(L, 3, "a", 0, "[2]u8"), cl(L, 3, "b", 1, "[2]u8"))))

    # Admission #9: `==` and `!=` are not defined on arrays or slices (the other equality goldens use a struct)
    L = ["manna v0;", "fn f(a: [2]u8, b: [2]u8, s: []u8, t: []u8) {", "    let r = a == b;", "    let q = s == t;", "    let w = a != b;", "}"]
    g.errors("op_eq_is_not_defined_on_arrays_and_slices", L, [e(L, 3, "a == b", "E0260", "operator '==' is not defined for [2]u8"),
        e(L, 4, "s == t", "E0260", "operator '==' is not defined for []u8"), e(L, 5, "a != b", "E0260", "operator '!=' is not defined for [2]u8")])

    # Admission #11: sizes over the limit by a large margin. S and T are nested arrays whose INNER array is already over the limit (reported at the
    # inner type, before any multiplication: the audit's finding 9); U is 2^63 u16 elements (2^63 * 2 = 2^64, the one-level product that would
    # wrap; the check is a division, so it never multiplies); V is 2^63 u8 elements. The nested product that wraps (an in-limit inner array, an
    # outer length of 2^48) is size_overflow_in_the_outer_multiplication (step 17).
    big = "type is too large (size exceeds the limit)"
    L = ["manna v0;", "struct S { a: [4294967296][4294967296]u8, }", "struct T { a: [4294967297][4294967297]u8, }", "struct U { a: [9223372036854775808]u16, }", "struct V { a: [9223372036854775808]u8, }"]
    g.errors("size_computation_overflowing_u64", L, [e(L, 2, "[4294967296]u8", "E0222", big), e(L, 3, "[4294967297]u8", "E0222", big),
        e(L, 4, "[9223372036854775808]u16", "E0222", big), e(L, 5, "[9223372036854775808]u8", "E0222", big)])
