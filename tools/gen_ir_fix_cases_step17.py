# Generates tests/ir_fix/* for the typed-IR step 17 fixes after the m2-ir-v0 audit (findings 6, 7, 8a, 8b and the E0261 expected-type rule).
# Each .fixed.mn is computed by renaming whole words, independent of the C builder; .remaining is what the fixed source must still report
# (columns computed from the fixed source). Run from the repo root with PYTHONPATH=tools.
import gen_ir_fix_cases_step14 as f14
import gen_ir_goldens_step7 as g

write, rename, snake, pascal = f14.write, f14.rename, f14.snake, f14.pascal

if __name__ == "__main__":
    # Finding 6: a redeclaration (E0213) binds nothing, but a misnamed one is renamed with the first binding. What remains is the E0213 itself.
    L = ["manna v0;", "fn f() {", "    let myVar: u8 = 1;", "    let myVar: u8 = 2;", "}"]
    F = rename(L, {"myVar": "my_var"})
    write("fix_duplicate_let_is_renamed_with_the_first", L, F, [g.err(F, 4, "my_var", "E0213", "'my_var' is already declared in this function")])
    L = ["manna v0;", "fn f() {", "    let Ab: u8 = 1;", "    for let Ab: u8 = 0; Ab < 2; Ab = Ab + 1 {", "    }", "}"]
    F = rename(L, {"Ab": "ab"})
    write("fix_duplicate_loop_variable_is_renamed_with_the_first", L, F, [g.err(F, 4, "ab", "E0213", "'ab' is already declared in this function")])
    L = ["manna v0;", "fn f() {", "    let myVar: u8 = 1;", "    let myVar: u8 = 2;", "    let myVar: u8 = 3;", "    myVar = 4;", "}"]
    F = rename(L, {"myVar": "my_var"})
    write("fix_three_declarations_of_one_name", L, F, [g.err(F, 4, "my_var", "E0213", "'my_var' is already declared in this function"),
                                                       g.err(F, 5, "my_var", "E0213", "'my_var' is already declared in this function")])
    # Finding 8a: a function or a struct used as a value (E0231) is a resolved use, so the rename reaches it.
    L = ["manna v0;", "fn doIt() {", "}", "fn g() {", "    let x: u8 = doIt;", "}"]
    F = rename(L, {"doIt": "do_it"})
    write("fix_function_used_as_a_value", L, F, [g.err(F, 5, "do_it", "E0231", "'do_it' is a function, not a value")])
    L = ["manna v0;", "struct my_s { a: u8, }", "fn f() {", "    let x: u8 = my_s;", "}"]
    F = rename(L, {"my_s": "MyS"})
    write("fix_struct_used_as_a_value", L, F, [g.err(F, 4, "MyS", "E0231", "'MyS' is a struct, not a value")])
    # Finding 8b: a field given twice in a construction (E0244): the second name still resolves to the field.
    L = ["manna v0;", "struct P { aB: u8, }", "fn f() {", "    let p = P(aB: 1, aB: 2);", "}"]
    F = rename(L, {"aB": "a_b"})
    write("fix_field_given_twice_in_a_construction", L, F, [g.err(F, 4, "a_b", "E0244", "field 'a_b' given twice", 1)])

    # Finding 4: E0261's fix, when the expression has an expected type, must make the result that type by one lossless widening.
    L = ["manna v0;", "fn take(x: u32) -> u32 {", "    return x;", "}",
         "fn ret(a: u8, b: u32) -> u32 {", "    return a + b;", "}",
         "fn ann(a: u8, b: u32) {", "    let r: u32 = a * b;", "}",
         "fn arg(a: u8, b: u32) -> u32 {", "    return take(a & b);", "}",
         "fn asg(a: u32, b: u8) {", "    let r: u32 = 0;", "    r = a | b;", "}",
         "fn shl(a: u32, b: u8) -> u32 {", "    return a << b;", "}",
         "fn cmp(a: i8, b: i32) {", "    if a == b {", "    }", "}"]
    F = list(L)
    F[5] = "    return u32(a) + b;"; F[8] = "    let r: u32 = u32(a) * b;"; F[11] = "    return take(u32(a) & b);"
    F[15] = "    r = a | u32(b);"; F[18] = "    return a << u32(b);"; F[21] = "    if i32(a) == b {"
    write("fix_e0261_with_an_expected_type_widens_to_it", L, F)
    # No single lossless wrap makes the result the expected type: no fix (the E0261 stays, nothing else is added).
    L = ["manna v0;", "fn reviewer(a: u8, b: u8) -> u8 {", "    return u32(a) + b;", "}",
         "fn narrow(a: u32, b: u8) -> u8 {", "    return a + b;", "}",
         "fn sign(a: u8, b: i8) -> u8 {", "    return a + b;", "}",
         "fn count(a: u8, b: u32) -> u8 {", "    return a << b;", "}",
         "fn cmp(a: u8, b: i8) {", "    if a < b {", "    }", "}",
         "fn nonint(a: u8, b: u32) {", "    let r: bool = a + b;", "}"]
    e261 = lambda x, y: "operands have different types: %s and %s; convert one explicitly" % (x, y)
    write("fix_e0261_no_fix_when_no_lossless_wrap_gives_the_expected_type", L, L,
          [g.err(L, 3, "u32(a) + b", "E0261", e261("u32", "u8")), g.err(L, 6, "a + b", "E0261", e261("u32", "u8")), g.err(L, 9, "a + b", "E0261", e261("u8", "i8")),
           g.err(L, 12, "a << b", "E0261", e261("u8", "u32")), g.err(L, 15, "a < b", "E0261", e261("u8", "i8")), g.err(L, 19, "a + b", "E0261", e261("u8", "u32"))])
