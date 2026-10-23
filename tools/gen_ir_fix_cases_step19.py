# Generates tests/ir_fix/* for the typed-IR step 19 fixes after the third audit round (re-audit of e45c0bc): the rename bookkeeping's memory
# (finding 1), an insert never joining two tokens (2), E0261 in an un-annotated let (3), literal siblings (4), the error cap (5) and stale
# spellings in a rename (6). Each .fixed.mn is written by hand here (independent of the C builder); .remaining is what the fixed source must still
# report, columns computed from the fixed source. Run from the repo root with PYTHONPATH=tools.
import gen_ir_fix_cases_step14 as f14
import gen_ir_goldens_step7 as g

write, rename, snake, pascal = f14.write, f14.rename, f14.snake, f14.pascal
e261 = lambda x, y: "operands have different types: %s and %s; convert one explicitly" % (x, y)
notlv = "can't assign to this expression"

if __name__ == "__main__":
    # ---- 19a (finding 1): uses of names that are not locals, in muted targets, while misnamed declarations are visible. The auditor's inputs
    # (mem_min, mem_fn) are scaled down here and run at full size by `make test-ir-memory-stress`; the renames must still be offered.
    L = ["manna v0;", "fn f() {", "    let myA: u8 = 0;", "    let myB: u8 = 0;", "    g(" + ", ".join(["zz"] * 40) + ") = myA + myB;", "}"]
    F = rename(L, {"myA": "my_a", "myB": "my_b"})
    write("fix_locals_renamed_next_to_many_uses_of_an_unknown_name", L, F, [g.err(F, 5, "g(" + ", ".join(["zz"] * 40) + ")", "E0270", notlv)])
    L = ["manna v0;", "fn doIt() -> u8 {", "    return 1;", "}", "fn f() {", "    g(" + ", ".join(["doIt"] * 40) + ") = 1;", "}"]
    F = rename(L, {"doIt": "do_it"})
    write("fix_function_renamed_next_to_many_value_uses", L, F, [g.err(F, 6, "g(" + ", ".join(["do_it"] * 40) + ")", "E0270", notlv)])

    # ---- 19b (finding 2): the inserted `T(` never joins the keyword before the operand (the applier adds a space). The auditor's k1-k4 and
    # `else if`. The fixed source is written with the space; the wrap itself is the ordinary one.
    L = ["manna v0;", "fn f(a: i8, b: i32) -> i32 {", "    return-a + b;", "}"]
    F = list(L); F[2] = "    return i32(-a) + b;"
    write("fix_e0261_insert_after_return_without_a_space", L, F)
    L = ["manna v0;", "fn f(a: u8, b: u32) -> u32 {", "    return(a) + b;", "}"]
    F = list(L); F[2] = "    return u32((a)) + b;"
    write("fix_e0261_insert_after_return_before_a_parenthesis", L, F)
    L = ["manna v0;", "fn f(a: u8, b: u32) {", "    if(a) < b {", "    }", "    while(a) < b {", "    }", "}"]
    F = list(L); F[2] = "    if u32((a)) < b {"; F[4] = "    while u32((a)) < b {"
    write("fix_e0261_insert_after_if_and_while_without_a_space", L, F)
    L = ["manna v0;", "fn f(a: u8, b: u32, c: bool) {", "    if c {", "    } else if(a) < b {", "    }", "}"]
    F = list(L); F[3] = "    } else if u32((a)) < b {"
    write("fix_e0261_insert_after_else_if_without_a_space", L, F)

    # ---- 19c-1 (finding 3): the type of an un-annotated `let` / `for let` initializer becomes the LOCAL's type, which every later use is
    # checked against, so the E0261 fix is not offered there (an expression statement is still free; a comparison is bool either way).
    L = ["manna v0;", "fn f(a: u8, b: u32) -> u8 {", "    let r = a + b;", "    return r;", "}"]
    write("fix_e0261_in_an_unannotated_let_has_no_fix_the_local_is_used_later", L, L, [g.err(L, 3, "a + b", "E0261", e261("u8", "u32"))])
    L = ["manna v0;", "fn f(a: u8, b: u32, n: u8) {", "    for let i = a + b; i < n; i = i + 1 {", "    }", "}"]
    write("fix_e0261_in_an_unannotated_for_let_has_no_fix", L, L, [g.err(L, 3, "a + b", "E0261", e261("u8", "u32"))])
    L = ["manna v0;", "fn g(x: u8) {", "}", "fn f(a: u8, b: u32) {", "    let r = a + b;", "    g(r);", "}"]
    write("fix_e0261_in_an_unannotated_let_has_no_fix_the_local_is_an_argument", L, L, [g.err(L, 5, "a + b", "E0261", e261("u8", "u32"))])
    L = ["manna v0;", "fn f(a: i8, b: i16) {", "    let r = -(a + b);", "}"]
    write("fix_e0261_under_unary_minus_in_an_unannotated_let_has_no_fix", L, L, [g.err(L, 3, "(a + b)", "E0261", e261("i8", "i16"))])
    L = ["manna v0;", "fn f(a: u8, b: u32) {", "    let r = a < b;", "}"]
    F = list(L); F[2] = "    let r = u32(a) < b;"
    write("fix_e0261_comparison_in_an_unannotated_let_keeps_its_fix_bool_either_way", L, F)

    # ---- 19c-2 (finding 4): a pending fix gives the operator the place's type; an untyped literal group beside it takes that type and must fit.
    L = ["manna v0;", "fn f(a: u8, b: u16) -> u16 {", "    return (a + b) + 70000;", "}"]
    write("fix_e0261_withdrawn_when_a_literal_beside_it_does_not_fit_the_new_type", L, L, [g.err(L, 3, "(a + b)", "E0261", e261("u8", "u16"))])
    L = ["manna v0;", "fn f(a: u8, b: u32) -> u32 {", "    return (a + b) * -1;", "}"]
    write("fix_e0261_withdrawn_when_a_negated_literal_beside_it_meets_an_unsigned_type", L, L, [g.err(L, 3, "(a + b)", "E0261", e261("u8", "u32"))])
    L = ["manna v0;", "fn f(a: i16, b: i8) -> i16 {", "    return (a - b) >> 70000;", "}"]
    write("fix_e0261_withdrawn_when_the_shift_count_literal_does_not_fit", L, L, [g.err(L, 3, "(a - b)", "E0261", e261("i16", "i8"))])
    L = ["manna v0;", "fn f(p: i16, q: u8) {", "    let r: i16 = 70000 + (p & q);", "}"]
    write("fix_e0261_withdrawn_when_a_literal_on_the_left_does_not_fit", L, L, [g.err(L, 3, "(p & q)", "E0261", e261("i16", "u8"))])
    L = ["manna v0;", "fn f(p: i16, q: u8) -> i16 {", "    return (p & q) | (100 << 40000);", "}"]
    write("fix_e0261_withdrawn_when_a_group_of_literals_beside_it_does_not_fit", L, L, [g.err(L, 3, "(p & q)", "E0261", e261("i16", "u8"))])
    # controls: the literal fits (or is negatable), so the fix stays
    L = ["manna v0;", "fn f(a: u8, b: u16, c: i8, d: i16) -> u16 {", "    let e: i16 = (c + d) * -1;", "    let h: i16 = 1 + (c + d);", "    return (a + b) + 7;", "}"]
    F = list(L); F[2] = "    let e: i16 = (i16(c) + d) * -1;"; F[3] = "    let h: i16 = 1 + (i16(c) + d);"; F[4] = "    return (u16(a) + b) + 7;"
    write("fix_e0261_kept_when_the_literal_beside_it_fits_the_new_type", L, F)

