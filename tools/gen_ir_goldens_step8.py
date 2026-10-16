# Generates the typed-IR step 8 goldens (tests/ir/op_*: the per-operator type matrix, unary operators, literal-on-the-left, no-cascade) from rules written here, with columns computed from the source text, independent of the C checker; run from the repo root.
import re
import gen_ir_goldens_step7 as g

OPS = [("+", "add"), ("-", "sub"), ("*", "mul"), ("/", "div"), ("%", "mod"),
       ("&", "bitand"), ("|", "bitor"), ("^", "bitxor"), ("<<", "shl"), (">>", "shr"),
       ("<", "lt"), (">", "gt"), ("<=", "le"), (">=", "ge"), ("==", "eq"), ("!=", "ne"),
       ("&&", "and"), ("||", "or")]
INTS = ["u8", "i8", "u32", "i64"]

def klass(op):
    if op in ("&&", "||"): return "logical"
    if op in ("==", "!="): return "equality"
    if op in ("<", ">", "<=", ">="): return "relational"
    if op in ("<<", ">>"): return "shift"
    return "arith"

# ---- the typing rules, written out independently of the C code ----
def result(op, lt, rt):
    """('ok', result type) | ('E0260', offending type) | ('E0261', None)"""
    k = klass(op)
    if k == "logical":
        for t in (lt, rt):
            if t != "bool": return ("E0260", t)
        return ("ok", "bool")
    allowed = lambda t: t in INTS or (k == "equality" and t == "bool")
    for t in (lt, rt):
        if not allowed(t): return ("E0260", t)
    if lt != rt: return ("E0261", None)
    return ("ok", lt if k in ("arith", "shift") else "bool")

def inner(L, ln, outer, needle, occ=0):
    base = g.col(L, ln, outer)
    seg = L[ln - 1][base - 1: base - 1 + len(outer)]
    pat = r"(?<![A-Za-z0-9_])" + re.escape(needle) + r"(?![A-Za-z0-9_])" if re.fullmatch(r"[A-Za-z0-9_]+", needle) else re.escape(needle)
    m = list(re.finditer(pat, seg))[occ]
    c = base + m.start()
    return "%d:%d-%d:%d" % (ln, c, ln, c + len(needle)), c

def errcol(L, ln, outer, needle, occ=0):
    return inner(L, ln, outer, needle, occ)[1]

def one_line_fn(name, params, body_src):
    return "fn %s(%s) { %s }" % (name, ", ".join("%s: %s" % p for p in params), body_src)

def matrix_for(op, nm):
    k = klass(op)
    types_ok = INTS + (["bool"] if k == "equality" else [])
    if k == "logical": types_ok = ["bool"]
    # ---- valid combinations ----
    L = ["manna v0;"]
    dumps = []
    for i, t in enumerate(types_ok):
        stmt_src = "let r = a %s b;" % op
        line = one_line_fn("case_%d" % (i + 1), [("a", t), ("b", t)], stmt_src)
        L.append(line)
        ln = len(L)
        res = result(op, t, t)[1]
        bin_sp, _ = inner(L, ln, "a %s b" % op, "a %s b" % op)
        bin_sp = inner(L, ln, stmt_src, "a %s b" % op)[0]
        la = g.N("local", inner(L, ln, stmt_src, "a")[0], "a #0 %s" % t)
        lb = g.N("local", inner(L, ln, stmt_src, "b")[0], "b #1 %s" % t)
        let = g.N("let", g.sp(L, ln, stmt_src), "r #2 %s" % res, g.N("binary", bin_sp, "%s %s" % (op, res), la, lb))
        blk = g.N("block", "%d:%d-%d:%d" % (ln, line.rindex("{" ) + 1 - (len(line) - len(line)), ln, len(line) + 1), "", let)
        # the block starts at the `{` that opens the body: the first `{` after the parameter list
        bstart = line.index("{") + 1
        blk = g.N("block", "%d:%d-%d:%d" % (ln, bstart, ln, len(line) + 1), "", let)
        p = " ".join("(param@%s %s #%d %s)" % (g.sp(L, ln, "%s: %s" % (n, t)), n, j, t) for j, n in enumerate("ab"))
        out = ["(fn@%d:1-%d:%d fn:case_%d" % (ln, ln, len(line) + 1, i + 1), "  (params %s)" % p, "  (returns)"]
        out += g.render(blk, 1)
        out[-1] += ")"
        dumps += out
    g.put("op_%s_ok" % nm, L, dumps)

    # ---- invalid combinations ----
    pairs = []
    if k == "logical":
        pairs = [("u8", "u8"), ("bool", "u8"), ("u8", "bool"), ("P", "bool"), ("i64", "P")]
    elif k == "equality":
        pairs = [("u8", "i8"), ("u8", "u32"), ("u8", "bool"), ("bool", "u8"), ("P", "P"), ("i64", "u8")]
    else:
        pairs = [("bool", "bool"), ("u8", "i8"), ("u8", "u32"), ("i64", "u8"), ("u8", "bool"), ("bool", "u8"), ("P", "P")]
        if k == "shift": pairs.append(("u32", "u8"))
    L = ["manna v0;", "struct P { x: u8, }"]
    errs = []
    for i, (lt, rt) in enumerate(pairs):
        stmt_src = "let r = a %s b;" % op
        L.append(one_line_fn("bad_%d" % (i + 1), [("a", lt), ("b", rt)], stmt_src))
        ln = len(L)
        kind, t = result(op, lt, rt)
        assert kind != "ok", (op, lt, rt)
        c = errcol(L, ln, stmt_src, "a %s b" % op)
        if kind == "E0260":
            errs.append("%d:%d E0260 operator '%s' is not defined for %s" % (ln, c, op, t))
        else:
            errs.append("%d:%d E0261 operands have different types: %s and %s; convert one explicitly" % (ln, c, lt, rt))
    g.errors("op_%s_bad" % nm, L, errs)

