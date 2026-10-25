# Generates the typed-IR step 20f goldens (tests/ir/*): a comparison whose operands have different types (E0261) is still bool, so a context that
# rejects bool reports its own diagnostic next to the E0261 (Bryden, 2026-10-25: the E0261 used to hide it, and its fix made it appear). Columns come
# from the source text; run from the repo root with PYTHONPATH=tools.
import gen_ir_goldens_step7 as g
import gen_ir_goldens_step12 as s12

e = s12.e
e261 = lambda x, y: "operands have different types: %s and %s; convert one explicitly" % (x, y)
mism = lambda a, b: "type mismatch: expected %s, found %s" % (a, b)

if __name__ == "__main__":
    L = ["manna v0;", "fn f(a: u8, b: u16) -> u32 {", "    return a < b;", "}"]
    g.errors("e0261_comparison_in_a_return_that_rejects_bool", L,
             [e(L, 3, "a < b", "E0261", e261("u8", "u16")), e(L, 3, "a < b", "E0223", mism("u32", "bool"))])
    L = ["manna v0;", "fn f(a: u8, b: u16) {", "    let r: u8 = a == b;", "}"]
    g.errors("e0261_comparison_in_an_annotated_let_that_rejects_bool", L,
             [e(L, 3, "a == b", "E0261", e261("u8", "u16")), e(L, 3, "a == b", "E0223", mism("u8", "bool"))])
    L = ["manna v0;", "fn take(x: i64) {", "}", "fn f(a: u8, b: u16) {", "    take(a >= b);", "}"]
    g.errors("e0261_comparison_as_an_argument_that_rejects_bool", L,
             [e(L, 5, "a >= b", "E0261", e261("u8", "u16")), e(L, 5, "a >= b", "E0223", mism("i64", "bool"))])
    L = ["manna v0;", "fn f(a: u8, b: u16, c: u32) -> u32 {", "    return (a < b) + c;", "}"]
    g.errors("e0261_comparison_under_arithmetic", L,
             [e(L, 3, "(a < b)", "E0261", e261("u8", "u16")),
              e(L, 3, "(a < b) + c", "E0260", "operator '+' is not defined for bool")])
    # controls: a context that takes bool adds nothing
    L = ["manna v0;", "fn f(a: u8, b: u16) -> bool {", "    return a < b;", "}"]
    g.errors("e0261_comparison_in_a_return_that_takes_bool_is_alone", L, [e(L, 3, "a < b", "E0261", e261("u8", "u16"))])
    L = ["manna v0;", "fn f(a: u8, b: u16) {", "    if a < b {", "    }", "}"]
    g.errors("e0261_comparison_in_a_condition_is_alone", L, [e(L, 3, "a < b", "E0261", e261("u8", "u16"))])
