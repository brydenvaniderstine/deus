# Generates the typed-IR step 18 goldens (tests/ir/*): the re-audit's inputs for the areas it found sound (the mute across loops, branches and
# the error cap; the oversized `let` annotation used everywhere), pinned so they stay sound. Columns come from the source text, independent
# of the C builder; run from the repo root with PYTHONPATH=tools. The sources are the auditor's, line for line (reaudit-repros/inputs).
import gen_ir_goldens_step7 as g
import gen_ir_goldens_step12 as s12

e = s12.e
notlv = "can't assign to this expression"
mism = "type mismatch: expected u8, found bool"

if __name__ == "__main__":
    # A bad assignment target inside a loop and both branches of an `if`: the 20-error cap (E0203) is reached on the SECOND target. Twelve real errors come first, then the
    # first target (since C11, Bryden 2026-10-27, a target is checked with diagnostics on, so its own mistakes count): the E0270 and six mistakes of its own - the arity of
    # `g` (4 arguments for 1), the unknown names `nope1` and `x`, `+` on a bool, the unknown function `P`, and the integer literals of `[1, 2]` that have no type - so 19
    # errors, and the E0270 of the second target is the 20th. The cap unwinds cleanly there. (Before C11 the filler was 18 `let`s and the first target was silent; the six
    # last filler `let`s went because the first target now supplies six real errors.)
    L = ["manna v0;", "fn g(x: u8) -> u8 {", "    return x;", "}", "fn f(c: bool) {"]
    L += ["    let a%d: u8 = true;" % i for i in range(12)]
    L += ["    while c {", "        if c {", "            g(nope1, true + 1, P(q: 1).z, [1, 2][7..x]) = g(1);", "        } else {", "            (g(nope2)) = 4;",
          "        }", "    }", "    let z1: u8 = true;", "    let z2: u8 = true;", "    let z3: u8 = true;", "}"]
    t1 = "g(nope1, true + 1, P(q: 1).z, [1, 2][7..x])"
    errs = [e(L, 6 + i, "true", "E0223", mism) for i in range(12)]
    errs += [e(L, 20, t1, "E0270", notlv), e(L, 20, t1, "E0233", "'g' takes 1 argument, found 4"), e(L, 20, "nope1", "E0230", "unknown name 'nope1'"),
             e(L, 20, "true + 1", "E0260", "operator '+' is not defined for bool"), e(L, 20, "P(q", "E0232", "unknown function 'P'"),
             e(L, 20, "1, 2", "E0225", "cannot infer the type of this integer literal; annotate it"), e(L, 20, "x", "E0230", "unknown name 'x'"),
             e(L, 22, "(g(nope2))", "E0270", notlv), e(L, 22, "(g(nope2))", "E0203", "too many errors, stopping")]
    g.errors("assign_target_muted_in_loops_and_branches_with_the_error_cap_reached", L, errs)

    # The same kind of targets where the cap is NOT reached: the value on the right of the assignment, a later statement and a later `let` still report, and so does
    # every mistake inside a target (C11): `nope` in the for step, the `true` argument of `g(true)`, the arity of `g(1, 2)` and the field of its u8 result.
    L = ["manna v0;", "fn g(x: u8) -> u8 {", "    return x;", "}", "fn f(c: bool) -> u8 {", "    for let i: u8 = 0; i < 3; g(i + nope) = 1 {",
         "        if c {", "            g(true) = nope2;", "        } else {", "            (g(1, 2)).x = h();", "        }", "    }",
         "    let y: u8 = true;", "    return y;", "}"]
    g.errors("assign_target_muted_in_a_for_step_and_both_branches_later_errors_print", L, [
        e(L, 6, "g(i + nope)", "E0270", notlv), e(L, 6, "nope", "E0230", "unknown name 'nope'"), e(L, 8, "g(true)", "E0270", notlv), e(L, 8, "true", "E0223", mism),
        e(L, 8, "nope2", "E0230", "unknown name 'nope2'"),
        e(L, 10, "(g(1, 2)).x", "E0270", notlv), e(L, 10, "(g(1, 2))", "E0233", "'g' takes 1 argument, found 2"), e(L, 10, "(g(1, 2))", "E0251", "type u8 has no fields"),
        e(L, 10, "h", "E0232", "unknown function 'h'"), e(L, 13, "true", "E0223", mism)])

    # An oversized `let` annotation in every position that uses the variable: assignment, element assignment, argument, length, slice,
    # construction field, conversion, unary, field access, condition, return, comparison. One E0222 per annotation; the variable's type is
    # ERROR, so none of its uses reports. The one other error is real and independent of `big`: `take(..)` is u8 and `len(..)` is u64.
    big = "type is too large (size exceeds the limit)"
    L = ["manna v0;", "struct P {", "    a: u8,", "}", "fn take(x: [3]u8) -> u8 {", "    return x[0];", "}",
         "fn f(v: u8) -> u8 {",
         "    let big: [4294967296]u8 = [v; 4294967296];", "    let s: [][4294967296]u8 = big;",
         "    for let i: [4294967296]u8 = big; i < 3; i = i + 1 {", "        big = [1, 2];", "        big[0] = true;",
         "        let q = take(big) + len(big) + big[0..2][0];", "        let p = P(a: big);",
         "        let w: u8 = u8(big) + -big + big.x;", "        if big {", "            return big;", "        }", "    }", "    return big == big;", "}"]
    g.errors("oversized_let_annotation_used_in_every_position_stays_silent", L, [
        e(L, 9, "[4294967296]u8", "E0222", big), e(L, 10, "[4294967296]u8", "E0222", big),  # the array whose size overflows, inside the slice type
        e(L, 11, "[4294967296]u8", "E0222", big),
        e(L, 14, "take(big) + len(big)", "E0261", "operands have different types: u8 and u64; convert one explicitly"),
        # step 21d (D1): `big == big` is bool whatever its poisoned operands are, so the `-> u8` that returns it reports its own mistake
        e(L, L.index("    return big == big;") + 1, "big == big", "E0223", "type mismatch: expected u8, found bool")])
