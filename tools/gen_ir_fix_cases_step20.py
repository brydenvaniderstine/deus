# Generates tests/ir_fix/* for step 20f: the E0261 fix of a comparison whose context rejects bool. The fix removes the E0261 only; the context's own
# diagnostic (E0223 / E0260), reported next to it since step 20f, remains. Run from the repo root with PYTHONPATH=tools.
import gen_ir_fix_cases_step14 as f14
import gen_ir_goldens_step7 as g

write = f14.write
mism = lambda a, b: "type mismatch: expected %s, found %s" % (a, b)

if __name__ == "__main__":
    L = ["manna v0;", "fn f(a: u8, b: u16) -> u32 {", "    return a < b;", "}"]
    F = list(L); F[2] = "    return u16(a) < b;"
    write("fix_e0261_comparison_fix_leaves_the_contexts_own_error", L, F, [g.err(F, 3, "u16(a) < b", "E0223", mism("u32", "bool"))])
    L = ["manna v0;", "fn f(a: u8, b: u16, c: u32) -> u32 {", "    return (a < b) + c;", "}"]
    F = list(L); F[2] = "    return (u16(a) < b) + c;"
    write("fix_e0261_comparison_under_arithmetic_leaves_the_operator_error", L, F, [g.err(F, 3, "(u16(a) < b) + c", "E0260", "operator '+' is not defined for bool")])

    # coverage (step 20i): the set of names that resolved to nothing grows past its first table (8 names -> rebuilt at 16) while a struct rename exists: the rename is
    # still offered, and no unknown name is touched.
    names = ["q%02d" % i for i in range(1, 13)]
    L = ["manna v0;", "struct my_point {", "    x: u8,", "}", "fn f() -> u8 {", "    return " + " + ".join(names) + ";", "}"]
    F = list(L); F[1] = "struct MyPoint {"
    write("fix_struct_rename_is_offered_next_to_twelve_distinct_unknown_names", L, F, [g.err(F, 6, n, "E0230", "unknown name '%s'" % n) for n in names])
