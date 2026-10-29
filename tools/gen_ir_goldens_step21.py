# Generates the typed-IR step 21d goldens (tests/ir/poison_*): decision D1 (Bryden, 2026-10-25). An expression whose type does not depend on its operands keeps
# that type when an operand is poisoned (here: an unknown name, E0230): a comparison, `&&`, `||` and `!` are bool, so the context reports its OWN mistake next
# to the E0230 instead of showing it only after the first mistake is fixed, and the operand that is not poisoned is still checked for its own mistake. Arithmetic,
# bitwise, shift and unary `-` / `~` take the operand's type and stay silent (controls). The intended diagnostics are written here from the mistakes, never read
# from the builder; columns come from the source text. Run from the repo root with PYTHONPATH=tools.
import os
import gen_ir_goldens_step7 as g
import gen_ir_goldens_step12 as s12

e = s12.e
unk = lambda n: "unknown name '%s'" % n
mism = lambda a, b: "type mismatch: expected %s, found %s" % (a, b)
notdef = lambda op, t: "operator '%s' is not defined for %s" % (op, t)


def golden(name, L, errs, mistakes=None):
    """the golden, and (D5) `NAME.mistakes` when it holds more than one independent mistake: the count comes from here, the generator, never from the builder"""
    g.errors(name, L, errs)
    n = mistakes or len(errs)
    if n > 1:
        open("tests/ir/%s.mistakes" % name, "w").write("%d\n" % n)
    elif os.path.exists("tests/ir/%s.mistakes" % name):  # a golden that went from several mistakes to one (step 22d) must not keep its old count
        os.remove("tests/ir/%s.mistakes" % name)