def unary():
    cases = {
        "neg": ("-", ["i8", "i64"], [("u8", "u8"), ("bool", "bool"), ("P", "P"), ("u32", "u32")]),
        "not": ("!", ["bool"], [("u8", "u8"), ("i8", "i8"), ("P", "P")]),
        "bitnot": ("~", ["u8", "i8", "u32", "i64"], [("bool", "bool"), ("P", "P")]),
    }
    for nm, (op, oks, bads) in cases.items():
        L = ["manna v0;"]
        dumps = []
        for i, t in enumerate(oks):
            stmt_src = "let r = %sa;" % op
            line = one_line_fn("case_%d" % (i + 1), [("a", t)], stmt_src)
            L.append(line)
            ln = len(L)
            ex = g.N("unary", inner(L, ln, stmt_src, "%sa" % op)[0], "%s %s" % (op, t), g.N("local", inner(L, ln, stmt_src, "a")[0], "a #0 %s" % t))
            let = g.N("let", g.sp(L, ln, stmt_src), "r #1 %s" % t, ex)
            bstart = line.index("{") + 1
            blk = g.N("block", "%d:%d-%d:%d" % (ln, bstart, ln, len(line) + 1), "", let)
            out = ["(fn@%d:1-%d:%d fn:case_%d" % (ln, ln, len(line) + 1, i + 1),
                   "  (params (param@%s a #0 %s))" % (g.sp(L, ln, "a: %s" % t), t), "  (returns)"]
            out += g.render(blk, 1)
            out[-1] += ")"
            dumps += out
        g.put("op_%s_ok" % nm, L, dumps)
        L = ["manna v0;", "struct P { x: u8, }"]
        errs = []
        for i, (t, _) in enumerate(bads):
            stmt_src = "let r = %sa;" % op
            L.append(one_line_fn("bad_%d" % (i + 1), [("a", t)], stmt_src))
            ln = len(L)
            errs.append("%d:%d E0260 operator '%s' is not defined for %s" % (ln, errcol(L, ln, stmt_src, "%sa" % op), op, t))
        g.errors("op_%s_bad" % nm, L, errs)

def ml(L, l1, start_needle, l2, occ=0, close_col=None):
    """span of a statement from the start of `start_needle` on line l1 to the last `}` on line l2."""
    c1 = g.col(L, l1, start_needle, occ)
    c2 = (L[l2 - 1].rindex("}") + 2) if close_col is None else close_col
    return g.span2(l1, c1, l2, c2)

