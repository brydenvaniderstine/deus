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
    # ---- 19a (finding 1): uses of names that are not locals, in bad targets, while misnamed declarations are visible. The auditor's inputs (mem_min, mem_fn) are scaled
    # down here and run by `make test-ir-memory-stress`; the renames must still be offered. Since C11 (Bryden 2026-10-27) every use in a target is REPORTED, so the 20-error
    # cap bounds how many uses one build can note: these cases hold 12 uses (the 40 of the muted days would stop the build at E0203 and offer no fix).
    uses = 12
    L = ["manna v0;", "fn f() {", "    let myA: u8 = 0;", "    let myB: u8 = 0;", "    g(" + ", ".join(["zz"] * uses) + ") = myA + myB;", "}"]
    F = rename(L, {"myA": "my_a", "myB": "my_b"})
    t = "g(" + ", ".join(["zz"] * uses) + ")"
    write("fix_locals_renamed_next_to_many_uses_of_an_unknown_name", L, F, [g.err(F, 5, t, "E0270", notlv), g.err(F, 5, "g", "E0232", "unknown function 'g'")] +
          [g.err(F, 5, "zz", "E0230", "unknown name 'zz'", occ=i) for i in range(uses)])
    L = ["manna v0;", "fn doIt() -> u8 {", "    return 1;", "}", "fn f() {", "    g(" + ", ".join(["doIt"] * uses) + ") = 1;", "}"]
    F = rename(L, {"doIt": "do_it"})
    t = "g(" + ", ".join(["do_it"] * uses) + ")"
    write("fix_function_renamed_next_to_many_value_uses", L, F, [g.err(F, 6, t, "E0270", notlv), g.err(F, 6, "g", "E0232", "unknown function 'g'")] +
          [g.err(F, 6, "do_it", "E0231", "'do_it' is a function, not a value", occ=i) for i in range(uses)])

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
    # checked against. The E0261 fix is attached and WAITS for those uses (IrLocal.fix_diag): the first use that rejects the new type
    # withdraws it, and a let whose uses all accept it keeps it. A comparison is bool either way (no waiting).
    def keep(name, L, edits):
        F = list(L)
        for ln, text in edits.items(): F[ln - 1] = text
        write(name, L, F)
    def drop(name, L, ln, tok, x, y):
        write(name, L, L, [g.err(L, ln, tok, "E0261", e261(x, y))])
    # rejected: the auditor's t1, t2, t3, and the other ways a later use can disagree
    L = ["manna v0;", "fn f(a: u8, b: u32) -> u8 {", "    let r = a + b;", "    return r;", "}"]
    drop("fix_e0261_in_an_unannotated_let_has_no_fix_the_local_is_returned_as_another_type", L, 3, "a + b", "u8", "u32")
    L = ["manna v0;", "fn f(a: u8, b: u32, n: u8) {", "    for let i = a + b; i < n; i = i + 1 {", "    }", "}"]
    drop("fix_e0261_in_an_unannotated_for_let_has_no_fix_the_condition_compares_another_type", L, 3, "a + b", "u8", "u32")
    L = ["manna v0;", "fn g(x: u8) {", "}", "fn f(a: u8, b: u32) {", "    let r = a + b;", "    g(r);", "}"]
    drop("fix_e0261_in_an_unannotated_let_has_no_fix_the_local_is_an_argument_of_another_type", L, 5, "a + b", "u8", "u32")
    L = ["manna v0;", "fn f(a: u8, b: u32, d: u16) {", "    let r = a + b;", "    let q = r + d;", "}"]
    drop("fix_e0261_in_an_unannotated_let_has_no_fix_a_later_operator_mixes_it_with_another_type", L, 3, "a + b", "u8", "u32")
    L = ["manna v0;", "fn f(a: u8, b: u32, d: u16) {", "    let r = a + b;", "    r = d;", "}"]
    drop("fix_e0261_in_an_unannotated_let_has_no_fix_it_is_assigned_another_type", L, 3, "a + b", "u8", "u32")
    L = ["manna v0;", "fn f(a: u8, b: u32) {", "    let r = a + b;", "    if r {", "    }", "}"]
    drop("fix_e0261_in_an_unannotated_let_has_no_fix_it_is_a_condition", L, 3, "a + b", "u8", "u32")
    L = ["manna v0;", "fn f(a: u8, b: u32) -> u8 {", "    let r = a + b;", "    let s = r;", "    return s;", "}"]
    drop("fix_e0261_in_an_unannotated_let_has_no_fix_a_copy_of_it_is_returned_as_another_type", L, 3, "a + b", "u8", "u32")
    L = ["manna v0;", "fn f(a: u8, b: u16) -> u16 {", "    let r = a + b;", "    return r + 70000;", "}"]
    drop("fix_e0261_in_an_unannotated_let_has_no_fix_a_literal_beside_it_does_not_fit", L, 3, "a + b", "u8", "u16")
    L = ["manna v0;", "fn f(a: u8, b: u16) -> u16 {", "    let r = a + b;", "    return -r;", "}"]
    drop("fix_e0261_in_an_unannotated_let_has_no_fix_it_is_negated_and_unsigned", L, 3, "a + b", "u8", "u16")
    L = ["manna v0;", "fn f(a: u8, b: u16, c: u8, d: u32) -> u32 {", "    let r = a + b;", "    let s = c + d;", "    return r + s;", "}"]
    write("fix_e0261_two_waiting_fixes_meeting_in_one_operator_are_both_withdrawn", L, L,
          [g.err(L, 3, "a + b", "E0261", e261("u8", "u16")), g.err(L, 4, "c + d", "E0261", e261("u8", "u32"))])
    L = ["manna v0;", "fn f(a: u8, b: u32) -> u8 {", "    let r = a + b;", "    let ok = r < 5;", "    return ok;", "}"]
    # step 21d (D1): the comparison is bool, so the `-> u8` that returns it reports its own mistake beside the E0261 (still no fix)
    write("fix_e0261_in_an_unannotated_let_has_no_fix_a_comparison_of_it_is_returned_as_another_type", L, L,
          [g.err(L, 3, "a + b", "E0261", e261("u8", "u32")), g.err(L, 5, "ok", "E0223", "type mismatch: expected u8, found bool")])
    L = ["manna v0;", "fn f(a: u8, b: u32, xs: [2]u8) {", "    let r = a + b;", "    len(r);", "}"]
    drop("fix_e0261_in_an_unannotated_let_has_no_fix_it_is_the_argument_of_len", L, 3, "a + b", "u8", "u32")
    L = ["manna v0;", "fn f(a: u8, b: u32) {", "    let r = a + b;", "    let xs = [r, 1];", "    let q = xs[0] + a;", "}"]
    drop("fix_e0261_in_an_unannotated_let_has_no_fix_it_is_an_array_element_that_decides_a_type", L, 3, "a + b", "u8", "u32")
    # accepted: every later use takes the wider type
    L = ["manna v0;", "fn f(a: u8, b: u32) -> u32 {", "    let r = a + b;", "    return r;", "}"]
    keep("fix_e0261_in_an_unannotated_let_keeps_its_fix_the_local_is_returned_as_that_type", L, {3: "    let r = u32(a) + b;"})
    L = ["manna v0;", "fn f(a: u8, b: u32) {", "    let r = a + b;", "}"]
    keep("fix_e0261_in_an_unannotated_let_keeps_its_fix_with_no_later_use", L, {3: "    let r = u32(a) + b;"})
    L = ["manna v0;", "fn f(a: u8, b: u32, n: u32) {", "    for let i = a + b; i < n; i = i + 1 {", "    }", "}"]
    keep("fix_e0261_in_an_unannotated_for_let_keeps_its_fix_condition_and_step_accept_the_type", L, {3: "    for let i = u32(a) + b; i < n; i = i + 1 {"})
    L = ["manna v0;", "fn g(x: u32) {", "}", "fn f(a: u8, b: u32, c: u32) {", "    let r = a + b;", "    let s: u32 = r;", "    let t = r + c;", "    g(t);", "    if r < c {", "    }", "}"]
    keep("fix_e0261_in_an_unannotated_let_keeps_its_fix_annotated_let_argument_operator_and_condition_accept_the_type", L, {5: "    let r = u32(a) + b;"})
    L = ["manna v0;", "fn f(a: u8, b: u32) -> u32 {", "    let r = a + b;", "    let s = r;", "    r = r + 1;", "    return s + r;", "}"]
    keep("fix_e0261_in_an_unannotated_let_keeps_its_fix_a_copy_an_assignment_with_a_literal_and_a_sum_accept_the_type", L, {3: "    let r = u32(a) + b;"})
    L = ["manna v0;", "fn f(a: u8, b: u32, xs: [3]u8) {", "    let r = a + b;", "    let q = u64(r);", "    xs[r];", "    xs[r..r];", "}"]
    keep("fix_e0261_in_an_unannotated_let_keeps_its_fix_a_conversion_an_index_and_a_slice_bound_accept_any_integer", L, {3: "    let r = u32(a) + b;"})
    L = ["manna v0;", "fn f(a: i8, b: i16) -> i16 {", "    let r = a + b;", "    return -r;", "}"]
    keep("fix_e0261_in_an_unannotated_let_keeps_its_fix_negation_of_a_signed_type", L, {3: "    let r = i16(a) + b;"})
    L = ["manna v0;", "fn f(a: u8, b: u32) -> bool {", "    let r = a + b;", "    let ok = r < 5;", "    return ok && r > 1;", "}"]
    keep("fix_e0261_in_an_unannotated_let_keeps_its_fix_comparisons_and_logic_accept_the_type", L, {3: "    let r = u32(a) + b;"})
    L = ["manna v0;", "fn f(a: u8, b: u32) {", "    let r = a < b;", "}"]
    keep("fix_e0261_comparison_in_an_unannotated_let_keeps_its_fix_bool_either_way", L, {3: "    let r = u32(a) < b;"})

    # the operator must be DEFINED for the type the waiting fix gives the operand, even when both operands are the same waiting local
    L = ["manna v0;", "fn f(a: u8, b: u32) {", "    let r = a + b;", "    let ok = r < 5;", "    let q = ok + ok;", "}"]
    # step 21d (D1): `ok` is bool even though `r` waits for its fix, so `ok + ok` is reported (E0260) and the E0261 KEEPS its fix (the case was renamed from "has_no_fix")
    F = list(L); F[2] = "    let r = u32(a) + b;"
    write("fix_e0261_in_an_unannotated_let_keeps_its_fix_arithmetic_on_the_bool_that_a_comparison_of_it_gives", L, F,
          [g.err(F, 5, "ok + ok", "E0260", "operator '+' is not defined for bool")])
    L = ["manna v0;", "fn f(a: u8, b: u32) {", "    let r = a + b;", "    let q = r && r;", "}"]
    drop("fix_e0261_in_an_unannotated_let_has_no_fix_it_is_both_operands_of_a_logical_and", L, 3, "a + b", "u8", "u32")
    # A sibling that is silent because of ANOTHER mistake has no known type yet: once that mistake is fixed it may disagree, so the fix is not
    # known to be accepted and is withdrawn (R2_g1 of the second audit has the same shape with an oversized `let`).
    unknown = lambda n: "unknown name '%s'" % n
    L = ["manna v0;", "fn f(a: u8, b: u16) -> u16 {", "    return (a + b) + nope;", "}"]
    write("fix_e0261_withdrawn_when_the_other_operand_is_silent_because_of_another_mistake", L, L,
          [g.err(L, 3, "(a + b)", "E0261", e261("u8", "u16")), g.err(L, 3, "nope", "E0230", unknown("nope"))])
    L = ["manna v0;", "fn f(a: u8, b: u16) {", "    let r = a + b;", "    let q = r + nope;", "}"]
    write("fix_e0261_in_an_unannotated_let_has_no_fix_a_later_operand_is_silent_because_of_another_mistake", L, L,
          [g.err(L, 3, "a + b", "E0261", e261("u8", "u16")), g.err(L, 4, "nope", "E0230", unknown("nope"))])

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

    # ---- 19c-3 (finding 5): once E0203 is reported there is no fix of any kind. 19 unknown names, then an E0261 whose fix its parent would
    # withdraw: the 20th error unwinds before the parent can run, and the fix attached to it must go too (auditor's cap1).
    L = ["manna v0;", "fn id8(x: u8) -> u8 {", "    return x;", "}", "fn f(a: u8, b: u16) -> u16 {"]
    L += ["    let e%d: u8 = zz%d;" % (i, i) for i in range(19)]
    L += ["    return (a + b) + id8(zz);", "}"]
    errs = [g.err(L, 6 + i, "zz%d" % i, "E0230", "unknown name 'zz%d'" % i) for i in range(19)]
    errs += [g.err(L, 25, "(a + b)", "E0261", e261("u8", "u16")), g.err(L, 25, "(a + b)", "E0203", "too many errors, stopping")]
    write("fix_e0261_none_offered_when_the_error_cap_stops_the_build", L, L, errs)
    # E0215 (a repeated attribute) has a fix of its own, attached in the declaration pass; when the cap is reached in that same pass (19 unknown
    # types after it) the fix is gone too. The declarations are checked first, so the E0215 comes before the 19 E0220.
    L = ["manna v0;", "[kernel_hook]", "[kernel_hook]", "fn h() {", "}"] + ["fn f%d(a: T%d) {\n}" % (i, i) for i in range(19)]
    L = "\n".join(L).split("\n")
    errs = [g.err(L, 3, "kernel_hook", "E0215", "duplicate attribute 'kernel_hook'")]
    for i in range(19):
        errs.append(g.err(L, 6 + 2 * i, "T%d" % i, "E0220", "unknown type 'T%d'" % i))
    errs[-1:] = [errs[-1], g.err(L, 6 + 2 * 18, "T18", "E0203", "too many errors, stopping")]
    write("fix_e0215_none_offered_when_the_error_cap_stops_the_build", L, L, errs)


    # ---- 19c-5: the wrap adds a level, so it is withdrawn when the fixed expression would be taller than the parser bound (MAX_EXPR_HEIGHT 512,
    # E0111). Before: the fix on a 511-operation chain gave a source that failed with E0111 (STATUS "Known issues"). The height that counts is the
    # longest path THROUGH the wrapped binary: a tall tree elsewhere in the same expression does not take the fix away.
    def chain(first, term, n):          # `first term term ...` with n operators
        return first + (" + " + term) * n
    # (a) the chain is the wrapped operand: 510 operations (height 511) gets its fix, 511 (height 512) cannot
    for n in (510, 511):
        body = "a" + " + a" * (n - 1) + " + b"
        L = ["manna v0;", "fn f(a: u8, b: u32) {", "    let r: u32 = " + body + ";", "}"]
        if n == 510:
            F = list(L); F[2] = "    let r: u32 = u32(" + "a" + " + a" * (n - 1) + ") + b;"
            write("fix_e0261_chain_of_510_operations_keeps_its_fix", L, F)
        else:
            write("fix_e0261_chain_of_511_operations_has_no_fix_the_wrap_would_pass_the_height_bound", L, L, [g.err(L, 3, body, "E0261", e261("u8", "u32"))])
    # (b) the wrap is at the BOTTOM of the chain: the new level is on the longest path just the same
    for n in (510, 511):
        body = chain("b", "c", n)
        L = ["manna v0;", "fn f(b: u8, c: u32) {", "    let r: u32 = " + body + ";", "}"]
        if n == 510:
            F = list(L); F[2] = "    let r: u32 = " + chain("u32(b)", "c", n) + ";"
            write("fix_e0261_at_the_bottom_of_a_510_operation_chain_keeps_its_fix", L, F)
        else:
            write("fix_e0261_at_the_bottom_of_a_511_operation_chain_has_no_fix", L, L, [g.err(L, 3, "b + c", "E0261", e261("u8", "u32"))])
    # (c) a 510-operation chain beside it makes the expression 512 high, but the wrap is on a short path: the fix stays
    tall = chain("c", "c", 510)
    L = ["manna v0;", "fn f(a: u8, b: u32, c: u32) {", "    let r: u32 = (a + b) + (" + tall + ");", "}"]
    F = list(L); F[2] = "    let r: u32 = (u32(a) + b) + (" + tall + ");"
    write("fix_e0261_on_a_short_path_beside_a_512_high_expression_keeps_its_fix", L, F)
    # (d) through a call argument, a field access, a construction and a named argument: four levels above the chain (measured: 507 operations is
    # the longest chain that parses there), and every one counts
    def deep(n, first):
        return ["manna v0;", "struct P {", "    x: u32,", "}", "fn g(v: u32) -> u32 {", "    return v;", "}", "fn f(b: u8, c: u32) -> u32 {",
                "    return g(P(x: " + chain(first, "c", n) + ").x);", "}"]
    L = deep(506, "b"); F = deep(506, "u32(b)")
    write("fix_e0261_under_a_call_a_field_and_a_named_argument_counts_every_level_keeps_its_fix", L, F)
    L = deep(507, "b")
    write("fix_e0261_under_a_call_a_field_and_a_named_argument_has_no_fix_when_those_levels_pass_the_bound", L, L, [g.err(L, 9, "b + c", "E0261", e261("u8", "u32"))])

    # ---- 19d (finding 6, as revised): the stale spellings are RENAMED with the declaration, never left behind and never a reason to withhold. Two
    # positions leave one: a function named in a type position (E0220 stops the lookup) and the field names of a construction E0240 stopped.
    # A rename is refused only for a real collision: r3 (`my_field:` would start to resolve to the renamed field once the E0240 is fixed) and r4
    # (`my_field:` names no field, E0242, and would resolve after the rename). The auditor's r1-r4, then controls.
    positional = "struct construction names every field: use 'field: value'"
    unknown_t = lambda n: "unknown type '%s'" % n
    L = ["manna v0;", "fn myFn() -> u8 {", "    return 1;", "}", "fn g() {", "    let q: myFn = 1;", "}"]
    F = rename(L, {"myFn": "my_fn"})
    write("fix_function_rename_reaches_its_name_written_in_a_type_position", L, F, [g.err(F, 6, "my_fn", "E0220", unknown_t("my_fn"))])
    L = ["manna v0;", "fn myFn() -> u8 {", "    return 1;", "}", "fn g() {", "    let q: myFn = 1;", "    let r: [2]myFn = 1;", "    let v = myFn();", "}"]
    F = rename(L, {"myFn": "my_fn"})
    write("fix_function_rename_reaches_every_type_position_it_is_written_in", L, F,
          [g.err(F, 6, "my_fn", "E0220", unknown_t("my_fn")), g.err(F, 7, "my_fn", "E0220", unknown_t("my_fn"))])
    L = ["manna v0;", "struct P {", "    myField: u8,", "    y: u8,", "}", "fn g() {", "    let p = P(1, myField: 2);", "}"]
    F = rename(L, {"myField": "my_field"})
    write("fix_field_rename_reaches_its_name_in_a_positional_construction", L, F, [g.err(F, 7, "1", "E0240", positional)])
    L = ["manna v0;", "struct P {", "    myField: u8,", "    otherField: u8,", "}", "fn g() {", "    let p = P(myField: 1, 2, otherField: 3);", "}"]
    F = rename(L, {"myField": "my_field", "otherField": "other_field"})
    write("fix_field_renames_reach_every_name_in_a_positional_construction", L, F, [g.err(F, 7, "2", "E0240", positional)])
    # r3 and r4: the real collisions stay refused
    L = ["manna v0;", "struct P {", "    myField: u8,", "    y: u8,", "}", "fn g() {", "    let p = P(1, my_field: 2);", "}"]
    write("fix_field_rename_refused_when_a_positional_construction_writes_its_target", L, L,
          [g.err(L, 3, "myField", "E0280", snake("myField")), g.err(L, 7, "1", "E0240", positional)])
    L = ["manna v0;", "struct P {", "    myField: u8,", "    y: u8,", "}", "fn g() {", "    let p = P(y: 1, my_field: 2);", "}"]
    write("fix_field_rename_refused_when_a_named_argument_writes_its_target", L, L,
          [g.err(L, 3, "myField", "E0280", snake("myField")), g.err(L, 7, "my_field", "E0242", "struct 'P' has no field 'my_field'")])
    L = ["manna v0;", "fn myFn() -> u8 {", "    return 1;", "}", "fn g() {", "    let q: my_fn = 1;", "}"]
    write("fix_function_rename_refused_when_a_type_position_writes_its_target", L, L,
          [g.err(L, 2, "myFn", "E0280", snake("myFn")), g.err(L, 6, "my_fn", "E0220", unknown_t("my_fn"))])
    # controls: the construction names another field / the type positions name something else
    L = ["manna v0;", "struct P {", "    myField: u8,", "    y: u8,", "}", "fn g(p: P) -> u8 {", "    let q = P(1, y: 2);", "    return p.myField;", "}"]
    F = rename(L, {"myField": "my_field"})
    write("fix_field_rename_offered_when_the_positional_construction_names_another_field", L, F, [g.err(F, 7, "1", "E0240", positional)])
    L = ["manna v0;", "fn myFn() -> u8 {", "    return 1;", "}", "struct S {", "    a: u8,", "}", "fn g() -> u8 {", "    let q: S = S(a: 1);", "    return myFn();", "}"]
    write("fix_function_rename_offered_when_the_type_positions_name_something_else", L, rename(L, {"myFn": "my_fn"}))
    # ---- 19e (findings 7, 8): the places that must NOT hand their own place to the expression inside them. The enclosing place is FREE (an
    # expression statement) or a type (a `return`), and the E0261 sits in the first element of an un-annotated array literal, a `len`
    # argument, a field base, an index, a conversion operand, or under `!`: no fix, because the wrap would change what those need. Each
    # case dies if synth_other (or the `!` case of synth_unary) hands its own place down - the auditor's mutants 2 and 3 - and would pass
    # a wrap that re-checks to E0223, E0255, E0251, E0260.
    L = ["manna v0;", "fn f(a: u8, b: u64, c: u8) {", "    [a + b, c];", "}"]
    write("fix_e0261_none_in_the_first_element_of_an_inferred_array_in_a_free_place", L, L, [g.err(L, 3, "a + b", "E0261", e261("u8", "u64"))])
    L = ["manna v0;", "fn f(a: u8, b: u16, c: u8) -> u16 {", "    return [a + b, c][0];", "}"]
    write("fix_e0261_none_in_the_first_element_of_an_inferred_array_in_a_typed_place", L, L, [g.err(L, 3, "a + b", "E0261", e261("u8", "u16"))])
    L = ["manna v0;", "fn f(a: u8, b: u64) -> u64 {", "    return len(a + b);", "}"]
    write("fix_e0261_none_in_a_len_argument", L, L, [g.err(L, 3, "a + b", "E0261", e261("u8", "u64"))])
    L = ["manna v0;", "fn f(a: u8, b: u32) -> u32 {", "    return (a + b).x;", "}"]
    write("fix_e0261_none_in_a_field_base", L, L, [g.err(L, 3, "(a + b)", "E0261", e261("u8", "u32"))])
    L = ["manna v0;", "fn f(xs: [4]u16, a: u8, b: u16) -> u16 {", "    return xs[a + b];", "}"]
    write("fix_e0261_none_in_an_index", L, L, [g.err(L, 3, "a + b", "E0261", e261("u8", "u16"))])
    L = ["manna v0;", "fn f(a: u8, b: u32) -> u32 {", "    return u32(a + b);", "}"]
    write("fix_e0261_none_in_a_conversion_operand", L, L, [g.err(L, 3, "a + b", "E0261", e261("u8", "u32"))])
    L = ["manna v0;", "fn f(a: u8, b: u32) {", "    !(a + b);", "}"]
    write("fix_e0261_none_under_logical_not_in_a_free_place", L, L, [g.err(L, 3, "(a + b)", "E0261", e261("u8", "u32"))])
    L = ["manna v0;", "fn f(a: u8, b: u32) -> bool {", "    return !(a + b);", "}"]
    write("fix_e0261_none_under_logical_not_in_a_typed_place", L, L, [g.err(L, 3, "(a + b)", "E0261", e261("u8", "u32"))])

    # ---- a MISSED fix, registered as C9 in docs/DEGRADATION.md (post-m2-ir-v0 fix-it pass): two un-annotated lets each have an E0261 whose fix
    # would be fine (both become u32 and `r + s` agrees), but a binary with two DIFFERENT waiting fixes withdraws both (19c-4), so neither is
    # offered. A no-fix case for now: the two E0261 stay, the source is unchanged.
    L = ["manna v0;", "fn f(a: u8, b: u32) -> u32 {", "    let r = a + b;", "    let s = a + b;", "    return r + s;", "}"]
    write("fix_e0261_two_lets_met_in_one_operator_have_no_fix_yet", L, L,
          [g.err(L, 3, "a + b", "E0261", e261("u8", "u32")), g.err(L, 4, "a + b", "E0261", e261("u8", "u32"))])

    # ---- 19f: a waiting fix whose new type is NOT an integer reaches an index (ir_expr.c accept_int_pending): `r < c` is bool, so the wrap that
    # makes `r` u32 is no use as an index and the E0261 has no fix. Since step 21d (D1) the bool index is reported (E0253) beside it, as a second mistake.
    L = ["manna v0;", "fn f(xs: [4]u8, a: u8, b: u32, c: u32) {", "    let r = a + b;", "    xs[r < c];", "}"]
    # step 21d (D1): the bool index is reported (E0253) beside the E0261, which still has no fix
    write("fix_e0261_none_when_the_local_reaches_an_index_through_a_comparison", L, L,
          [g.err(L, 3, "a + b", "E0261", e261("u8", "u32")), g.err(L, 4, "r < c", "E0253", "index/bound must be an integer, found bool")])
