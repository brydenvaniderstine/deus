# Generates the typed-IR step 13 goldens (tests/ir/convert_*: conversions `T(x)`, E0265, the existing E0225/E0226 for operands that have no type) from a spec whose columns come from the source text, independent of the C builder; run from the repo root.
import gen_ir_goldens_step7 as g
import gen_ir_goldens_step10 as c
import gen_ir_goldens_step11 as s11
import gen_ir_goldens_step12 as s12

plain, ret, call, cl, nm = c.plain, c.ret, c.call, c.cl, c.nm
let, assign, sdecl, snew = s11.let, s11.assign, s11.sdecl, s11.snew
arr, rep, idx, e = s12.arr, s12.rep, s12.idx, s12.e
msg_unit, infer, mismatch, unk = c.msg_unit, s12.infer, s12.mismatch, s12.unk
INTS = ["u8", "i8", "u16", "i16", "u32", "i32", "u64", "i64"]
only = "conversions are between integer types only"

def cv(L, ln, text, t, operand, occ=0):
    return g.N("convert", g.sp(L, ln, text, occ), t, operand)

def cannot(a, b, hint=False):
    return "can't convert %s to %s; %s%s" % (a, b, only, " (write 'x != 0')" if hint else "")

if __name__ == "__main__":
    # ---------- every integer pair (including the same-type no-op, kept) ----------
    for src in INTS:
        L = ["manna v0;", "fn f(x: %s) {" % src] + ["    let a%d = %s(x);" % (i, t) for i, t in enumerate(INTS)] + ["}"]
        lets = [let(L, 3 + i, "let a%d = %s(x);" % (i, t), "a%d" % i, i + 1, t, cv(L, 3 + i, "%s(x)" % t, t, cl(L, 3 + i, "x", 0, src))) for i, t in enumerate(INTS)]
        g.put("convert_from_" + src, L, plain(L, 2, len(L), "f", [("x: " + src, "x", src)], "", *lets))

    # ---------- nested, in expressions, calls, indexes, arrays, fields ----------
    L = ["manna v0;", "fn take(a: u32) -> u32 {", "    return a;", "}", "fn f(x: u8, y: u16, i: u32) -> u64 {", "    let a = u32(u16(u8(x)));", "    let b = u8(u16(u32(i)));",
         "    let c = u16(x) + y;", "    let d = take(u32(x));", "    let e = u32(y) == i;", "    let h = u64(x) + u64(i);", "    return h;", "}"]
    g.put("convert_nested_and_in_expressions", L,
          plain(L, 2, 4, "take", [("a: u32", "a", "u32")], "u32", ret(L, 3, "return a;", cl(L, 3, "a", 0, "u32"))) +
          plain(L, 5, 13, "f", [("x: u8", "x", "u8"), ("y: u16", "y", "u16"), ("i: u32", "i", "u32")], "u64",
                let(L, 6, "let a = u32(u16(u8(x)));", "a", 3, "u32", cv(L, 6, "u32(u16(u8(x)))", "u32", cv(L, 6, "u16(u8(x))", "u16", cv(L, 6, "u8(x)", "u8", cl(L, 6, "x", 0, "u8"))))),
                let(L, 7, "let b = u8(u16(u32(i)));", "b", 4, "u8", cv(L, 7, "u8(u16(u32(i)))", "u8", cv(L, 7, "u16(u32(i))", "u16", cv(L, 7, "u32(i)", "u32", cl(L, 7, "i", 2, "u32"))))),
                let(L, 8, "let c = u16(x) + y;", "c", 5, "u16", g.N("binary", g.sp(L, 8, "u16(x) + y"), "+ u16", cv(L, 8, "u16(x)", "u16", cl(L, 8, "x", 0, "u8")), cl(L, 8, "y", 1, "u16"))),
                let(L, 9, "let d = take(u32(x));", "d", 6, "u32", call(L, 9, "take(u32(x))", "u32", "take", cv(L, 9, "u32(x)", "u32", cl(L, 9, "x", 0, "u8")))),
                let(L, 10, "let e = u32(y) == i;", "e", 7, "bool", g.N("binary", g.sp(L, 10, "u32(y) == i"), "== bool", cv(L, 10, "u32(y)", "u32", cl(L, 10, "y", 1, "u16")), cl(L, 10, "i", 2, "u32"))),
                let(L, 11, "let h = u64(x) + u64(i);", "h", 8, "u64", g.N("binary", g.sp(L, 11, "u64(x) + u64(i)"), "+ u64", cv(L, 11, "u64(x)", "u64", cl(L, 11, "x", 0, "u8")), cv(L, 11, "u64(i)", "u64", cl(L, 11, "i", 2, "u32")))),
                ret(L, 12, "return h;", cl(L, 12, "h", 8, "u64"))))

    L = ["manna v0;", "struct P { v: u16, }", "fn f(i: u8) -> P {", "    let a: [4]u8 = [i; 4];", "    let b = a[u32(i)];", "    let c = [u16(i), 2];", "    return P(v: u16(b));", "}"]
    g.put("convert_in_index_array_and_construction", L, sdecl(L, 2, 2, "P", 2, 2, [(2, "v: u16", "v", "u16", 0)]) +
          plain(L, 3, 8, "f", [("i: u8", "i", "u8")], "P",
                let(L, 4, "let a: [4]u8 = [i; 4];", "a", 1, "[4]u8", rep(L, 4, "[i; 4]", 4, "[4]u8", cl(L, 4, "i", 0, "u8"))),
                let(L, 5, "let b = a[u32(i)];", "b", 2, "u8", idx(L, 5, "a[u32(i)]", "u8", cl(L, 5, "a", 1, "[4]u8"), cv(L, 5, "u32(i)", "u32", cl(L, 5, "i", 0, "u8")))),
                let(L, 6, "let c = [u16(i), 2];", "c", 3, "[2]u16", arr(L, 6, "[u16(i), 2]", "[2]u16", cv(L, 6, "u16(i)", "u16", cl(L, 6, "i", 0, "u8")), nm(L, 6, "2", "u16"))),
                ret(L, 7, "return P(v: u16(b));", snew(L, 7, "P(v: u16(b))", "P", cv(L, 7, "u16(b)", "u16", cl(L, 7, "b", 2, "u8"))))))

    L = ["manna v0;", "fn f(x: u8) -> u32 {", "    return u32(x + 1);", "}", "fn g(x: u8) -> u32 {", "    return u32(255 + x);", "}"]
    g.put("convert_operand_is_typed_on_its_own", L,
          plain(L, 2, 4, "f", [("x: u8", "x", "u8")], "u32", ret(L, 3, "return u32(x + 1);", cv(L, 3, "u32(x + 1)", "u32", g.N("binary", g.sp(L, 3, "x + 1"), "+ u8", cl(L, 3, "x", 0, "u8"), nm(L, 3, "1", "u8"))))) +
          plain(L, 5, 7, "g", [("x: u8", "x", "u8")], "u32", ret(L, 6, "return u32(255 + x);", cv(L, 6, "u32(255 + x)", "u32", g.N("binary", g.sp(L, 6, "255 + x"), "+ u8", nm(L, 6, "255", "u8"), cl(L, 6, "x", 0, "u8"))))))

    # ---------- E0225: a literal operand has no type ----------
    for n in ("255", "300"):
        L = ["manna v0;", "fn f() -> u8 {", "    return u8(%s);" % n, "}"]
        g.errors("convert_literal_%s_has_no_type" % n, L, [e(L, 3, n, "E0225", infer)])
    L = ["manna v0;", "fn f() {", "    let a: u32 = u32(-1);", "    let b: u8 = u8(1 + 2);", "    let c: i8 = i8(-(1));", "    let d: u64 = u64(~0);", "    let e: u8 = u8(true);", "}"]
    g.errors("convert_literal_groups_have_no_type", L, [e(L, 3, "-1", "E0225", infer), e(L, 4, "1 + 2", "E0225", infer), e(L, 5, "-(1)", "E0225", infer), e(L, 6, "~0", "E0225", infer),
                                                        e(L, 7, "u8(true)", "E0265", cannot("bool", "u8"))])

    # ---------- E0265 ----------
    L = ["manna v0;", "struct P { x: u8, }", "fn f(x: u8, b: bool, p: P, a: [2]u8, s: []u8) {", "    let c = bool(x);", "    let d = u8(b);", "    let e = bool(b);", "    let g = u8(p);",
         "    let h = u16(a);", "    let i = i8(s);", "    let j = u8(true);", "    let k = bool(1);", "}"]
    g.errors("convert_bool_and_non_integers", L, [e(L, 4, "bool(x)", "E0265", cannot("u8", "bool", True)), e(L, 5, "u8(b)", "E0265", cannot("bool", "u8")), e(L, 6, "bool(b)", "E0265", cannot("bool", "bool")),
        e(L, 7, "u8(p)", "E0265", cannot("P", "u8")), e(L, 8, "u16(a)", "E0265", cannot("[2]u8", "u16")), e(L, 9, "i8(s)", "E0265", cannot("[]u8", "i8")),
        e(L, 10, "u8(true)", "E0265", cannot("bool", "u8")), e(L, 11, "1", "E0225", infer)])
    L = ["manna v0;", "fn nothing() {", "}", "fn f(b: bool) {", "    let a = u8(nothing());", "    let c = u8(nope);", "    let d = bool(nope);", "    let e: bool = u8(b);", "    let g: u8 = u8(nope);", "}"]
    g.errors("convert_unit_error_operands_and_the_result_type", L, [e(L, 5, "nothing()", "E0226", msg_unit), e(L, 6, "nope", "E0230", unk("nope")), e(L, 7, "nope", "E0230", unk("nope")),
        e(L, 8, "u8(b)", "E0265", cannot("bool", "u8")), e(L, 8, "u8(b)", "E0223", mismatch("bool", "u8")), e(L, 9, "nope", "E0230", unk("nope"))])
    L = ["manna v0;", "fn f(x: u8) {", "    u8(x) = 1;", "    u32(x) = u32(x);", "}"]
    g.errors("convert_is_not_an_lvalue", L, [e(L, 3, "u8(x)", "E0270", "can't assign to this expression"), e(L, 4, "u32(x)", "E0270", "can't assign to this expression", 0)])
