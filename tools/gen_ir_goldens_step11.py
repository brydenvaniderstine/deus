# Generates the typed-IR step 11 goldens (tests/ir/construct_*, field_*: struct construction, field access, field assignment, E0240-E0245, E0251, E0226 as a field value) from a spec whose columns come from the source text, independent of the C builder; run from the repo root.
import gen_ir_goldens_step7 as g
import gen_ir_goldens_step10 as c

plain, ret, call, cl, nm = c.plain, c.ret, c.call, c.cl, c.nm
msg_unit = c.msg_unit

def sdecl(L, first, last, name, size, align, fields):
    """a struct's dump: fields = (line, needle, name, type, offset); the span is first:1 to the end of `last`."""
    out = ["(struct@%d:1-%d:%d %s size=%d align=%d" % (first, last, len(L[last - 1]) + 1, name, size, align)]
    for ln, needle, n, t, off in fields:
        out.append("  (field@%s %s %s +%d)" % (g.sp(L, ln, needle + ","), n, t, off))  # a struct field's span includes its trailing comma
    if len(out) == 1:
        return [out[0] + ")"]
    out[-1] += ")"
    return out

def snew(L, ln, text, sname, *vals):
    return g.N("struct_new", g.sp(L, ln, text), sname, *vals)

def fld(L, ln, text, name, idx, t, base):
    return g.N("field", g.sp(L, ln, text), "%s #%d %s" % (name, idx, t), base)

def assign(L, ln, text, place, value):
    return g.stmt(L, ln, text, "assign", "", place, value)

def let(L, ln, text, name, slot, t, init):
    return g.stmt(L, ln, text, "let", "%s #%d %s" % (name, slot, t), init)

def estmt(L, ln, text, e):
    return g.stmt(L, ln, text, "expr_stmt", "", e)

def mismatch(e, f): return "type mismatch: expected %s, found %s" % (e, f)
def nofield(s, f): return "struct '%s' has no field '%s'" % (s, f)
def missing(f, s): return "missing field '%s' in construction of '%s'" % (f, s)
def nofields(t): return "type %s has no fields" % t
def order(a, b): return "fields must be given in declaration order: '%s' before '%s'" % (a, b)
positional = "struct construction names every field: use 'field: value'"
notname = "left of ':' must be a field name"
twice = lambda f: "field '%s' given twice" % f
unk = lambda n: "unknown name '%s'" % n
notassign = "can't assign to this expression"