def sibling_and_misc():
    # ---- literal-on-the-left (moved from step 6): the other operand supplies the type ----
    L = ["manna v0;",
         "fn f(x: u8, n: u32, y: i8) {",
         "    let a = 0 == x;",
         "    let b = 1 + x;",
         "    let c = 10 < n;",
         "    let d = x == 0;",
         "    let e = x + 1;",
         "    let g = n > 10;",
         "    let h = 1 + 2 + x;",
         "    let i = x + (1 + 2);",
         "    let j = -1 + y;",
         "    let k = ~0 & n;",
         "    let l = 1 << x;",
         "    let m = x << 2;",
         "}"]
    def lv(ln, stmt, name, slot, t, occ=0):
        return g.N("local", inner(L, ln, stmt, name, occ)[0], "%s #%d %s" % (name, slot, t))
    def iv(ln, stmt, text, t, occ=0):
        return g.N("int", inner(L, ln, stmt, text, occ)[0], "%s %s" % (text, t))
    def bn(ln, stmt, expr, op, t, a, b):
        return g.N("binary", inner(L, ln, stmt, expr)[0], "%s %s" % (op, t), a, b)
    def let(ln, stmt, name, slot, t, e):
        return g.stmt(L, ln, stmt, "let", "%s #%d %s" % (name, slot, t), e)
    S = lambda n: "let %s = " % n
    st = {}
    stmts = []
    stmts.append(let(3, "let a = 0 == x;", "a", 3, "bool", bn(3, "let a = 0 == x;", "0 == x", "==", "bool", iv(3, "let a = 0 == x;", "0", "u8"), lv(3, "let a = 0 == x;", "x", 0, "u8"))))
    stmts.append(let(4, "let b = 1 + x;", "b", 4, "u8", bn(4, "let b = 1 + x;", "1 + x", "+", "u8", iv(4, "let b = 1 + x;", "1", "u8"), lv(4, "let b = 1 + x;", "x", 0, "u8"))))
    stmts.append(let(5, "let c = 10 < n;", "c", 5, "bool", bn(5, "let c = 10 < n;", "10 < n", "<", "bool", iv(5, "let c = 10 < n;", "10", "u32"), lv(5, "let c = 10 < n;", "n", 1, "u32"))))
    stmts.append(let(6, "let d = x == 0;", "d", 6, "bool", bn(6, "let d = x == 0;", "x == 0", "==", "bool", lv(6, "let d = x == 0;", "x", 0, "u8"), iv(6, "let d = x == 0;", "0", "u8"))))
    stmts.append(let(7, "let e = x + 1;", "e", 7, "u8", bn(7, "let e = x + 1;", "x + 1", "+", "u8", lv(7, "let e = x + 1;", "x", 0, "u8"), iv(7, "let e = x + 1;", "1", "u8"))))
    stmts.append(let(8, "let g = n > 10;", "g", 8, "bool", bn(8, "let g = n > 10;", "n > 10", ">", "bool", lv(8, "let g = n > 10;", "n", 1, "u32"), iv(8, "let g = n > 10;", "10", "u32"))))
    inner_h = bn(9, "let h = 1 + 2 + x;", "1 + 2", "+", "u8", iv(9, "let h = 1 + 2 + x;", "1", "u8"), iv(9, "let h = 1 + 2 + x;", "2", "u8"))
    stmts.append(let(9, "let h = 1 + 2 + x;", "h", 9, "u8", bn(9, "let h = 1 + 2 + x;", "1 + 2 + x", "+", "u8", inner_h, lv(9, "let h = 1 + 2 + x;", "x", 0, "u8"))))
    inner_i = bn(10, "let i = x + (1 + 2);", "(1 + 2)", "+", "u8", iv(10, "let i = x + (1 + 2);", "1", "u8"), iv(10, "let i = x + (1 + 2);", "2", "u8"))
    stmts.append(let(10, "let i = x + (1 + 2);", "i", 10, "u8", bn(10, "let i = x + (1 + 2);", "x + (1 + 2)", "+", "u8", lv(10, "let i = x + (1 + 2);", "x", 0, "u8"), inner_i)))
    neg = g.N("unary", inner(L, 11, "let j = -1 + y;", "-1")[0], "- i8", iv(11, "let j = -1 + y;", "1", "i8"))
    stmts.append(let(11, "let j = -1 + y;", "j", 11, "i8", bn(11, "let j = -1 + y;", "-1 + y", "+", "i8", neg, lv(11, "let j = -1 + y;", "y", 2, "i8"))))
    bnot = g.N("unary", inner(L, 12, "let k = ~0 & n;", "~0")[0], "~ u32", iv(12, "let k = ~0 & n;", "0", "u32"))
    stmts.append(let(12, "let k = ~0 & n;", "k", 12, "u32", bn(12, "let k = ~0 & n;", "~0 & n", "&", "u32", bnot, lv(12, "let k = ~0 & n;", "n", 1, "u32"))))
    stmts.append(let(13, "let l = 1 << x;", "l", 13, "u8", bn(13, "let l = 1 << x;", "1 << x", "<<", "u8", iv(13, "let l = 1 << x;", "1", "u8"), lv(13, "let l = 1 << x;", "x", 0, "u8"))))
    stmts.append(let(14, "let m = x << 2;", "m", 14, "u8", bn(14, "let m = x << 2;", "x << 2", "<<", "u8", lv(14, "let m = x << 2;", "x", 0, "u8"), iv(14, "let m = x << 2;", "2", "u8"))))
    g.put("op_literal_sibling_ok", L, g.func(L, 2, 15, "f", [("x: u8", "x", "u8"), ("n: u32", "n", "u32"), ("y: i8", "y", "i8")], "", g.body(L, 2, 15, *stmts)))

    L = ["manna v0;", "fn f(x: u8, y: i8) {", "    let a = 256 + x;", "    let b = x + 256;", "    let c = 300 == x;", "    let d = -129 < y;", "}"]
    g.errors("op_literal_sibling_range", L, [
        g.err(L, 3, "256", "E0224", "integer literal 256 does not fit in u8"),
        g.err(L, 4, "256", "E0224", "integer literal 256 does not fit in u8"),
        g.err(L, 5, "300", "E0224", "integer literal 300 does not fit in u8"),
        g.err(L, 6, "-129", "E0224", "integer literal -129 does not fit in i8")])
    cant = "cannot infer the type of this integer literal; annotate it"
    L = ["manna v0;", "fn f() {", "    let a = 0 == 1;", "    let b = 1 + 2 == 3;", "    if 0 < 1 { }", "    let c = -1 != 2;", "}"]
    g.errors("op_literal_cannot_infer", L, [g.err(L, 3, "0", "E0225", cant), g.err(L, 4, "1 + 2", "E0225", cant),
                                          g.err(L, 5, "0", "E0225", cant), g.err(L, 6, "-1", "E0225", cant)])
    L = ["manna v0;", "fn f(b: bool) {", "    let a = true == 1;", "    let c = 1 == b;", "    let d = 1 + b;", "    let e = b < 1;", "}"]
    g.errors("op_literal_sibling_bool", L, [
        g.err(L, 3, "1", "E0223", "type mismatch: expected bool, found integer literal"),
        g.err(L, 4, "1", "E0223", "type mismatch: expected bool, found integer literal"),
        g.err(L, 5, "1 + b", "E0260", "operator '+' is not defined for bool"),
        g.err(L, 6, "b < 1", "E0260", "operator '<' is not defined for bool")])

    # ---- no cascades ----
    L = ["manna v0;", "fn f() {", "    let r = nope + 1;", "    let s = r * 2;", "    let t: u8 = r;", "    let u = nope == 0;", "    if u { }", "}"]
    g.errors("op_poison_unknown_name", L, [g.err(L, 3, "nope", "E0230", "unknown name 'nope'"), g.err(L, 6, "nope", "E0230", "unknown name 'nope'")])
    L = ["manna v0;", "fn f(a: bool, b: bool) {", "    let r = a + b;", "    let s = r + 1;", "    let t: u8 = r;", "    let u = -r;", "    let v = r < s;", "}"]
    g.errors("op_poison_bad_operator_once", L, [g.err(L, 3, "a + b", "E0260", "operator '+' is not defined for bool")])
    L = ["manna v0;", "fn f(a: u8, b: i8) {", "    let r = a + b;", "    let s = r * 2;", "    let t = r == s;", "}"]
    g.errors("op_poison_types_differ_once", L, [g.err(L, 3, "a + b", "E0261", "operands have different types: u8 and i8; convert one explicitly")])
    L = ["manna v0;", "fn f(a: u8, b: i8, c: u8) {", "    let r = (a + b) * c;", "}"]
    g.errors("op_poison_nested", L, [g.err(L, 3, "(a + b)", "E0261", "operands have different types: u8 and i8; convert one explicitly")])
    L = ["manna v0;", "fn f(a: bool, b: u8, c: i8) {", "    let r = a + a;", "    let s = b + c;", "}"]
    g.errors("op_two_independent_errors", L, [g.err(L, 3, "a + a", "E0260", "operator '+' is not defined for bool"),
                                              g.err(L, 4, "b + c", "E0261", "operands have different types: u8 and i8; convert one explicitly")])
    L = ["manna v0;", "fn f(x: u8, b: bool) {", "    let a = !1;", "    let c = !x;", "    let d = 1 && b;", "    let e = x && b;", "    let h = b && 1;", "    let k = !b;", "}"]
    g.errors("op_logical_and_not_forms", L, [
        g.err(L, 3, "1", "E0223", "type mismatch: expected bool, found integer literal"),
        g.err(L, 4, "!x", "E0260", "operator '!' is not defined for u8"),
        g.err(L, 5, "1", "E0223", "type mismatch: expected bool, found integer literal"),
        g.err(L, 6, "x && b", "E0260", "operator '&&' is not defined for u8"),
        g.err(L, 7, "1", "E0223", "type mismatch: expected bool, found integer literal")])
    L = ["manna v0;", "fn f(a: u8, b: u8) {", "    let r: u16 = a + b;", "    let s: bool = a + b;", "    let t: u8 = a < b;", "}"]
    g.errors("op_expected_type_mismatch", L, [g.err(L, 3, "a + b", "E0223", "type mismatch: expected u16, found u8"),
                                              g.err(L, 4, "a + b", "E0223", "type mismatch: expected bool, found u8"),
                                              g.err(L, 5, "a < b", "E0223", "type mismatch: expected u8, found bool")])

    # ---- precedence and conditions (positive) ----
    L = ["manna v0;",
         "fn f(a: u8, b: u8, c: u8, d: u8, p: bool, q: bool, r: bool) {",
         "    let x = a + b * c < d;",
         "    let y = p && q || r;",
         "    let z = a == b && c != d;",
         "}"]
    def lv2(ln, stmt, name, slot, t):
        return g.N("local", inner(L, ln, stmt, name)[0], "%s #%d %s" % (name, slot, t))
    s1 = "let x = a + b * c < d;"
    mul = g.N("binary", inner(L, 3, s1, "b * c")[0], "* u8", lv2(3, s1, "b", 1, "u8"), lv2(3, s1, "c", 2, "u8"))
    add = g.N("binary", inner(L, 3, s1, "a + b * c")[0], "+ u8", lv2(3, s1, "a", 0, "u8"), mul)
    lt = g.N("binary", inner(L, 3, s1, "a + b * c < d")[0], "< bool", add, lv2(3, s1, "d", 3, "u8"))
    s2 = "let y = p && q || r;"
    andn = g.N("binary", inner(L, 4, s2, "p && q")[0], "&& bool", lv2(4, s2, "p", 4, "bool"), lv2(4, s2, "q", 5, "bool"))
    orn = g.N("binary", inner(L, 4, s2, "p && q || r")[0], "|| bool", andn, lv2(4, s2, "r", 6, "bool"))
    s3 = "let z = a == b && c != d;"
    eq = g.N("binary", inner(L, 5, s3, "a == b")[0], "== bool", lv2(5, s3, "a", 0, "u8"), lv2(5, s3, "b", 1, "u8"))
    ne = g.N("binary", inner(L, 5, s3, "c != d")[0], "!= bool", lv2(5, s3, "c", 2, "u8"), lv2(5, s3, "d", 3, "u8"))
    an = g.N("binary", inner(L, 5, s3, "a == b && c != d")[0], "&& bool", eq, ne)
    params = [("a: u8", "a", "u8"), ("b: u8", "b", "u8"), ("c: u8", "c", "u8"), ("d: u8", "d", "u8"), ("p: bool", "p", "bool"), ("q: bool", "q", "bool"), ("r: bool", "r", "bool")]
    g.put("op_precedence_ok", L, g.func(L, 2, 6, "f", params, "", g.body(L, 2, 6,
        g.stmt(L, 3, s1, "let", "x #7 bool", lt), g.stmt(L, 4, s2, "let", "y #8 bool", orn), g.stmt(L, 5, s3, "let", "z #9 bool", an))))

if __name__ == "__main__":
    for op, nm in OPS:
        matrix_for(op, nm)
    unary()
    sibling_and_misc()
