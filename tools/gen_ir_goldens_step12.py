# Generates the typed-IR step 12 goldens (tests/ir/array_*, repeat_*, index_*, slice_*: array literals, [v; N], indexing, slicing, element assignment, E0252, E0253, and the array size limit E0222 for literals; step 12b adds len_*) from a spec whose columns come from the source text, independent of the C builder; run from the repo root.
import gen_ir_goldens_step7 as g
import gen_ir_goldens_step10 as c
import gen_ir_goldens_step11 as s11

plain, ret, call, cl, nm = c.plain, c.ret, c.call, c.cl, c.nm
let, assign, estmt, sdecl, snew, fld = s11.let, s11.assign, s11.estmt, s11.sdecl, s11.snew, s11.fld
msg_unit = c.msg_unit
infer = "cannot infer the type of this integer literal; annotate it"
big = "type is too large (size exceeds the limit)"
sos = "a slice's element type can't contain a slice type"

def arr(L, ln, text, t, *elems, occ=0):
    return g.N("array", g.sp(L, ln, text, occ), t, *elems)

def rep(L, ln, text, count, t, v, occ=0):
    return g.N("repeat", g.sp(L, ln, text, occ), "%d %s" % (count, t), v)

def idx(L, ln, text, t, base, i, occ=0):
    return g.N("index", g.sp(L, ln, text, occ), t, base, i)

def slc(L, ln, text, base, lo, hi, occ=0):
    return g.N("slice", g.sp(L, ln, text, occ), "[]u8", base, lo, hi)

def e(L, ln, needle, code, msg, occ=0): return g.err(L, ln, needle, code, msg, occ)
def mismatch(a, b): return "type mismatch: expected %s, found %s" % (a, b)
def noindex(t): return "can't index/slice type %s" % t
def notint(t): return "index/bound must be an integer, found %s" % t
def unk(n): return "unknown name '%s'" % n
notassign = "can't assign to this expression"