if __name__ == "__main__":
    # every comparison operator, the poison on either side, in a return that rejects bool: two mistakes each
    for name, op in (("eq", "=="), ("ne", "!="), ("lt", "<"), ("le", "<="), ("gt", ">"), ("ge", ">=")):
        for side, text in (("right", "a %s nope" % op), ("left", "nope %s a" % op)):
            L = ["manna v0;", "fn f(a: u8) -> u32 {", "    return %s;" % text, "}"]
            golden("poison_comparison_%s_%s_operand_in_a_return_that_rejects_bool" % (name, side), L,
                     [e(L, 3, "nope", "E0230", unk("nope")), e(L, 3, text, "E0223", mism("u32", "bool"))])
    # the other contexts that reject bool
    L = ["manna v0;", "fn f() {", "    let x: u32 = nope < 1;", "}"]
    golden("poison_comparison_in_an_annotated_let_that_rejects_bool", L,
             [e(L, 3, "nope", "E0230", unk("nope")), e(L, 3, "nope < 1", "E0223", mism("u32", "bool"))])
    L = ["manna v0;", "fn take(x: i64) {", "}", "fn f() {", "    take(nope == 1);", "}"]
    golden("poison_comparison_as_an_argument_that_rejects_bool", L,
             [e(L, 5, "nope", "E0230", unk("nope")), e(L, 5, "nope == 1", "E0223", mism("i64", "bool"))])
    L = ["manna v0;", "fn f() -> u32 {", "    return (nope < 1) + 2;", "}"]
    golden("poison_comparison_under_arithmetic", L,
             [e(L, 3, "nope", "E0230", unk("nope")), e(L, 3, "(nope < 1) + 2", "E0260", notdef("+", "bool"))])
    L = ["manna v0;", "fn f() {", "    let ok = nope < 1;", "    let q = ok + ok;", "}"]
    golden("poison_comparison_kept_in_an_unannotated_let_is_bool_later", L,
             [e(L, 3, "nope", "E0230", unk("nope")), e(L, 4, "ok + ok", "E0260", notdef("+", "bool"))])
    L = ["manna v0;", "fn f(xs: [4]u8) {", "    xs[nope < 1];", "}"]
    golden("poison_comparison_as_an_index", L,
             [e(L, 3, "nope", "E0230", unk("nope")), e(L, 3, "nope < 1", "E0253", "index/bound must be an integer, found bool")])
    # controls: a context that takes bool adds nothing; a literal sibling has nothing to check; two poisoned operands are two mistakes
    L = ["manna v0;", "fn f(a: u8) -> bool {", "    return a < nope;", "}"]
    golden("poison_comparison_in_a_return_that_takes_bool_is_alone", L, [e(L, 3, "nope", "E0230", unk("nope"))])
    L = ["manna v0;", "fn f(a: u8) {", "    if a < nope {", "    }", "}"]
    golden("poison_comparison_in_a_condition_is_alone", L, [e(L, 3, "nope", "E0230", unk("nope"))])
    L = ["manna v0;", "fn f() -> bool {", "    return nope < 1;", "}"]
    golden("poison_comparison_with_a_literal_is_alone", L, [e(L, 3, "nope", "E0230", unk("nope"))])
    L = ["manna v0;", "fn f() -> bool {", "    return nope < nada;", "}"]
    golden("poison_comparison_of_two_unknown_names_is_two_mistakes", L,
             [e(L, 3, "nope", "E0230", unk("nope")), e(L, 3, "nada", "E0230", unk("nada"))])
    # the typed operand is still checked: `<` is not defined for bool whatever the other side is
    L = ["manna v0;", "fn f(b: bool) -> bool {", "    return nope < b;", "}"]
    golden("poison_comparison_other_operand_type_is_still_checked", L,
             [e(L, 3, "nope", "E0230", unk("nope")), e(L, 3, "nope < b", "E0260", notdef("<", "bool"))])
    L = ["manna v0;", "fn f(b: bool) -> bool {", "    return nope == b;", "}"]
    golden("poison_equality_of_a_bool_is_defined_and_alone", L, [e(L, 3, "nope", "E0230", unk("nope"))])
    # && and ||
    L = ["manna v0;", "fn f() -> bool {", "    return nope && 1;", "}"]
    golden("poison_logical_other_operand_literal_is_a_mistake", L,
             [e(L, 3, "nope", "E0230", unk("nope")), e(L, 3, "1", "E0223", mism("bool", "integer literal"))])
    L = ["manna v0;", "fn f(x: u8) -> bool {", "    return nope || x;", "}"]
    golden("poison_logical_other_operand_not_bool_is_a_mistake", L,
             [e(L, 3, "nope", "E0230", unk("nope")), e(L, 3, "nope || x", "E0260", notdef("||", "u8"))])
    L = ["manna v0;", "fn f(b: bool) -> bool {", "    return nope && b;", "}"]
    golden("poison_logical_with_a_bool_in_a_bool_return_is_alone", L, [e(L, 3, "nope", "E0230", unk("nope"))])
    L = ["manna v0;", "fn f(b: bool) -> u32 {", "    return b || nope;", "}"]
    golden("poison_logical_in_a_return_that_rejects_bool", L,
             [e(L, 3, "nope", "E0230", unk("nope")), e(L, 3, "b || nope", "E0223", mism("u32", "bool"))])
    L = ["manna v0;", "fn f() -> bool {", "    return nope && nada;", "}"]
    golden("poison_logical_of_two_unknown_names_is_two_mistakes", L,
             [e(L, 3, "nope", "E0230", unk("nope")), e(L, 3, "nada", "E0230", unk("nada"))])
    # !
    L = ["manna v0;", "fn f() -> u32 {", "    return !nope;", "}"]
    golden("poison_not_in_a_return_that_rejects_bool", L,
             [e(L, 3, "nope", "E0230", unk("nope")), e(L, 3, "!nope", "E0223", mism("u32", "bool"))])
    L = ["manna v0;", "fn f() {", "    if !nope {", "    }", "}"]
    golden("poison_not_in_a_condition_is_alone", L, [e(L, 3, "nope", "E0230", unk("nope"))])
    # the boundary: the result type IS the operand's, so nothing else can be said
    L = ["manna v0;", "fn f(a: u8) -> bool {", "    return a + nope;", "}"]
    golden("poison_arithmetic_stays_silent_in_a_return_that_rejects_its_type", L, [e(L, 3, "nope", "E0230", unk("nope"))])
    L = ["manna v0;", "fn f() -> bool {", "    return -nope;", "}"]
    golden("poison_negation_stays_silent", L, [e(L, 3, "nope", "E0230", unk("nope"))])
    L = ["manna v0;", "fn f() -> bool {", "    return ~nope;", "}"]
    golden("poison_complement_stays_silent", L, [e(L, 3, "nope", "E0230", unk("nope"))])

    # ---- step C2c: an unsized field no longer hides the other fields' own size mistakes (layout_struct), and the dependents of an unsized struct stay silent ----
    cyc = "struct 'A' contains itself"
    big = "type is too large (size exceeds the limit)"
    CYCLE = ["manna v0;", "struct A { b: B, }", "struct B { a: A, }"]
    L = CYCLE + ["struct D { c: A, big: [4294967296]u8, }"]
    golden("layout_unsized_field_does_not_hide_a_later_fields_own_size_error", L,
           [e(L, 2, "A", "E0221", cyc), e(L, 4, "[4294967296]u8", "E0222", big)])
    L = CYCLE + ["struct D { big: [4294967296]u8, c: A, }"]
    golden("layout_too_large_field_before_an_unsized_one_is_reported_once", L,
           [e(L, 2, "A", "E0221", cyc), e(L, 4, "[4294967296]u8", "E0222", big)])
    L = CYCLE + ["struct D { c: A, d: A, }"]
    golden("layout_two_unsized_fields_are_one_silent_struct", L, [e(L, 2, "A", "E0221", cyc)])
    # the cascade the `!dep->sized` guard prevents: D holds 2^31-1 bytes beside an unsized field, so a size that were computed for it (the unset size of A read as 0) would make [2]D too
    # large; D has no layout, so E says nothing. One mistake, one diagnostic.
    L = CYCLE + ["struct D { c: A, x: [2147483647]u8, }", "struct E { d: [2]D, }"]
    golden("layout_dependents_of_an_unsized_struct_stay_silent", L, [e(L, 2, "A", "E0221", cyc)])
    # the offsets after an unsized field are NOT known (step 22d, Bryden 2026-10-29: a total overflow is reported only up to the first field whose size is unknown; at step C2c
    # this was reported as a lower bound): the sum that exceeds the limit only after the unsized field is not claimed. One mistake: the cycle.
    L = CYCLE + ["struct D { c: A, x: [2147483647]u8, y: u8, }"]
    golden("layout_known_fields_beside_an_unsized_one_that_exceed_the_limit_are_reported", L, [e(L, 2, "A", "E0221", cyc)])
    # a struct holding an unsized one has no size, so a struct holding THAT stays silent too (one mistake): were D given a size from its other field, F would be too large
    L = CYCLE + ["struct D { c: A, y: u8, }", "struct F { d: D, z: [2147483647]u8, }"]
    golden("layout_a_struct_holding_an_unsized_one_gives_its_dependents_no_size", L, [e(L, 2, "A", "E0221", cyc)])
