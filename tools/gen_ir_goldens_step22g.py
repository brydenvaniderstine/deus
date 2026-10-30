# Generates the typed-IR step 22g goldens (tests/ir/poison_beside_*): round-4 finding 5, decided by Bryden (2026-10-30): REPORT BOTH. D1 (step 21d) says the part of an expression
# that is not poisoned is still checked for its own mistake; conversions, indexes and arithmetic skipped it. Rule: a mistake that is certain WHATEVER the poisoned part turns out to
# be is reported beside it; one that depends on the poisoned part's type is not (no cascade). Certain: a conversion to `bool` (no operand type converts to it: E0265, worded without
# the operand's type); an index or slice bound whose own type can never be an index (bool, a struct, an array: E0253), also beside a base that is not poisoned but not indexable
# (`x[true]` with `x: u8` is E0252 AND E0253); an operator that the non-poisoned operand's own type can never take (`nope + b` with a bool `b`: E0260), for arithmetic, bitwise and
# shift operators as D1 already did for comparisons. Not claimed: a conversion to an integer type, an integer index, `nope + x` with `x: u8` (a mismatch depends on the unknown type).
# The intended diagnostics are written here from the mistakes, never read from the builder; columns come from the source text. Run from the repo root with PYTHONPATH=tools.
import gen_ir_goldens_step7 as g
import gen_ir_goldens_step12 as s12
import gen_ir_goldens_step21 as s21

e = s12.e
unk = lambda n: "unknown name '%s'" % n
notint = lambda t: "index/bound must be an integer, found %s" % t
noidx = lambda t: "can't index/slice type %s" % t
noop = lambda op, t: "operator '%s' is not defined for %s" % (op, t)
noconv = "can't convert to bool; conversions are between integer types only"
golden = s21.golden

HEAD = ["manna v0;", "struct P { a: u8, }", "fn f(b: bool, x: u8, xs: [4]u8, p: P) -> bool {"]
TAIL = ["    return true;", "}"]


def prog(*stmts):
    return HEAD + ["    " + s for s in stmts] + TAIL


