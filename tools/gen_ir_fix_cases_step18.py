# Generates tests/ir_fix/* for the typed-IR step 18 fixes after the m2-ir-v0 re-audit: E0261's fix inside nested checks (findings F1, F4),
# parentheses kept by a rename (F2) and rename collisions across kinds (F3). Each .fixed.mn is written by hand here (independent of the C
# builder); .remaining is what the fixed source must still report, columns computed from the fixed source. Run from the repo root with
# PYTHONPATH=tools.
import gen_ir_fix_cases_step14 as f14
import gen_ir_goldens_step7 as g

write, rename, snake, pascal = f14.write, f14.rename, f14.snake, f14.pascal
e261 = lambda x, y: "operands have different types: %s and %s; convert one explicitly" % (x, y)

if __name__ == "__main__":
    # ---- E0261: the expected type must survive a nested check (audit findings F1a-F1e). The auditor's inputs; each has no fix, because
    # the only lossless wrap changes the type the context needs, and the E0261 stays as the only diagnostic.
    G = ["fn g(x: u8) -> u8 {", "    return x;", "}"]
    L = ["manna v0;"] + G + ["fn f(a: u8, b: u8, c: u16) -> u8 {", "    return g(a + b) + c;", "}"]
    write("fix_e0261_nested_call_argument_keeps_the_expected_type", L, L, [g.err(L, 6, "g(a + b) + c", "E0261", e261("u8", "u16"))])
    L = ["manna v0;"] + G + ["fn f(a: u8, b: u8, d: i8) -> bool {", "    if g(a + b) < d {", "        return true;", "    }", "    return false;", "}"]
    write("fix_e0261_comparison_with_no_lossless_widening_has_no_fix", L, L, [g.err(L, 6, "g(a + b) < d", "E0261", e261("u8", "i8"))])
    L = ["manna v0;"] + G + ["fn f(a: u8, b: u8, c: u32) -> u8 {", "    return g(a + b) << c;", "}"]
    write("fix_e0261_shift_count_is_never_narrowed", L, L, [g.err(L, 6, "g(a + b) << c", "E0261", e261("u8", "u32"))])
    L = ["manna v0;", "struct P {", "    x: u8,", "}", "fn f(a: u8, b: u8, c: u16) -> u8 {", "    return P(x: a + b).x + c;", "}"]
    write("fix_e0261_construction_field_keeps_the_expected_type", L, L, [g.err(L, 6, "P(x: a + b).x + c", "E0261", e261("u8", "u16"))])
    L = ["manna v0;", "fn f(a: u8, b: u8, d: u16) {", "    let r: u8 = [a, a + b][1] + d;", "}"]
    write("fix_e0261_array_element_keeps_the_expected_type", L, L, [g.err(L, 3, "[a, a + b][1] + d", "E0261", e261("u8", "u16"))])
    # F4a / F4b: the fix is inside an enclosing operator; it must leave that operator, and the place's type, valid.
    L = ["manna v0;", "fn f(a: u8, b: u16, c: u8) -> u8 {", "    return a + b + c;", "}"]
    write("fix_e0261_inner_operand_would_not_make_the_result_the_expected_type", L, L, [g.err(L, 3, "a + b", "E0261", e261("u8", "u16"))])
    # (a parenthesized operand's span includes its parentheses, so the error starts at the "(")
    L = ["manna v0;", "fn f(a: i8, b: i16) -> i8 {", "    return -(a + b);", "}"]
    write("fix_e0261_under_unary_minus_keeps_the_expected_type", L, L, [g.err(L, 3, "(a + b)", "E0261", e261("i8", "i16"))])
    # The same shapes where the wrap IS valid: applied, and the fixed source builds clean.
    L = ["manna v0;"] + G + ["fn f(a: u8, b: u8, c: u16) -> u16 {", "    return g(a + b) + c;", "}",
         "fn h(a: u8, b: u16, c: u16) -> u16 {", "    return a + b + c;", "}",
         "fn k(a: i8, b: i16) -> i16 {", "    return -(a + b);", "}",
         "fn m(a: u8, b: u16) {", "    let r = a + b;", "}"]
    F = list(L)
    F[5] = "    return u16(g(a + b)) + c;"; F[8] = "    return u16(a) + b + c;"; F[11] = "    return -(i16(a) + b);"; F[14] = "    let r = u16(a) + b;"
    write("fix_e0261_nested_shapes_where_the_wrap_is_valid", L, F)
    # A fix inside an operator whose other operand has another type is withdrawn: wrapping it would make THAT operator fail.
    L = ["manna v0;", "fn f(a: u8, b: u16, c: u8) -> u16 {", "    return a + b + c;", "}", "fn g(a: u8, b: u16, c: u8) {", "    let r = a + b + c;", "}"]
    write("fix_e0261_withdrawn_when_the_enclosing_operator_would_fail", L, L,
          [g.err(L, 3, "a + b", "E0261", e261("u8", "u16")), g.err(L, 6, "a + b", "E0261", e261("u8", "u16"))])

    # ---- a rename edits the name token only: parentheses around a use stay (audit findings F2a, F2b). The target is not assignable, so
    # the E0270 stays after the fix, at the (parenthesized) target.
    L = ["manna v0;", "fn f() -> u8 {", "    let myVar: u8 = 1;", "    (myVar) = 2;", "    return myVar;", "}"]
    F = rename(L, {"myVar": "my_var"})
    write("fix_rename_keeps_the_parentheses_of_a_bad_target", L, F, [g.err(F, 4, "(my_var)", "E0270", "can't assign to this expression")])
    L = ["manna v0;", "struct P {", "    x: u8,", "}", "fn f() -> u8 {", "    let myP: P = P(x: 1);", "    (myP).x = 2;", "    (myP)[0] = 3;", "    return myP.x;", "}"]
    F = rename(L, {"myP": "my_p"})
    write("fix_rename_keeps_the_parentheses_of_a_field_and_an_index_target", L, F,
          [g.err(F, 7, "(my_p).x", "E0270", "can't assign to this expression"), g.err(F, 8, "(my_p)[0]", "E0270", "can't assign to this expression")])
    L = ["manna v0;", "fn doIt() -> u8 {", "    return 1;", "}", "fn g() -> u8 {", "    let myVar: u8 = 1;", "    ( ( myVar ) ) = 2;", "    let x: u8 = ( myVar );", "    let y: u8 = (doIt);", "    return ((myVar));", "}"]
    F = rename(L, {"myVar": "my_var", "doIt": "do_it"})
    write("fix_rename_keeps_nested_and_spaced_parentheses", L, F,
          [g.err(F, 7, "( ( my_var ) )", "E0270", "can't assign to this expression"), g.err(F, 9, "(do_it)", "E0231", "'do_it' is a function, not a value")])
