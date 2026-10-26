# Generates tests/ir_fix/* for step 21d (decision D1): a waiting E0261 fix (an un-annotated local whose initializer is the mismatch) that reaches a comparison,
# `&&` or `!` keeps its fix now that those keep their bool type over a poisoned operand; the other mistake in the expression is reported and remains. Run from the
# repo root with PYTHONPATH=tools.
import gen_ir_fix_cases_step14 as f14
import gen_ir_goldens_step7 as g

write = f14.write

def mistakes(name, n):
    """D5: the case holds n independent mistakes (the E0261 and the context's own); the audit counts an input whose diagnostics equal this"""
    open("tests/ir_fix/%s.mistakes" % name, "w").write("%d\n" % n)


if __name__ == "__main__":
    L = ["manna v0;", "fn f(a: u8, b: u32) -> bool {", "    let r = a + b;", "    return r < 5;", "}"]
    F = list(L); F[2] = "    let r = u32(a) + b;"
    write("fix_e0261_waiting_local_in_a_comparison_that_returns_bool_keeps_its_fix", L, F)
    L = ["manna v0;", "fn f(a: u8, b: u32, c: bool) -> bool {", "    let r = a + b;", "    return (r < 5) && c;", "}"]
    F = list(L); F[2] = "    let r = u32(a) + b;"
    write("fix_e0261_waiting_local_under_and_keeps_its_fix", L, F)
    L = ["manna v0;", "fn f(a: u8, b: u32) -> bool {", "    let r = a + b;", "    return !(r < 5) && nope;", "}"]
    F = list(L); F[2] = "    let r = u32(a) + b;"
    write("fix_e0261_waiting_local_beside_an_unknown_name_keeps_its_fix", L, F, [g.err(F, 4, "nope", "E0230", "unknown name 'nope'")])
    mistakes("fix_e0261_waiting_local_beside_an_unknown_name_keeps_its_fix", 2)
    # step 20f / 21d cases written by earlier generators: their sources hold the E0261 AND a second mistake the context reports (D1)
    mistakes("fix_e0261_in_an_unannotated_let_has_no_fix_a_comparison_of_it_is_returned_as_another_type", 2)
    mistakes("fix_e0261_in_an_unannotated_let_keeps_its_fix_arithmetic_on_the_bool_that_a_comparison_of_it_gives", 2)
    mistakes("fix_e0261_none_when_the_local_reaches_an_index_through_a_comparison", 2)
    # two E0261 in one operator: the two mistakes are the two diagnostics (D5)
    mistakes("fix_e0261_two_waiting_fixes_meeting_in_one_operator_are_both_withdrawn", 2)
    mistakes("fix_e0261_two_lets_met_in_one_operator_have_no_fix_yet", 2)