if __name__ == "__main__":
    # the auditor's input (finding 5), as a function of its own
    L = ["manna v0;", "fn f(b: bool, x: u8, xs: [4]u8) -> bool {", "    let c = nope < b;", "    let d = bool(nope2);", "    let e = nope3[b];", "    let g = xs[b];", "    return true;", "}"]
    golden("poison_beside_a_conversion_to_bool_and_an_index_of_the_wrong_type", L,
           [e(L, 3, "nope", "E0230", unk("nope")), e(L, 3, "nope < b", "E0260", noop("<", "bool")), e(L, 4, "nope2", "E0230", unk("nope2")), e(L, 4, "bool(nope2)", "E0265", noconv),
            e(L, 5, "nope3", "E0230", unk("nope3")), e(L, 5, "b]", "E0253", notint("bool")), e(L, 6, "b]", "E0253", notint("bool"))])
    # a base that is not poisoned but not indexable: the index is checked for its own mistake too
    L = prog("let r = x[true];")
    golden("poison_beside_an_index_of_a_base_that_is_not_indexable", L, [e(L, 4, "x", "E0252", noidx("u8")), e(L, 4, "true", "E0253", notint("bool"))])
    L = prog("let r = x[true..1];", "let s = x[0..p];")
    golden("poison_beside_slice_bounds_of_a_base_that_is_not_indexable", L,
           [e(L, 4, "x", "E0252", noidx("u8")), e(L, 4, "true", "E0253", notint("bool")), e(L, 5, "x", "E0252", noidx("u8")), e(L, 5, "p]", "E0253", notint("P"))])
    # a poisoned base: a bound whose own type can never be an index is reported, either bound, any such type
    L = prog("let s1 = nope1[b..1];", "let s2 = nope2[1..b];", "let s3 = nope3[p..xs];")
    golden("poison_beside_slice_bounds_that_can_never_be_an_index", L,
           [e(L, 4, "nope1", "E0230", unk("nope1")), e(L, 4, "b..", "E0253", notint("bool")), e(L, 5, "nope2", "E0230", unk("nope2")), e(L, 5, "b]", "E0253", notint("bool")),
            e(L, 6, "nope3", "E0230", unk("nope3")), e(L, 6, "p..", "E0253", notint("P")), e(L, 6, "xs]", "E0253", notint("[4]u8"))])
    L = prog("let i1 = nope1[p];", "let i2 = nope2[xs];", "let i3 = nope3.zz[b];")
    golden("poison_beside_an_index_of_a_struct_an_array_and_a_field_of_a_poisoned_base", L,
           [e(L, 4, "nope1", "E0230", unk("nope1")), e(L, 4, "p]", "E0253", notint("P")), e(L, 5, "nope2", "E0230", unk("nope2")), e(L, 5, "xs]", "E0253", notint("[4]u8")),
            e(L, 6, "nope3", "E0230", unk("nope3")), e(L, 6, "b]", "E0253", notint("bool"))])
    # the conversion: only a target no operand type can reach (bool) is a certain mistake; the operand's own mistakes still come first
    L = prog("let c1 = bool(nope1);", "let c2 = bool(nope2 + 1);")
    golden("poison_beside_a_conversion_to_bool", L,
           [e(L, 4, "nope1", "E0230", unk("nope1")), e(L, 4, "bool(nope1)", "E0265", noconv), e(L, 5, "nope2", "E0230", unk("nope2")), e(L, 5, "bool(nope2 + 1)", "E0265", noconv)])
    # arithmetic, bitwise and shift operators: the non-poisoned operand's own type can never take the operator (E0260), either side
    L = prog("let a1 = nope1 + b;", "let a2 = b - nope2;", "let a3 = nope3 & b;", "let a4 = nope4 << b;", "let a5 = p * nope5;", "let a6 = nope6 | xs;")
    golden("poison_beside_an_arithmetic_operand_the_operator_can_never_take", L,
           [e(L, 4, "nope1", "E0230", unk("nope1")), e(L, 4, "nope1 + b", "E0260", noop("+", "bool")),
            e(L, 5, "nope2", "E0230", unk("nope2")), e(L, 5, "b - nope2", "E0260", noop("-", "bool")),
            e(L, 6, "nope3", "E0230", unk("nope3")), e(L, 6, "nope3 & b", "E0260", noop("&", "bool")),
            e(L, 7, "nope4", "E0230", unk("nope4")), e(L, 7, "nope4 << b", "E0260", noop("<<", "bool")),
            e(L, 8, "nope5", "E0230", unk("nope5")), e(L, 8, "p * nope5", "E0260", noop("*", "P")),
            e(L, 9, "nope6", "E0230", unk("nope6")), e(L, 9, "nope6 | xs", "E0260", noop("|", "[4]u8"))])
    # controls: what depends on the unknown type is not claimed (one mistake each: the unknown name)
    for i, name in ((1, "a_conversion_to_an_integer_type"), (2, "an_integer_index"), (3, "a_literal_index"), (4, "integer_slice_bounds"), (5, "arithmetic_with_a_typed_integer"), (7, "a_shift_by_a_typed_integer"), (8, "unary_minus")):
        stmt = {1: "let t1 = u8(nope1);", 2: "let t2 = nope2[x];", 3: "let t3 = nope3[1];", 4: "let t4 = nope4[0..x];", 5: "let t5 = nope5 + x;", 7: "let t7 = nope7 << x;", 8: "let t8 = -nope8;"}[i]
        L = prog(stmt)
        golden("poison_control_%s_gives_only_the_unknown_name" % name, L, [e(L, 4, "nope%d" % i, "E0230", unk("nope%d" % i))])
    # the same function's other branches, each pinned by a golden: a unit value is not blamed beside a poisoned base (step 10's rule stands), and a poisoned index beside a poisoned
    # base is two independent unknown names and nothing else
    L = prog("let t9 = nope9[u()];") + ["fn u() {", "}"]
    golden("poison_control_a_unit_index_is_not_blamed_beside_a_poisoned_base", L, [e(L, 4, "nope9", "E0230", unk("nope9"))])
    L = prog("let t10 = nope10[nope11];")
    golden("poison_beside_a_poisoned_index_of_a_poisoned_base_is_two_unknown_names", L, [e(L, 4, "nope10", "E0230", unk("nope10")), e(L, 4, "nope11", "E0230", unk("nope11"))])
