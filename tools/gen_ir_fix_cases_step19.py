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