if __name__ == "__main__":
    # ---------- accepted ----------
    L = ["manna v0;", "fn f(n: u32) {", "    let a: [3]u8 = [4, 5, 6];", "    let b = [n, 5];", "    let c: [2][2]u8 = [[7, 8], [9, 1]];", "}"]
    nu = lambda ln, t, *ts: [nm(L, ln, x, t) for x in ts]
    g.put("array_literal_basic", L, plain(L, 2, 6, "f", [("n: u32", "n", "u32")], "",
        let(L, 3, "let a: [3]u8 = [4, 5, 6];", "a", 1, "[3]u8", arr(L, 3, "[4, 5, 6]", "[3]u8", *nu(3, "u8", "4", "5", "6"))),
        let(L, 4, "let b = [n, 5];", "b", 2, "[2]u32", arr(L, 4, "[n, 5]", "[2]u32", cl(L, 4, "n", 0, "u32"), nm(L, 4, "5", "u32"))),
        let(L, 5, "let c: [2][2]u8 = [[7, 8], [9, 1]];", "c", 3, "[2][2]u8", arr(L, 5, "[[7, 8], [9, 1]]", "[2][2]u8",
            arr(L, 5, "[7, 8]", "[2]u8", nm(L, 5, "7", "u8"), nm(L, 5, "8", "u8")), arr(L, 5, "[9, 1]", "[2]u8", nm(L, 5, "9", "u8"), nm(L, 5, "1", "u8"))))))

    L = ["manna v0;", "fn take(a: [2]u8) -> u8 {", "    return a[0];", "}", "fn make() -> [2]u8 {", "    return [3, 4];", "}", "fn f() -> u8 {", "    return take([5, 6]);", "}"]
    g.put("array_literal_as_argument_and_return_value", L,
          plain(L, 2, 4, "take", [("a: [2]u8", "a", "[2]u8")], "u8", ret(L, 3, "return a[0];", idx(L, 3, "a[0]", "u8", cl(L, 3, "a", 0, "[2]u8"), nm(L, 3, "0", "u64")))) +
          plain(L, 5, 7, "make", [], "[2]u8", ret(L, 6, "return [3, 4];", arr(L, 6, "[3, 4]", "[2]u8", nm(L, 6, "3", "u8"), nm(L, 6, "4", "u8")))) +
          plain(L, 8, 10, "f", [], "u8", ret(L, 9, "return take([5, 6]);", call(L, 9, "take([5, 6])", "u8", "take", arr(L, 9, "[5, 6]", "[2]u8", nm(L, 9, "5", "u8"), nm(L, 9, "6", "u8"))))))

    L = ["manna v0;", "fn one() -> u32 {", "    return 7;", "}", "fn f(n: u32) {", "    let a: [4]u8 = [3; 4];", "    let b = [n; 5];", "    let c: [0]u8 = [2; 0];",
         "    let d = [n; 0];", "    let e = [one(); 2];", "}"]
    g.put("repeat_basic_including_zero_count", L,
          plain(L, 2, 4, "one", [], "u32", ret(L, 3, "return 7;", nm(L, 3, "7", "u32"))) +
          plain(L, 5, 11, "f", [("n: u32", "n", "u32")], "",
                let(L, 6, "let a: [4]u8 = [3; 4];", "a", 1, "[4]u8", rep(L, 6, "[3; 4]", 4, "[4]u8", nm(L, 6, "3", "u8"))),
                let(L, 7, "let b = [n; 5];", "b", 2, "[5]u32", rep(L, 7, "[n; 5]", 5, "[5]u32", cl(L, 7, "n", 0, "u32"))),
                let(L, 8, "let c: [0]u8 = [2; 0];", "c", 3, "[0]u8", rep(L, 8, "[2; 0]", 0, "[0]u8", nm(L, 8, "2", "u8"))),
                let(L, 9, "let d = [n; 0];", "d", 4, "[0]u32", rep(L, 9, "[n; 0]", 0, "[0]u32", cl(L, 9, "n", 0, "u32"))),
                let(L, 10, "let e = [one(); 2];", "e", 5, "[2]u32", rep(L, 10, "[one(); 2]", 2, "[2]u32", call(L, 10, "one()", "u32", "one")))))

    L = ["manna v0;", "fn f(n: u8) {", "    let b = [n; 2147483647];", "}"]
    g.put("repeat_at_the_size_limit_is_accepted", L, plain(L, 2, 4, "f", [("n: u8", "n", "u8")], "",
          let(L, 3, "let b = [n; 2147483647];", "b", 1, "[2147483647]u8", rep(L, 3, "[n; 2147483647]", 2147483647, "[2147483647]u8", cl(L, 3, "n", 0, "u8")))))

    L = ["manna v0;", "fn f(a: [4]u8, s: []u8, n: u32, m: i64, g: [2][3]u8) {", "    let w = a[1];", "    let x = a[n];", "    let y = a[m];", "    let z = s[n];",
         "    let v = g[1][2];", "    let u = a[9];", "}"]
    P = [("a: [4]u8", "a", "[4]u8"), ("s: []u8", "s", "[]u8"), ("n: u32", "n", "u32"), ("m: i64", "m", "i64"), ("g: [2][3]u8", "g", "[2][3]u8")]
    g.put("index_basic", L, plain(L, 2, 9, "f", P, "",
          let(L, 3, "let w = a[1];", "w", 5, "u8", idx(L, 3, "a[1]", "u8", cl(L, 3, "a", 0, "[4]u8"), nm(L, 3, "1", "u64"))),
          let(L, 4, "let x = a[n];", "x", 6, "u8", idx(L, 4, "a[n]", "u8", cl(L, 4, "a", 0, "[4]u8"), cl(L, 4, "n", 2, "u32"))),
          let(L, 5, "let y = a[m];", "y", 7, "u8", idx(L, 5, "a[m]", "u8", cl(L, 5, "a", 0, "[4]u8"), cl(L, 5, "m", 3, "i64"))),
          let(L, 6, "let z = s[n];", "z", 8, "u8", idx(L, 6, "s[n]", "u8", cl(L, 6, "s", 1, "[]u8"), cl(L, 6, "n", 2, "u32"))),
          let(L, 7, "let v = g[1][2];", "v", 9, "u8", idx(L, 7, "g[1][2]", "u8", idx(L, 7, "g[1]", "[3]u8", cl(L, 7, "g", 4, "[2][3]u8"), nm(L, 7, "1", "u64")), nm(L, 7, "2", "u64"))),
          let(L, 8, "let u = a[9];", "u", 10, "u8", idx(L, 8, "a[9]", "u8", cl(L, 8, "a", 0, "[4]u8"), nm(L, 8, "9", "u64")))))

    L = ["manna v0;", "fn f(a: [4]u8, s: []u8, n: u32) {", "    let b = a[1..3];", "    let c = s[n..n];", "    let d = b[0..2];", "    let e = a[0..n];", "}"]
    P = [("a: [4]u8", "a", "[4]u8"), ("s: []u8", "s", "[]u8"), ("n: u32", "n", "u32")]
    g.put("slice_basic", L, plain(L, 2, 7, "f", P, "",
          let(L, 3, "let b = a[1..3];", "b", 3, "[]u8", slc(L, 3, "a[1..3]", cl(L, 3, "a", 0, "[4]u8"), nm(L, 3, "1", "u64"), nm(L, 3, "3", "u64"))),
          let(L, 4, "let c = s[n..n];", "c", 4, "[]u8", slc(L, 4, "s[n..n]", cl(L, 4, "s", 1, "[]u8"), cl(L, 4, "n", 2, "u32", 0), cl(L, 4, "n", 2, "u32", 1))),
          let(L, 5, "let d = b[0..2];", "d", 5, "[]u8", slc(L, 5, "b[0..2]", cl(L, 5, "b", 3, "[]u8"), nm(L, 5, "0", "u64"), nm(L, 5, "2", "u64"))),
          let(L, 6, "let e = a[0..n];", "e", 6, "[]u8", slc(L, 6, "a[0..n]", cl(L, 6, "a", 0, "[4]u8"), nm(L, 6, "0", "u64"), cl(L, 6, "n", 2, "u32")))))

    L = ["manna v0;", "struct P { a: [2]u8, }", "fn f(a: [3]u8, s: []u8, p: P, m: [2][2]u8, n: u32) {", "    a[0] = 5;", "    a[n] = a[1];", "    s[2] = 6;", "    p.a[1] = 7;", "    m[1][0] = 8;", "}"]
    P = [("a: [3]u8", "a", "[3]u8"), ("s: []u8", "s", "[]u8"), ("p: P", "p", "P"), ("m: [2][2]u8", "m", "[2][2]u8"), ("n: u32", "n", "u32")]
    g.put("index_assignment", L, sdecl(L, 2, 2, "P", 2, 1, [(2, "a: [2]u8", "a", "[2]u8", 0)]) + plain(L, 3, 9, "f", P, "",
          assign(L, 4, "a[0] = 5;", idx(L, 4, "a[0]", "u8", cl(L, 4, "a", 0, "[3]u8"), nm(L, 4, "0", "u64")), nm(L, 4, "5", "u8")),
          assign(L, 5, "a[n] = a[1];", idx(L, 5, "a[n]", "u8", cl(L, 5, "a", 0, "[3]u8", 0), cl(L, 5, "n", 4, "u32")), idx(L, 5, "a[1]", "u8", cl(L, 5, "a", 0, "[3]u8", 1), nm(L, 5, "1", "u64"))),
          assign(L, 6, "s[2] = 6;", idx(L, 6, "s[2]", "u8", cl(L, 6, "s", 1, "[]u8"), nm(L, 6, "2", "u64")), nm(L, 6, "6", "u8")),
          assign(L, 7, "p.a[1] = 7;", idx(L, 7, "p.a[1]", "u8", fld(L, 7, "p.a", "a", 0, "[2]u8", cl(L, 7, "p", 2, "P")), nm(L, 7, "1", "u64")), nm(L, 7, "7", "u8")),
          assign(L, 8, "m[1][0] = 8;", idx(L, 8, "m[1][0]", "u8", idx(L, 8, "m[1]", "[2]u8", cl(L, 8, "m", 3, "[2][2]u8"), nm(L, 8, "1", "u64")), nm(L, 8, "0", "u64")), nm(L, 8, "8", "u8"))))

    L = ["manna v0;", "struct Q { x: u8, }", "struct R { qs: [2]Q, }", "fn f(r: R, ps: [3]Q) -> u8 {", "    ps[1].x = 4;", "    r.qs[0].x = 5;", "    return r.qs[1].x + ps[2].x;", "}"]
    P = [("r: R", "r", "R"), ("ps: [3]Q", "ps", "[3]Q")]
    qs = lambda ln, k, occ=0: fld(L, ln, "r.qs", "qs", 0, "[2]Q", cl(L, ln, "r", 0, "R", occ))
    g.put("index_postfix_chains_over_structs", L, sdecl(L, 2, 2, "Q", 1, 1, [(2, "x: u8", "x", "u8", 0)]) + sdecl(L, 3, 3, "R", 2, 1, [(3, "qs: [2]Q", "qs", "[2]Q", 0)]) +
          plain(L, 4, 8, "f", P, "u8",
                assign(L, 5, "ps[1].x = 4;", fld(L, 5, "ps[1].x", "x", 0, "u8", idx(L, 5, "ps[1]", "Q", cl(L, 5, "ps", 1, "[3]Q"), nm(L, 5, "1", "u64"))), nm(L, 5, "4", "u8")),
                assign(L, 6, "r.qs[0].x = 5;", fld(L, 6, "r.qs[0].x", "x", 0, "u8", idx(L, 6, "r.qs[0]", "Q", qs(6, 0), nm(L, 6, "0", "u64"))), nm(L, 6, "5", "u8")),
                ret(L, 7, "return r.qs[1].x + ps[2].x;", g.N("binary", g.sp(L, 7, "r.qs[1].x + ps[2].x"), "+ u8",
                    fld(L, 7, "r.qs[1].x", "x", 0, "u8", idx(L, 7, "r.qs[1]", "Q", qs(7, 0), nm(L, 7, "1", "u64"))),
                    fld(L, 7, "ps[2].x", "x", 0, "u8", idx(L, 7, "ps[2]", "Q", cl(L, 7, "ps", 1, "[3]Q"), nm(L, 7, "2", "u64")))))))

    L = ["manna v0;", "fn copy(a: [2]u8) -> [2]u8 {", "    let b = a;", "    return b;", "}"]
    g.put("array_is_a_value_let_and_return_copy", L, plain(L, 2, 5, "copy", [("a: [2]u8", "a", "[2]u8")], "[2]u8",
          let(L, 3, "let b = a;", "b", 1, "[2]u8", cl(L, 3, "a", 0, "[2]u8")), ret(L, 4, "return b;", cl(L, 4, "b", 1, "[2]u8"))))

    # ---------- E0252 / E0253 ----------
    L = ["manna v0;", "struct P { x: u8, }", "fn one() -> u8 {", "    return 1;", "}", "fn f(n: u8, b: bool, p: P) {", "    let a = n[0];", "    let c = b[1..2];",
         "    let d = p[0];", "    let e = one()[0];", "    let g = (1)[0];", "    let h = nope[0];", "    let i = n[nope];", "}"]
    g.errors("index_base_must_be_an_array_or_a_slice", L, [e(L, 7, "n", "E0252", noindex("u8")), e(L, 8, "b", "E0252", noindex("bool")), e(L, 9, "p", "E0252", noindex("P")),
        e(L, 10, "one()", "E0252", noindex("u8")), e(L, 11, "(1)", "E0225", infer), e(L, 12, "nope", "E0230", unk("nope")),
        e(L, 13, "n", "E0252", noindex("u8")), e(L, 13, "nope", "E0230", unk("nope"))])
    L = ["manna v0;", "struct P { x: u8, }", "fn f(a: [4]u8, b: bool, p: P, m: [2]u8) {", "    let x = a[b];", "    let y = a[p];", "    let z = a[m];", "    let w = a[true..2];",
         "    let v = a[0..p];", "    let u = a[nope];", "}"]
    g.errors("index_and_bounds_must_be_integers", L, [e(L, 4, "b", "E0253", notint("bool")), e(L, 5, "p", "E0253", notint("P")), e(L, 6, "m", "E0253", notint("[2]u8")),
        e(L, 7, "true", "E0253", notint("bool")), e(L, 8, "p", "E0253", notint("P")), e(L, 9, "nope", "E0230", unk("nope"))])
    L = ["manna v0;", "fn f(a: [4]u8) {", "    let z = a[-1];", "}"]
    g.errors("index_negative_literal_is_an_unsigned_operator_error", L, [e(L, 3, "-1", "E0260", "operator '-' is not defined for u64")])

    # ---------- E0222 (literals) ----------
    L = ["manna v0;", "fn f(n: u8, big: [1073741824]u8) {", "    let a = [n; 2147483648];", "    let b = [n; 2147483647];", "    let c = [big, big];", "    let d = [big; 2];", "    let e = [n; 0];", "}"]
    g.errors("array_literal_size_limit", L, [e(L, 3, "[n; 2147483648]", "E0222", big), e(L, 5, "[big, big]", "E0222", big), e(L, 6, "[big; 2]", "E0222", big)])

    # ---------- E0223 / E0224 / E0225 / E0226 / E0227 ----------
    L = ["manna v0;", "fn take(s: []u8) {", "}", "fn give(a: [3]u8) {", "    take(a);", "}", "fn f(n: u32, a: [3]u8, s: []u8) {", "    let b: [3]u8 = [4, 5];", "    let c: []u8 = [6, 7];",
         "    let d: [2]u8 = [8; 3];", "    let e: u8 = [n, n];", "    let g = [n, true];", "    let h: [2]u8 = a;", "    let i: [2]u8 = [2, 300];", "    let j: [2]u8 = s;", "}"]
    g.errors("array_type_mismatches", L, [e(L, 5, "a", "E0223", mismatch("[]u8", "[3]u8")), e(L, 8, "[4, 5]", "E0223", mismatch("[3]u8", "[2]u8")), e(L, 9, "[6, 7]", "E0223", mismatch("[]u8", "[2]u8")),
        e(L, 10, "[8; 3]", "E0223", mismatch("[2]u8", "[3]u8")), e(L, 11, "[n, n]", "E0223", mismatch("u8", "[2]u32")), e(L, 12, "true", "E0223", mismatch("u32", "bool")),
        e(L, 13, "a", "E0223", mismatch("[2]u8", "[3]u8")), e(L, 14, "300", "E0224", "integer literal 300 does not fit in u8"), e(L, 15, "s", "E0223", mismatch("[2]u8", "[]u8"))])
    L = ["manna v0;", "fn f() {", "    let a = [1, 2];", "    let b = [0; 3];", "    let c = [1, nope];", "}"]
    g.errors("array_literal_needs_an_element_type", L, [e(L, 3, "1", "E0225", infer), e(L, 4, "0", "E0225", infer), e(L, 5, "1", "E0225", infer), e(L, 5, "nope", "E0230", unk("nope"))])
    L = ["manna v0;", "fn nothing() {", "}", "fn f(a: [2]u8, n: u8) {", "    let x = [nothing()];", "    let y = [n, nothing()];", "    let z = [nothing(); 2];", "    let w = a[nothing()];",
         "    let v = nothing()[0];", "    let u = a[0..nothing()];", "    let t: [2]u8 = [nothing(), 1];", "}"]
    g.errors("array_unit_values_are_not_values", L, [e(L, 5, "nothing()", "E0226", msg_unit), e(L, 6, "nothing()", "E0226", msg_unit), e(L, 7, "nothing()", "E0226", msg_unit),
        e(L, 8, "nothing()", "E0226", msg_unit), e(L, 9, "nothing()", "E0226", msg_unit), e(L, 10, "nothing()", "E0226", msg_unit), e(L, 11, "nothing()", "E0226", msg_unit)])
    L = ["manna v0;", "fn f(ss: [2][]u8, s: []u8) {", "    let a = ss[0..1];", "    let b = ss[1];", "    let c = s[0..1];", "}"]
    g.errors("slice_of_an_array_of_slices_is_a_slice_of_slice", L, [e(L, 3, "ss[0..1]", "E0227", sos)])

    # ---------- element assignment ----------
    L = ["manna v0;", "fn f(a: [2]u8, n: u8, b: bool) {", "    a[0] = true;", "    a[b] = 1;", "    n[0] = 1;", "    a[0..1] = a;", "    [1, 2][0] = 3;", "    a[0] = 300;", "}"]
    g.errors("index_assignment_errors", L, [e(L, 3, "true", "E0223", mismatch("u8", "bool")), e(L, 4, "b", "E0253", notint("bool")), e(L, 5, "n", "E0252", noindex("u8")),
        e(L, 6, "a[0..1]", "E0270", notassign), e(L, 7, "[1, 2][0]", "E0270", notassign), e(L, 8, "300", "E0224", "integer literal 300 does not fit in u8")])

    # ---------- pins for the mutation survivors (A07, A15, A22, A23) ----------
    L = ["manna v0;", "struct Q { x: u32, }", "fn f(a: [4]u32, q: [3]Q) {", "    let b = a[0..2];", "    let c = q[1..3];", "}"]
    def slcT(L, ln, text, t, base, lo, hi):
        return g.N("slice", g.sp(L, ln, text), t, base, lo, hi)
    g.put("slice_element_type_follows_the_base", L, sdecl(L, 2, 2, "Q", 4, 4, [(2, "x: u32", "x", "u32", 0)]) + plain(L, 3, 6, "f", [("a: [4]u32", "a", "[4]u32"), ("q: [3]Q", "q", "[3]Q")], "",
          let(L, 4, "let b = a[0..2];", "b", 2, "[]u32", slcT(L, 4, "a[0..2]", "[]u32", cl(L, 4, "a", 0, "[4]u32"), nm(L, 4, "0", "u64"), nm(L, 4, "2", "u64"))),
          let(L, 5, "let c = q[1..3];", "c", 3, "[]Q", slcT(L, 5, "q[1..3]", "[]Q", cl(L, 5, "q", 1, "[3]Q"), nm(L, 5, "1", "u64"), nm(L, 5, "3", "u64")))))
    L = ["manna v0;", "fn f() {", "    let a: Nope = [1, 2];", "    let b: Nope = [3; 4];", "}"]
    g.errors("array_literal_against_a_failed_annotation_is_silent", L, [e(L, 3, "Nope", "E0220", "unknown type 'Nope'"), e(L, 4, "Nope", "E0220", "unknown type 'Nope'")])
    L = ["manna v0;", "struct P { a: [1073741824]u8, }", "fn f(s: []u8, p: P) {", "    let a = [s; 134217728];", "    let b = [s; 134217727];", "    let c = [p; 2];", "    let d = [p; 1];", "}"]
    g.errors("array_literal_size_limit_slices_and_structs", L, [e(L, 4, "[s; 134217728]", "E0222", big), e(L, 6, "[p; 2]", "E0222", big)])

    # ================= step 12b: len(x), E0254-E0257 =================
    def ln(L, n, text, arg):
        return g.N("len", g.sp(L, n, text), "u64", arg)

    L = ["manna v0;", "fn make() -> [3]u8 {", "    return [1, 2, 3];", "}", "fn f(a: [4]u8, s: []u8) -> u64 {", "    let x = len(a);", "    let y = len(s);", "    let z = len(make());",
         "    let w = a[0..len(a)];", "    let v = a[len(a) - 1];", "    return x + y + z;", "}"]
    mk = call(L, 8, "make()", "[3]u8", "make")
    g.put("len_of_array_slice_and_call_result", L,
        plain(L, 2, 4, "make", [], "[3]u8", ret(L, 3, "return [1, 2, 3];", arr(L, 3, "[1, 2, 3]", "[3]u8", nm(L, 3, "1", "u8"), nm(L, 3, "2", "u8"), nm(L, 3, "3", "u8")))) +
        plain(L, 5, 12, "f", [("a: [4]u8", "a", "[4]u8"), ("s: []u8", "s", "[]u8")], "u64",
            let(L, 6, "let x = len(a);", "x", 2, "u64", ln(L, 6, "len(a)", cl(L, 6, "a", 0, "[4]u8"))),
            let(L, 7, "let y = len(s);", "y", 3, "u64", ln(L, 7, "len(s)", cl(L, 7, "s", 1, "[]u8"))),
            let(L, 8, "let z = len(make());", "z", 4, "u64", ln(L, 8, "len(make())", mk)),
            let(L, 9, "let w = a[0..len(a)];", "w", 5, "[]u8", slc(L, 9, "a[0..len(a)]", cl(L, 9, "a", 0, "[4]u8", 0), nm(L, 9, "0", "u64"), ln(L, 9, "len(a)", cl(L, 9, "a", 0, "[4]u8", 1)))),
            let(L, 10, "let v = a[len(a) - 1];", "v", 6, "u8", idx(L, 10, "a[len(a) - 1]", "u8", cl(L, 10, "a", 0, "[4]u8", 0),
                g.N("binary", g.sp(L, 10, "len(a) - 1"), "- u64", ln(L, 10, "len(a)", cl(L, 10, "a", 0, "[4]u8", 1)), nm(L, 10, "1", "u64")))),
            ret(L, 11, "return x + y + z;", g.N("binary", g.sp(L, 11, "x + y + z"), "+ u64", g.N("binary", g.sp(L, 11, "x + y"), "+ u64", cl(L, 11, "x", 2, "u64"), cl(L, 11, "y", 3, "u64")), cl(L, 11, "z", 4, "u64")))))

    L = ["manna v0;", "fn f(a: [2]u8) {", "    let x = len();", "    let y = len(a, a);", "    let z = len(v: a);", "    let w = len(nope, nope2);", "}"]
    one = lambda found: "len takes exactly one argument, found %s" % found
    g.errors("len_wrong_argument_count_or_named_argument", L, [e(L, 3, "len()", "E0254", one(0)), e(L, 4, "len(a, a)", "E0254", one(2)), e(L, 5, "len(v: a)", "E0254", one("a named argument")),
        e(L, 6, "len(nope, nope2)", "E0254", one(2)), e(L, 6, "nope", "E0230", unk("nope")), e(L, 6, "nope2", "E0230", unk("nope2"))])
    needs = lambda t: "len needs an array or a slice, found %s" % t
    L = ["manna v0;", "struct P { x: u8, }", "fn f(n: u8, b: bool, p: P) {", "    let x = len(n);", "    let y = len(b);", "    let z = len(p);", "    let w = len(1);", "    let v = len(nope);",
         "    let u: bool = len(b);", "    let t: bool = len(nope);", "}"]
    g.errors("len_argument_must_be_an_array_or_a_slice", L, [e(L, 4, "n", "E0255", needs("u8")), e(L, 5, "b", "E0255", needs("bool")), e(L, 6, "p", "E0255", needs("P")), e(L, 7, "1", "E0225", infer),
        e(L, 8, "nope", "E0230", unk("nope")), e(L, 9, "b", "E0255", needs("bool"), 0), e(L, 9, "len(b)", "E0223", mismatch("bool", "u64")),
        e(L, 10, "nope", "E0230", unk("nope")), e(L, 10, "len(nope)", "E0223", mismatch("bool", "u64"))])
    nc = "len is a builtin function; call it as len(x)"
    L = ["manna v0;", "fn f(a: [2]u8) {", "    let x = len;", "    let y = len + 1;", "    len = 3;", "}"]
    g.errors("len_is_not_a_value", L, [e(L, 3, "len", "E0256", nc), e(L, 4, "len", "E0256", nc), e(L, 5, "len", "E0256", nc)])
    dc = "'len' is a builtin and can't be declared"
    L = ["manna v0;", "struct len { x: u8, }"]
    g.errors("len_cannot_name_a_struct", L, [e(L, 2, "len", "E0257", dc)])
    L = ["manna v0;", "fn len() {", "}", "fn f(a: [2]u8) {", "    let n = len(a);", "}"]
    g.errors("len_cannot_name_a_function", L, [e(L, 2, "len", "E0257", dc)])
    L = ["manna v0;", "fn f(len: u8) -> u8 {", "    return len;", "}"]
    g.errors("len_cannot_name_a_parameter", L, [e(L, 2, "len", "E0257", dc)])
    L = ["manna v0;", "fn f(a: [2]u8) -> u64 {", "    let len: u64 = 5;", "    let n = len(a);", "    return len + n;", "}"]
    g.errors("len_cannot_name_a_local_but_the_call_stays_the_builtin", L, [e(L, 3, "len", "E0257", dc)])
    L = ["manna v0;", "fn f() {", "    for let len: u8 = 0; len < 3; len = len + 1 {", "    }", "}"]
    g.errors("len_cannot_name_a_loop_variable", L, [e(L, 3, "len", "E0257", dc)])

    # pin for the mutation survivor B07
    L = ["manna v0;", "fn nothing() {", "}", "fn f() {", "    let x = len(nothing());", "    let y: u64 = x;", "}"]
    g.errors("len_unit_argument_is_not_a_value", L, [e(L, 5, "nothing()", "E0226", msg_unit)])

    # covers the bool element size in the literal size check
    L = ["manna v0;", "fn f(b: bool) {", "    let a = [b; 2147483648];", "    let c = [b; 2147483647];", "}"]
    g.errors("array_literal_size_limit_bool_elements", L, [e(L, 3, "[b; 2147483648]", "E0222", big)])

    # 12d: an array literal where a non-array type is expected is ONE mismatch, even when its elements are bare literals
    L = ["manna v0;", "fn f(n: u32) {", "    let x: u8 = [1, 2];", "    let y: bool = [3; 2];", "    let z: u8 = [1, n];", "    let w: u8 = [n, n];", "}"]
    g.errors("array_literal_where_a_non_array_is_expected", L, [e(L, 3, "[1, 2]", "E0223", mismatch("u8", "array literal")), e(L, 4, "[3; 2]", "E0223", mismatch("bool", "array literal")),
        e(L, 5, "[1, n]", "E0223", mismatch("u8", "array literal")), e(L, 6, "[n, n]", "E0223", mismatch("u8", "[2]u32"))])
