# Generates the typed-IR step 17 goldens (tests/ir/*), the fixes after the m2-ir-v0 audit (docs/history/m2-ir-v0-audit-brief.md, the reviewer's
# findings of 2026-10-21). Columns come from the source text, independent of the C builder; run from the repo root with PYTHONPATH=tools.
import gen_ir_goldens_step7 as g
import gen_ir_goldens_step12 as s12

e = s12.e
big = "type is too large (size exceeds the limit)"

if __name__ == "__main__":
    # Finding 1: an oversized `let` annotation is one error: the let is typed ERROR, so the initializer (which would build the same
    # oversized type, or a mismatch against it) and every later use stay silent.
    L = ["manna v0;", "fn id(a: u8) -> u8 {", "    return a;", "}", "fn f(v: u8) {",
         "    let a: [4294967296]u8 = [v; 4294967296];", "    let b: [4294967296]u8 = [v, v];", "    let c: [281474976710656][65536]u8 = [1, 2];",
         "    let d: [4294967296]u8 = v;", "    let h: [4294967296]u8 = id(v);", "    let k: [4294967296]u8 = a;", "    let m = a[0] + b[0];", "}"]
    g.errors("let_oversized_annotation_is_one_error", L, [e(L, 6, "[4294967296]u8", "E0222", big), e(L, 7, "[4294967296]u8", "E0222", big),
        e(L, 8, "[281474976710656][65536]u8", "E0222", big), e(L, 9, "[4294967296]u8", "E0222", big), e(L, 10, "[4294967296]u8", "E0222", big),
        e(L, 11, "[4294967296]u8", "E0222", big)])
    # The same with ONE let, so `make audit-ir-poison` has a golden whose whole output is the single diagnostic (it was two before the fix).
    L = ["manna v0;", "fn f(v: u8) {", "    let x: [4294967296]u8 = [v; 4294967296];", "}"]
    g.errors("one_error_let_oversized_annotation_with_a_literal", L, [e(L, 3, "[4294967296]u8", "E0222", big)])
