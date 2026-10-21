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

    # Finding 2: the target of a non-assignable assignment is checked muted. Its calls still count toward recursion (E0235), a cycle through
    # one included; nothing else in it is reported (one mistake, one error).
    rec, notlv = "recursion is not allowed in v0", "can't assign to this expression"
    L = ["manna v0;", "fn f() {", "    f() = 1;", "}", "fn a() {", "    b() = 1;", "}", "fn b() {", "    a();", "}"]
    g.errors("assign_target_calls_count_toward_recursion", L, [e(L, 3, "f()", "E0270", notlv), e(L, 6, "b()", "E0270", notlv),
        e(L, 3, "f()", "E0235", rec), e(L, 6, "b()", "E0235", rec)])
    # An unknown function and a wrong argument count inside the target are not reported; the errors after the assignment are (the mute is lowered).
    L = ["manna v0;", "fn g(x: u8) {", "}", "fn f(x: u8) {", "    g(x) = 3;", "    nope(x) = 1;", "    g(x, x) = 3;", "    let y: u8 = nope;", "    let z: u8 = true;", "}"]
    g.errors("assign_target_is_silent_but_later_errors_still_print", L, [e(L, 5, "g(x)", "E0270", notlv), e(L, 6, "nope(x)", "E0270", notlv),
        e(L, 7, "g(x, x)", "E0270", notlv), e(L, 8, "nope", "E0230", "unknown name 'nope'"), e(L, 9, "true", "E0223", "type mismatch: expected u8, found bool")])

    # Finding 9: a size computation that really multiplies. The inner array is 65536 bytes, within the limit; the outer is 2^48 of them, 2^64
    # bytes, which wraps to 0 if the multiplication is done in u64 (size_computation_overflowing_u64 never gets that far: its inner array is
    # already too big). Reported at the whole type, in a struct field, a parameter, a return type and a `let`.
    ty = "[281474976710656][65536]u8"
    L = ["manna v0;", "struct W { a: %s, }" % ty, "fn f(p: %s) {" % ty, "}", "fn g() -> %s {" % ty, "}" ]
    g.errors("size_overflow_in_the_outer_multiplication", L, [e(L, 2, ty, "E0222", big), e(L, 3, ty, "E0222", big), e(L, 5, ty, "E0222", big)])
    L = ["manna v0;", "fn f() {", "    let x: %s = [1, 2];" % ty, "}"]
    g.errors("one_error_let_size_overflow_in_the_outer_multiplication", L, [e(L, 3, ty, "E0222", big)])

    # Finding 3: E0256 is for a bare `len` when no VISIBLE declaration named len exists. A local named len in a closed block is E0257 at its
    # declaration; the bare `len` after the block has nothing to resolve to, so it is E0256 as well (two mistakes, two errors).
    L = ["manna v0;", "fn f() {", "    if true {", "        let len: u8 = 1;", "    }", "    let n = len;", "}"]
    g.errors("len_bare_after_the_scope_of_a_local_named_len_closed", L, [e(L, 4, "len", "E0257", "'len' is a builtin and can't be declared"),
        e(L, 6, "len", "E0256", "len is a builtin function; call it as len(x)")])
