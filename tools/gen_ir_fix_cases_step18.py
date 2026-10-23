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

    # ---- a rename must not change what a use resolves to (audit findings F3a, F3b, d1). The auditor's inputs: no fix is offered where
    # a local would capture the use of a function (3a) or where a renamed local would capture a use of a function (3b) or of an unknown name.
    unknown = lambda n: "unknown name '%s'" % n
    L = ["manna v0;", "fn helperFn() -> u8 {", "    return 7;", "}", "fn main() -> u8 {", "    let helper_fn: u8 = 1;", "    let x: u8 = helperFn;", "    return x;", "}"]
    write("fix_function_rename_that_a_visible_local_would_capture", L, L,
          [g.err(L, 2, "helperFn", "E0280", snake("helperFn")), g.err(L, 7, "helperFn", "E0231", "'helperFn' is a function, not a value")])
    L = ["manna v0;", "fn helper_fn() -> u8 {", "    return 7;", "}", "fn main() -> u8 {", "    let helperFn: u8 = 1;", "    let x: u8 = helper_fn;", "    return x + helperFn;", "}"]
    write("fix_local_rename_onto_a_function_used_as_a_value", L, L,
          [g.err(L, 6, "helperFn", "E0280", snake("helperFn")), g.err(L, 7, "helper_fn", "E0231", "'helper_fn' is a function, not a value")])
    L = ["manna v0;", "fn f(c: bool) -> u8 {", "    if c {", "        let item_count: u8 = 1;", "    }", "    if c {", "        let itemCount: u8 = 2;", "        return item_count;", "    }", "    return 0;", "}"]
    write("fix_local_rename_onto_an_unknown_name_used_in_its_scope", L, L,
          [g.err(L, 7, "itemCount", "E0280", snake("itemCount")), g.err(L, 8, "item_count", "E0230", unknown("item_count"))])
    L = ["manna v0;", "fn f(itemCount: u8) -> u8 {", "    return item_count;", "}"]
    write("fix_parameter_rename_onto_an_unknown_name_used_in_the_body", L, L,
          [g.err(L, 2, "itemCount", "E0280", snake("itemCount")), g.err(L, 3, "item_count", "E0230", unknown("item_count"))])
    # A struct and a local swap names: each rename would land on the other's name, and the use of the struct as a value would be captured.
    L = ["manna v0;", "struct my_s {", "    a: u8,", "}", "fn f() {", "    let MyS: u8 = 1;", "    let x: u8 = my_s;", "}"]
    write("fix_struct_and_local_that_would_swap_names", L, L,
          [g.err(L, 2, "my_s", "E0281", pascal("my_s")), g.err(L, 6, "MyS", "E0280", snake("MyS")), g.err(L, 7, "my_s", "E0231", "'my_s' is a struct, not a value")])
    # Controls: where the name is not visible at the use (the local is declared later, or its block is closed), the rename is safe and is offered.
    L = ["manna v0;", "fn doIt() -> u8 {", "    return 1;", "}", "fn f() {", "    let x: u8 = doIt;", "    let do_it: u8 = 2;", "}"]
    write("fix_function_rename_is_offered_when_the_local_is_declared_after_the_use", L, rename(L, {"doIt": "do_it"}),
          [g.err(rename(L, {"doIt": "do_it"}), 6, "do_it", "E0231", "'do_it' is a function, not a value")])
    L = ["manna v0;", "fn f(c: bool) -> u8 {", "    let y: u8 = item_count;", "    let itemCount: u8 = 2;", "    return itemCount;", "}"]
    F = rename(L, {"itemCount": "item_count"})
    F[2] = "    let y: u8 = item_count;"
    write("fix_local_rename_is_offered_when_the_unknown_name_comes_before_its_declaration", L, F, [g.err(F, 3, "item_count", "E0230", unknown("item_count"))])

    # ---- a rename must not make a name that resolved to nothing start to resolve (the property test's finding): an unknown name, function,
    # type or field used ANYWHERE blocks the rename whose target it spells. Each case is its own program (a declaration-pass error would
    # stop the bodies being read).
    L = ["manna v0;", "fn helperFn() -> u8 {", "    return 1;", "}", "fn g() -> u8 {", "    return helper_fn;", "}"]
    write("fix_function_rename_onto_an_unknown_name_used_elsewhere", L, L,
          [g.err(L, 2, "helperFn", "E0280", snake("helperFn")), g.err(L, 6, "helper_fn", "E0230", unknown("helper_fn"))])
    L = ["manna v0;", "fn helperFn() -> u8 {", "    return 1;", "}", "fn g() {", "    helper_fn();", "}"]
    write("fix_function_rename_onto_an_unknown_callee", L, L,
          [g.err(L, 2, "helperFn", "E0280", snake("helperFn")), g.err(L, 6, "helper_fn", "E0232", "unknown function 'helper_fn'")])
    L = ["manna v0;", "struct my_s {", "    a: u8,", "}", "fn k() {", "    let a: MyS = 1;", "}"]
    write("fix_struct_rename_onto_an_unknown_type", L, L,
          [g.err(L, 2, "my_s", "E0281", pascal("my_s")), g.err(L, 6, "MyS", "E0220", "unknown type 'MyS'")])
    L = ["manna v0;", "struct P {", "    fooBar: u8,", "}", "fn m(p: P) -> u8 {", "    return p.foo_bar;", "}"]
    write("fix_field_rename_onto_an_unknown_field", L, L,
          [g.err(L, 3, "fooBar", "E0280", snake("fooBar")), g.err(L, 6, "foo_bar", "E0242", "struct 'P' has no field 'foo_bar'")])
    L = ["manna v0;", "struct P {", "    fooBar: u8,", "}", "fn n() -> P {", "    return P(foo_bar: 1);", "}"]
    write("fix_field_rename_onto_an_unknown_construction_field", L, L,
          [g.err(L, 3, "fooBar", "E0280", snake("fooBar")), g.err(L, 6, "foo_bar", "E0242", "struct 'P' has no field 'foo_bar'")])

    # ---- more E0261 shapes (these cover every branch of the fix-context code: the signed place under unary minus, a fix in the right operand of
    # an enclosing operator kept or withdrawn, a fix in both operands withdrawn).
    L = ["manna v0;", "fn f(a: i8, b: i16) {", "    let r = -(a + b);", "}"]
    F = list(L); F[2] = "    let r = -(i16(a) + b);"
    write("fix_e0261_under_unary_minus_in_an_unconstrained_value_needs_a_signed_type", L, F)
    L = ["manna v0;", "fn f(a: u8, b: u16) {", "    let r = -(a + b);", "}", "fn g(a: u8, b: u16) -> u16 {", "    return -(a + b);", "}"]
    write("fix_e0261_under_unary_minus_no_fix_when_the_type_would_be_unsigned", L, L,
          [g.err(L, 3, "(a + b)", "E0261", e261("u8", "u16")), g.err(L, 6, "(a + b)", "E0261", e261("u8", "u16"))])
    L = ["manna v0;", "fn f(a: u8, b: u16, c: u16) -> u16 {", "    return c + (a + b);", "}"]
    F = list(L); F[2] = "    return c + (u16(a) + b);"
    write("fix_e0261_in_the_right_operand_of_an_operator_that_accepts_it", L, F)
    L = ["manna v0;", "fn f(a: u8, b: u16, c: u8) -> u16 {", "    return c + (a + b);", "}"]
    write("fix_e0261_in_the_right_operand_withdrawn_when_the_left_has_another_type", L, L, [g.err(L, 3, "(a + b)", "E0261", e261("u8", "u16"))])
    L = ["manna v0;", "fn f(a: u8, b: u16, c: u8, d: u16) -> u16 {", "    return (a + b) + (c + d);", "}"]
    write("fix_e0261_two_fixes_inside_one_operator_are_both_withdrawn", L, L,
          [g.err(L, 3, "(a + b)", "E0261", e261("u8", "u16")), g.err(L, 3, "(c + d)", "E0261", e261("u8", "u16"))])
    # A misnamed local whose block closed, then the same name misnamed again in a sibling block that also uses a name declared nowhere:
    # only the second is visible at the use, and its rename is blocked only if it spells that name.
    L = ["manna v0;", "fn f(c: bool) {", "    if c {", "        let aB: u8 = 1;", "    }", "    if c {", "        let aB: u8 = 2;", "        let y: u8 = zz;", "    }", "}"]
    write("fix_sibling_blocks_with_the_same_misnamed_local_and_an_unknown_name", L, rename(L, {"aB": "a_b"}), [g.err(rename(L, {"aB": "a_b"}), 8, "zz", "E0230", unknown("zz"))])
    # The same two blocks, the unknown name spelling the target: only the block that is open at the use is blocked; the closed one is not
    # (the lookup by name finds the open block's local, a different slot, so the closed block's rename is not mistaken for a visible one).
    L = ["manna v0;", "fn f(c: bool) {", "    if c {", "        let aB: u8 = 1;", "    }", "    if c {", "        let aB: u8 = 2;", "        let y: u8 = a_b;", "    }", "}"]
    F = list(L); F[3] = "        let a_b: u8 = 1;"
    write("fix_only_the_open_block_is_blocked_by_an_unknown_name_spelling_its_target", L, F,
          [g.err(F, 7, "aB", "E0280", snake("aB")), g.err(F, 8, "a_b", "E0230", unknown("a_b"))])