if __name__ == "__main__":
    P2 = lambda L, ln=2: sdecl(L, ln, ln, "P", 2, 1, [(ln, "x: u8", "x", "u8", 0), (ln, "y: u8", "y", "u8", 1)])

    # ---------- accepted ----------
    L = ["manna v0;", "struct P { x: u8, y: u8, }", "fn make() -> P {", "    return P(x: 1, y: 2);", "}",
         "fn build(a: u8) -> P {", "    let p = P(x: a, y: 3);", "    return p;", "}"]
    g.put("construct_basic", L, P2(L) +
          plain(L, 3, 5, "make", [], "P", ret(L, 4, "return P(x: 1, y: 2);", snew(L, 4, "P(x: 1, y: 2)", "P", nm(L, 4, "1", "u8"), nm(L, 4, "2", "u8")))) +
          plain(L, 6, 9, "build", [("a: u8", "a", "u8")], "P",
                let(L, 7, "let p = P(x: a, y: 3);", "p", 1, "P", snew(L, 7, "P(x: a, y: 3)", "P", cl(L, 7, "a", 0, "u8"), nm(L, 7, "3", "u8"))),
                ret(L, 8, "return p;", cl(L, 8, "p", 1, "P"))))

    L = ["manna v0;", "struct Empty { }", "fn make() -> Empty {", "    return Empty();", "}"]
    g.put("construct_zero_fields", L, sdecl(L, 2, 2, "Empty", 0, 1, []) +
          plain(L, 3, 5, "make", [], "Empty", ret(L, 4, "return Empty();", snew(L, 4, "Empty()", "Empty"))))

    L = ["manna v0;", "struct S { v: i8, w: u64, }", "fn make() -> S {", "    return S(v: -128, w: 18446744073709551615);", "}"]
    g.put("construct_literals_take_the_field_types", L,
          sdecl(L, 2, 2, "S", 16, 8, [(2, "v: i8", "v", "i8", 0), (2, "w: u64", "w", "u64", 8)]) +
          plain(L, 3, 5, "make", [], "S", ret(L, 4, "return S(v: -128, w: 18446744073709551615);",
                snew(L, 4, "S(v: -128, w: 18446744073709551615)", "S", g.N("int", g.sp(L, 4, "-128"), "-128 i8"), nm(L, 4, "18446744073709551615", "u64")))))

    L = ["manna v0;", "struct P { x: u8, y: u8, }", "fn f(x: bool, y: bool) -> P {", "    return P(x: 1, y: 2);", "}"]
    g.put("construct_field_names_are_not_looked_up_as_locals", L, P2(L) +
          plain(L, 3, 5, "f", [("x: bool", "x", "bool"), ("y: bool", "y", "bool")], "P",
                ret(L, 4, "return P(x: 1, y: 2);", snew(L, 4, "P(x: 1, y: 2)", "P", nm(L, 4, "1", "u8"), nm(L, 4, "2", "u8")))))

    L = ["manna v0;", "struct Inner { a: u8, }", "struct Outer { inner: Inner, b: u8, }", "fn make() -> Outer {",
         "    return Outer(inner: Inner(a: 1), b: 2);", "}", "fn get(o: Outer) -> u8 {", "    return o.inner.a;", "}"]
    g.put("construct_nested_structs", L,
          sdecl(L, 2, 2, "Inner", 1, 1, [(2, "a: u8", "a", "u8", 0)]) +
          sdecl(L, 3, 3, "Outer", 2, 1, [(3, "inner: Inner", "inner", "Inner", 0), (3, "b: u8", "b", "u8", 1)]) +
          plain(L, 4, 6, "make", [], "Outer", ret(L, 5, "return Outer(inner: Inner(a: 1), b: 2);",
                snew(L, 5, "Outer(inner: Inner(a: 1), b: 2)", "Outer", snew(L, 5, "Inner(a: 1)", "Inner", nm(L, 5, "1", "u8")), nm(L, 5, "2", "u8")))) +
          plain(L, 7, 9, "get", [("o: Outer", "o", "Outer")], "u8",
                ret(L, 8, "return o.inner.a;", fld(L, 8, "o.inner.a", "a", 0, "u8", fld(L, 8, "o.inner", "inner", 0, "Inner", cl(L, 8, "o", 0, "Outer"))))))

    L = ["manna v0;", "struct P { x: u8, y: u16, }", "fn get_x(p: P) -> u8 {", "    return p.x;", "}", "fn next_y(p: P) -> u16 {",
         "    let y = p.y;", "    return y + 1;", "}", "fn plus(p: P) -> u8 {", "    return 1 + p.x;", "}"]
    g.put("field_access_basic", L, sdecl(L, 2, 2, "P", 4, 2, [(2, "x: u8", "x", "u8", 0), (2, "y: u16", "y", "u16", 2)]) +
          plain(L, 3, 5, "get_x", [("p: P", "p", "P")], "u8", ret(L, 4, "return p.x;", fld(L, 4, "p.x", "x", 0, "u8", cl(L, 4, "p", 0, "P")))) +
          plain(L, 6, 9, "next_y", [("p: P", "p", "P")], "u16",
                let(L, 7, "let y = p.y;", "y", 1, "u16", fld(L, 7, "p.y", "y", 1, "u16", cl(L, 7, "p", 0, "P"))),
                ret(L, 8, "return y + 1;", g.N("binary", g.sp(L, 8, "y + 1"), "+ u16", cl(L, 8, "y", 1, "u16"), nm(L, 8, "1", "u16")))) +
          plain(L, 10, 12, "plus", [("p: P", "p", "P")], "u8",
                ret(L, 11, "return 1 + p.x;", g.N("binary", g.sp(L, 11, "1 + p.x"), "+ u8", nm(L, 11, "1", "u8"), fld(L, 11, "p.x", "x", 0, "u8", cl(L, 11, "p", 0, "P"))))))

    L = ["manna v0;", "struct P { x: u8, y: u8, }", "fn make(a: u8) -> P {", "    return P(x: a, y: a);", "}", "fn f() -> u8 {",
         "    return make(7).y;", "}", "fn g() -> u8 {", "    return P(x: 1, y: 2).x;", "}"]
    mk = lambda ln: call(L, ln, "make(7)", "P", "make", nm(L, ln, "7", "u8"))
    g.put("field_access_on_a_call_result_and_a_construction", L, P2(L) +
          plain(L, 3, 5, "make", [("a: u8", "a", "u8")], "P", ret(L, 4, "return P(x: a, y: a);",
                snew(L, 4, "P(x: a, y: a)", "P", cl(L, 4, "a", 0, "u8", 0), cl(L, 4, "a", 0, "u8", 1)))) +
          plain(L, 6, 8, "f", [], "u8", ret(L, 7, "return make(7).y;", fld(L, 7, "make(7).y", "y", 1, "u8", mk(7)))) +
          plain(L, 9, 11, "g", [], "u8", ret(L, 10, "return P(x: 1, y: 2).x;",
                fld(L, 10, "P(x: 1, y: 2).x", "x", 0, "u8", snew(L, 10, "P(x: 1, y: 2)", "P", nm(L, 10, "1", "u8"), nm(L, 10, "2", "u8"))))))

    L = ["manna v0;", "struct P { x: u8, y: u8, }", "struct O { p: P, z: bool, }", "fn f(p: P, o: O) {", "    p.x = 1;", "    p.y = p.x;",
         "    o.p.y = 255;", "    o.z = true;", "    o.p = p;", "}"]
    pl = lambda ln, nme: cl(L, ln, nme, 0 if nme == "p" else 1, "P" if nme == "p" else "O")
    g.put("field_assignment", L, P2(L) + sdecl(L, 3, 3, "O", 3, 1, [(3, "p: P", "p", "P", 0), (3, "z: bool", "z", "bool", 2)]) +
          plain(L, 4, 10, "f", [("p: P", "p", "P"), ("o: O", "o", "O")], "",
                assign(L, 5, "p.x = 1;", fld(L, 5, "p.x", "x", 0, "u8", pl(5, "p")), nm(L, 5, "1", "u8")),
                assign(L, 6, "p.y = p.x;", fld(L, 6, "p.y", "y", 1, "u8", pl(6, "p")), fld(L, 6, "p.x", "x", 0, "u8", cl(L, 6, "p", 0, "P", 1))),
                assign(L, 7, "o.p.y = 255;", fld(L, 7, "o.p.y", "y", 1, "u8", fld(L, 7, "o.p", "p", 0, "P", pl(7, "o"))), nm(L, 7, "255", "u8")),
                assign(L, 8, "o.z = true;", fld(L, 8, "o.z", "z", 1, "bool", pl(8, "o")), g.boolean(L, 8, "true")),
                assign(L, 9, "o.p = p;", fld(L, 9, "o.p", "p", 0, "P", pl(9, "o")), cl(L, 9, "p", 0, "P", 1))))

    L = ["manna v0;", "struct B { a: [2]u8, }", "fn f(v: [2]u8) -> B {", "    return B(a: v);", "}"]
    g.put("construct_with_an_array_field", L, sdecl(L, 2, 2, "B", 2, 1, [(2, "a: [2]u8", "a", "[2]u8", 0)]) +
          plain(L, 3, 5, "f", [("v: [2]u8", "v", "[2]u8")], "B", ret(L, 4, "return B(a: v);", snew(L, 4, "B(a: v)", "B", cl(L, 4, "v", 0, "[2]u8")))))

    L = ["manna v0;", "struct P { x: u8, y: u8, }", "fn take(p: P) -> u8 {", "    return p.y;", "}", "fn f() -> u8 {", "    return take(P(x: 1, y: 2));", "}"]
    g.put("construct_as_an_argument", L, P2(L) +
          plain(L, 3, 5, "take", [("p: P", "p", "P")], "u8", ret(L, 4, "return p.y;", fld(L, 4, "p.y", "y", 1, "u8", cl(L, 4, "p", 0, "P")))) +
          plain(L, 6, 8, "f", [], "u8", ret(L, 7, "return take(P(x: 1, y: 2));", call(L, 7, "take(P(x: 1, y: 2))", "u8", "take",
                snew(L, 7, "P(x: 1, y: 2)", "P", nm(L, 7, "1", "u8"), nm(L, 7, "2", "u8"))))))

    # ---------- E0240 ----------
    L = ["manna v0;", "struct P { x: u8, y: u8, }", "fn f() {", "    let a = P(1, 2);", "    let b = P(x: 1, 2);", "    let c = P(1, y: 2);",
         "    let d = P(nope, y: nope2);", "    let e = a;", "}"]
    g.errors("construct_positional_arguments", L, [g.err(L, 4, "1", "E0240", positional), g.err(L, 5, "2", "E0240", positional),
                                                    g.err(L, 6, "1", "E0240", positional), g.err(L, 7, "nope", "E0240", positional),
                                                    g.err(L, 7, "nope", "E0230", unk("nope")), g.err(L, 7, "nope2", "E0230", unk("nope2"))])
    # ---------- E0241 ----------
    L = ["manna v0;", "struct P { x: u8, y: u8, }", "fn f() {", "    let a = P((x): 1, y: 2);", "    let b = P(1 + 1: 1, y: 2);", "    let c = P(a.z: 1, y: nope);", "}"]
    g.errors("construct_left_of_the_colon_must_be_a_field_name", L, [g.err(L, 4, "(x)", "E0241", notname), g.err(L, 5, "1 + 1", "E0241", notname),
                                                                     g.err(L, 6, "a.z", "E0241", notname), g.err(L, 6, "nope", "E0230", unk("nope"))])
    # ---------- E0242 ----------
    L = ["manna v0;", "struct P { x: u8, y: u8, }", "struct E { }", "fn f(p: P) {", "    let a = P(x: 1, z: 2, y: 3);", "    let b = P(x: 1, yy: 2);",
         "    let c = p.z;", "    let d = E().q;", "    let e = E(q: nope);", "}"]
    g.errors("field_unknown_in_a_construction_and_in_an_access", L, [g.err(L, 5, "z", "E0242", nofield("P", "z")), g.err(L, 6, "yy", "E0242", nofield("P", "yy")),
                                                                     g.err(L, 7, "z", "E0242", nofield("P", "z")), g.err(L, 8, "q", "E0242", nofield("E", "q")),
                                                                     g.err(L, 9, "q", "E0242", nofield("E", "q")), g.err(L, 9, "nope", "E0230", unk("nope"))])
    # ---------- E0243 ----------
    L = ["manna v0;", "struct P { x: u8, y: u8, z: u8, }", "fn f() {", "    let a = P(x: 1);", "    let b = P(y: 2);", "    let c = P();", "}"]
    g.errors("construct_missing_fields", L, [g.err(L, 4, "P(x: 1)", "E0243", missing("y", "P")), g.err(L, 4, "P(x: 1)", "E0243", missing("z", "P")),
                                              g.err(L, 5, "P(y: 2)", "E0243", missing("x", "P")), g.err(L, 5, "P(y: 2)", "E0243", missing("z", "P")),
                                              g.err(L, 6, "P()", "E0243", missing("x", "P")), g.err(L, 6, "P()", "E0243", missing("y", "P")), g.err(L, 6, "P()", "E0243", missing("z", "P"))])
    L = ["manna v0;", "struct P { x: u8, y: u8, }", "fn f() {", "    let a = P(x: 1, yy: 2);", "    let b = P(1, 2);", "    let c = P((x): 1);", "}"]
    g.errors("construct_an_unattributable_argument_does_not_also_report_a_missing_field", L, [
        g.err(L, 4, "yy", "E0242", nofield("P", "yy")), g.err(L, 5, "1", "E0240", positional), g.err(L, 6, "(x)", "E0241", notname)])
    # ---------- E0244 ----------
    L = ["manna v0;", "struct P { x: u8, y: u8, }", "fn f() {", "    let a = P(x: 1, x: 2, y: 3);", "    let b = P(x: 1, y: 2, x: 3);",
         "    let c = P(x: 1, x: nope, y: 2);", "    let d = P(x: 1, x: 2);", "}"]
    g.errors("construct_field_given_twice", L, [g.err(L, 4, "x", "E0244", twice("x"), 1), g.err(L, 5, "x", "E0244", twice("x"), 1),
                                                g.err(L, 6, "x", "E0244", twice("x"), 1), g.err(L, 6, "nope", "E0230", unk("nope")),
                                                g.err(L, 7, "x", "E0244", twice("x"), 1), g.err(L, 7, "P(x: 1, x: 2)", "E0243", missing("y", "P"))])
    # ---------- E0245 ----------
    L = ["manna v0;", "struct P { x: u8, y: u8, z: u8, }", "fn f() {", "    let a = P(y: 1, x: 2, z: 3);", "    let b = P(z: 1, y: 2, x: 3);",
         "    let c = P(y: 300, x: 2, z: 3);", "    let d = P(z: 1, x: 2);", "    let e = P(y: 1, x: true, z: 3);", "}"]
    g.errors("construct_fields_out_of_order", L, [
        g.err(L, 4, "x", "E0245", order("x", "y")), g.err(L, 5, "y", "E0245", order("y", "z")),
        g.err(L, 6, "300", "E0224", "integer literal 300 does not fit in u8"), g.err(L, 6, "x", "E0245", order("x", "y")),
        g.err(L, 7, "x", "E0245", order("x", "z")), g.err(L, 7, "P(z: 1, x: 2)", "E0243", missing("y", "P")),
        g.err(L, 8, "x", "E0245", order("x", "y")), g.err(L, 8, "true", "E0223", mismatch("u8", "bool"))])
    # ---------- value types ----------
    L = ["manna v0;", "struct P { x: u8, ok: bool, }", "struct Q { x: u8, ok: bool, }", "fn f(n: u16) {", "    let a = P(x: 256, ok: true);", "    let b = P(x: true, ok: 1);",
         "    let c = P(x: n, ok: false);", "    let d: P = Q(x: 1, ok: true);", "    let e = P(x: nope, ok: true);", "    let f = e;", "}"]
    g.errors("construct_value_types", L, [
        g.err(L, 5, "256", "E0224", "integer literal 256 does not fit in u8"), g.err(L, 6, "true", "E0223", mismatch("u8", "bool")),
        g.err(L, 6, "1", "E0223", mismatch("bool", "integer literal")), g.err(L, 7, "n", "E0223", mismatch("u8", "u16")),
        g.err(L, 8, "Q(x: 1, ok: true)", "E0223", mismatch("P", "Q")), g.err(L, 9, "nope", "E0230", unk("nope"))])
    # ---------- E0251 ----------
    L = ["manna v0;", "fn one() -> u8 {", "    return 1;", "}", "fn f(n: u8, b: bool) {", "    let x = n.v;", "    let y = b.v;", "    let z = (n + 1).v;",
         "    let w = one().v;", "    let u = n.v.v;", "}"]
    g.errors("field_access_on_a_type_without_fields", L, [
        g.err(L, 6, "n", "E0251", nofields("u8")), g.err(L, 7, "b", "E0251", nofields("bool")), g.err(L, 8, "(n + 1)", "E0251", nofields("u8")),
        g.err(L, 9, "one()", "E0251", nofields("u8")), g.err(L, 10, "n", "E0251", nofields("u8"))])
    # ---------- E0225 / E0226 ----------
    L = ["manna v0;", "fn f() {", "    let a = (1).x;", "    let b = (1 + 2).x;", "}"]
    g.errors("field_access_on_an_integer_literal", L, [
        g.err(L, 3, "(1)", "E0225", "cannot infer the type of this integer literal; annotate it"),
        g.err(L, 4, "(1 + 2)", "E0225", "cannot infer the type of this integer literal; annotate it")])
    L = ["manna v0;", "fn nothing() {", "}", "struct P { x: u8, }", "fn f() {", "    let a = P(x: nothing());", "    let b = nothing().x;", "    let c = P(zz: nothing());", "}"]
    g.errors("unit_value_as_a_field_value_and_as_a_field_base", L, [
        g.err(L, 6, "nothing()", "E0226", msg_unit), g.err(L, 7, "nothing()", "E0226", msg_unit), g.err(L, 8, "zz", "E0242", nofield("P", "zz"))])
    # ---------- field assignment ----------
    L = ["manna v0;", "fn nothing() {", "}", "struct P { x: u8, ok: bool, }", "fn f(p: P, n: u8) {", "    p.zz = nope;", "    p.x = true;", "    n.x = 1;",
         "    p.ok = 1;", "    p.x = nothing();", "    p.x = 300;", "}"]
    g.errors("field_assignment_errors", L, [
        g.err(L, 6, "zz", "E0242", nofield("P", "zz")), g.err(L, 6, "nope", "E0230", unk("nope")), g.err(L, 7, "true", "E0223", mismatch("u8", "bool")),
        g.err(L, 8, "n", "E0251", nofields("u8")), g.err(L, 9, "1", "E0223", mismatch("bool", "integer literal")),
        g.err(L, 10, "nothing()", "E0226", msg_unit), g.err(L, 11, "300", "E0224", "integer literal 300 does not fit in u8")])
    L = ["manna v0;", "struct P { x: u8, y: u8, }", "fn make() -> P {", "    return P(x: 1, y: 2);", "}", "fn f(p: P) {", "    make().x = 1;", "    P(x: 1, y: 2).y = 2;",
         "    (p).x = 3;", "    p.x.y = nope;", "}"]
    g.errors("field_assignment_target_must_be_rooted_in_a_name", L, [
        g.err(L, 7, "make().x", "E0270", notassign), g.err(L, 8, "P(x: 1, y: 2).y", "E0270", notassign), g.err(L, 9, "(p).x", "E0270", notassign),
        g.err(L, 10, "p.x", "E0251", nofields("u8")), g.err(L, 10, "nope", "E0230", unk("nope"))])
    # ---------- operators on structs ----------
    L = ["manna v0;", "struct P { x: u8, }", "fn f(p: P, q: P) -> bool {", "    return p == q;", "}"]
    g.errors("construct_structs_have_no_operators", L, [g.err(L, 4, "p == q", "E0260", "operator '==' is not defined for P")])
