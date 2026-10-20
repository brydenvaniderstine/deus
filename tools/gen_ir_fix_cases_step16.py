# Generates tests/ir_fix/* for typed-IR step 16: a rename in a function with more than 16 locals, so the per-body rename table grows twice
# (ir_rename.c body_set_rename: 8 -> 16 -> 32 slots) and the copy keeps the entries already there. The .fixed.mn is computed by renaming whole
# words, independent of the C builder. Run from the repo root with PYTHONPATH=tools.
import os
import gen_ir_fix_cases_step14 as f14

if __name__ == "__main__":
    names = ["a%d" % i for i in range(20)]
    bad = {0: "badZero", 9: "badNine", 17: "badSeventeen"}  # slots 0, 9 and 17: the table is allocated, grown past 8, then past 16
    for i, n in bad.items(): names[i] = n
    L = ["manna v0;", "fn f() -> u8 {"] + ["    let %s: u8 = %d;" % (n, i) for i, n in enumerate(names)] + ["    return %s + %s + %s;" % (names[0], names[9], names[17]), "}"]
    mapping = {"badZero": "bad_zero", "badNine": "bad_nine", "badSeventeen": "bad_seventeen"}
    f14.write("fix_rename_in_a_function_with_many_locals", L, f14.rename(L, mapping))
