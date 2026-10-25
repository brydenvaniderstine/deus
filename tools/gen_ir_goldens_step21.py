# Generates the typed-IR step 21d goldens (tests/ir/poison_*): decision D1 (Bryden, 2026-10-25). An expression whose type does not depend on its operands keeps
# that type when an operand is poisoned (here: an unknown name, E0230): a comparison, `&&`, `||` and `!` are bool, so the context reports its OWN mistake next
# to the E0230 instead of showing it only after the first mistake is fixed, and the operand that is not poisoned is still checked for its own mistake. Arithmetic,
# bitwise, shift and unary `-` / `~` take the operand's type and stay silent (controls). The intended diagnostics are written here from the mistakes, never read
# from the builder; columns come from the source text. Run from the repo root with PYTHONPATH=tools.
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
