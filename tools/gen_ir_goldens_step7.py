# Generates the typed-IR step 7 goldens (tests/ir/body_*.mn/.ir: names, let, assignment, if, scopes) from a spec whose columns come from the source text, independent of the C builder; run from the repo root.
import os
OUT = "tests/ir"

# ---- positions, computed from the source lines ----
import re

def col(lines, ln, needle, occ=0):
    """1-based column of the occ-th match of `needle` on the line; an identifier-like needle
    matches whole words only (so `a` does not match inside `let`)."""
    line = lines[ln - 1]
    pat = r"(?<![A-Za-z0-9_])" + re.escape(needle) + r"(?![A-Za-z0-9_])" if re.fullmatch(r"[A-Za-z0-9_]+", needle) else re.escape(needle)
    ms = list(re.finditer(pat, line))
    return ms[occ].start() + 1

def sp(lines, ln, needle, occ=0):
    c = col(lines, ln, needle, occ)
    return "%d:%d-%d:%d" % (ln, c, ln, c + len(needle))

def span2(l1, c1, l2, c2):
    return "%d:%d-%d:%d" % (l1, c1, l2, c2)

def err(lines, ln, needle, code, msg, occ=0):
    return "%d:%d %s %s" % (ln, col(lines, ln, needle, occ), code, msg)

def put(name, lines, expected):
    with open(os.path.join(OUT, name + ".mn"), "w") as f:
        f.write("\n".join(lines) + "\n")
    with open(os.path.join(OUT, name + ".ir"), "w") as f:
        f.write(("\n".join(expected) + "\n") if expected else "")

def errors(name, lines, errs):
    put(name, lines, ["ERRORS:"] + errs)

# ---- S-expression nodes: (tag, span, header, children) ----
def N(tag, span, header, *children):
    return (tag, span, header, list(children))

def render(node, depth):
    tag, span, header, kids = node
    out = ["  " * depth + "(%s@%s%s" % (tag, span, (" " + header) if header else "")]
    for k in kids:
        out += render(k, depth + 1)
    out[-1] += ")"
    return out

def local(L, ln, name, slot, t, occ=0):
    return N("local", sp(L, ln, name, occ), "%s #%d %s" % (name, slot, t))

def num(L, ln, text, t, occ=0):
    return N("int", sp(L, ln, text, occ), "%s %s" % (text, t))

def boolean(L, ln, text, occ=0):
    return N("bool", sp(L, ln, text, occ), "%s bool" % text)

def stmt(L, ln, text, tag, header, *kids):
    return N(tag, sp(L, ln, text), header, *kids)

def block(span, *stmts):
    return N("block", span, "", *stmts)

def func(L, first, last, name, params, ret, blk):
    """params: (needle, name, type) triples found on line `first`; blk: the body block node."""
    end = len(L[last - 1]) + 1
    p = " ".join("(param@%s %s #%d %s)" % (sp(L, first, nd), n, i, t) for i, (nd, n, t) in enumerate(params))
    out = ["(fn@%d:1-%d:%d fn:%s" % (first, last, end, name),
           "  (params%s)" % ((" " + p) if p else ""),
           "  (returns%s)" % ((" " + ret) if ret else "")]
    out += render(blk, 1)
    out[-1] += ")"
    return out

def body(L, first, last, *stmts):
    """the function body block: from the last `{` of the header line to the closing line's `}`."""
    c = L[first - 1].rindex("{") + 1
    return block(span2(first, c, last, len(L[last - 1]) + 1), *stmts)

if __name__ == "__main__":
    # ---------- 7a: names, let, expression statements ----------
    L = ["manna v0;",
         "fn f(a: u8) {",
         "    let b = a;",
         "    let c: u8 = 7;",
         "    let d = true;",
         "    c;",
         "}"]
    put("body_let_basic", L, func(L, 2, 7, "f", [("a: u8", "a", "u8")], "", body(L, 2, 7,
        stmt(L, 3, "let b = a;", "let", "b #1 u8", local(L, 3, "a", 0, "u8")),
        stmt(L, 4, "let c: u8 = 7;", "let", "c #2 u8", num(L, 4, "7", "u8")),
        stmt(L, 5, "let d = true;", "let", "d #3 bool", boolean(L, 5, "true")),
        stmt(L, 6, "c;", "expr_stmt", "", local(L, 6, "c", 2, "u8")))))

    L = ["manna v0;",
         "struct P { x: u8, }",
         "fn f(p: P, a: [2]u8, c: bool) -> bool {",
         "    let q = p;",
         "    let r = a;",
         "    let s = c;",
         "    return s;",
         "}"]
    sdump = ["(struct@2:1-2:20 P size=1 align=1", "  (field@2:12-2:18 x u8 +0))"]
    put("body_let_infers_from_a_name", L, sdump + func(L, 3, 8, "f",
        [("p: P", "p", "P"), ("a: [2]u8", "a", "[2]u8"), ("c: bool", "c", "bool")], "bool", body(L, 3, 8,
        stmt(L, 4, "let q = p;", "let", "q #3 P", local(L, 4, "p", 0, "P")),
        stmt(L, 5, "let r = a;", "let", "r #4 [2]u8", local(L, 5, "a", 1, "[2]u8")),
        stmt(L, 6, "let s = c;", "let", "s #5 bool", local(L, 6, "c", 2, "bool")),
        stmt(L, 7, "return s;", "return", "", local(L, 7, "s", 5, "bool")))))

    L = ["manna v0;",
         "fn f() { }",
         "fn g() {",
         "    let f = true;",
         "    f;",
         "}"]
    put("body_local_may_share_a_function_name", L,
        func(L, 2, 2, "f", [], "", body(L, 2, 2)) +
        func(L, 3, 6, "g", [], "", body(L, 3, 6,
            stmt(L, 4, "let f = true;", "let", "f #0 bool", boolean(L, 4, "true")),
            stmt(L, 5, "f;", "expr_stmt", "", local(L, 5, "f", 0, "bool")))))

    L = ["manna v0;",
         "fn f(a: u8, b: bool) {",
         "    a = 1;",
         "    a = a;",
         "    b = true;",
         "    let c: u8 = 0;",
         "    c = a;",
         "}"]
    put("body_assign_basic", L, func(L, 2, 8, "f", [("a: u8", "a", "u8"), ("b: bool", "b", "bool")], "", body(L, 2, 8,
        stmt(L, 3, "a = 1;", "assign", "", local(L, 3, "a", 0, "u8"), num(L, 3, "1", "u8")),
        stmt(L, 4, "a = a;", "assign", "", local(L, 4, "a", 0, "u8"), local(L, 4, "a", 0, "u8", occ=1)),
        stmt(L, 5, "b = true;", "assign", "", local(L, 5, "b", 1, "bool"), boolean(L, 5, "true")),
        stmt(L, 6, "let c: u8 = 0;", "let", "c #2 u8", num(L, 6, "0", "u8")),
        stmt(L, 7, "c = a;", "assign", "", local(L, 7, "c", 2, "u8"), local(L, 7, "a", 0, "u8")))))

    # ---------- 7a errors ----------
    L = ["manna v0;",
         "fn a() { let y = x; }",
         "fn b() { x; }",
         "fn c() -> u8 { return x; }"]
    errors("body_unknown_name", L, [err(L, 2, "x", "E0230", "unknown name 'x'"),
                                    err(L, 3, "x", "E0230", "unknown name 'x'"),
                                    err(L, 4, "x", "E0230", "unknown name 'x'")])
    L = ["manna v0;",
         "fn f() {",
         "    let a: u8 = b;",
         "    let b: u8 = 1;",
         "    let c: u8 = b;",
         "}"]
    errors("body_use_before_let", L, [err(L, 3, "b", "E0230", "unknown name 'b'")])
    L = ["manna v0;",
         "fn f() {",
         "    let x: u32 = x;",
         "}"]
    errors("body_not_visible_in_own_initializer", L, [err(L, 3, "x", "E0230", "unknown name 'x'", occ=1)])
    L = ["manna v0;",
         "fn g() { }",
         "struct P { }",
         "fn f() {",
         "    let a = g;",
         "    let b = P;",
         "    g;",
         "}"]
    errors("body_function_and_struct_names_are_not_values", L,
           [err(L, 5, "g", "E0231", "'g' is a function, not a value"),
            err(L, 6, "P", "E0231", "'P' is a struct, not a value"),
            err(L, 7, "g", "E0231", "'g' is a function, not a value")])
    L = ["manna v0;",
         "fn f() {",
         "    let a = nope;",
         "    let b = a;",
         "    let c: u8 = a;",
         "    a;",
         "    b;",
         "    c;",
         "    a = 1;",
         "    b = true;",
         "}"]
    errors("body_unknown_name_is_one_error", L, [err(L, 3, "nope", "E0230", "unknown name 'nope'")])
    cant = "cannot infer the type of this integer literal; annotate it"
    L = ["manna v0;",
         "fn f() {",
         "    let i = 0;",
         "    let n = -1;",
         "    let s = 1 + 2;",
         "    5;",
         "}"]
    errors("body_cannot_infer_a_literal", L, [err(L, 3, "0", "E0225", cant), err(L, 4, "-1", "E0225", cant),
                                              err(L, 5, "1 + 2", "E0225", cant), err(L, 6, "5", "E0225", cant)])
    L = ["manna v0;",
         "fn f() {",
         "    let i = 0;",
         "    let j: u8 = i;",
         "    i;",
         "    let k = i;",
         "}"]
    errors("body_cannot_infer_is_one_error", L, [err(L, 3, "0", "E0225", cant)])
    L = ["manna v0;",
         "fn f() {",
         "    let x: Nope = 1;",
         "    x;",
         "    let y: [][]u8 = 1;",
         "    y;",
         "}"]
    errors("body_bad_annotation_is_one_error", L, [err(L, 3, "Nope", "E0220", "unknown type 'Nope'"),
                                                    err(L, 5, "[]u8", "E0227", "a slice's element type can't contain a slice type")])
    L = ["manna v0;",
         "fn f(a: bool) {",
         "    let x: u8 = true;",
         "    let b: bool = 1;",
         "    let c: u8 = a;",
         "    let d: u8 = 256;",
         "}"]
    errors("body_let_mismatch", L, [err(L, 3, "true", "E0223", "type mismatch: expected u8, found bool"),
                                     err(L, 4, "1", "E0223", "type mismatch: expected bool, found integer literal"),
                                     err(L, 5, "a", "E0223", "type mismatch: expected u8, found bool"),
                                     err(L, 6, "256", "E0224", "integer literal 256 does not fit in u8")])
    already = "'x' is already declared in this function"
    L = ["manna v0;", "fn f() {", "    let x: u8 = 1;", "    let x: u8 = 2;", "}"]
    errors("body_redeclare_same_block", L, [err(L, 4, "x", "E0213", already)])
    L = ["manna v0;", "fn f(x: u8) {", "    let x: u8 = 1;", "}"]
    errors("body_redeclare_parameter", L, [err(L, 3, "x", "E0213", already)])
    L = ["manna v0;", "fn f() {", "    let x: u8 = 1;", "    let x: bool = true;", "    let y: u8 = x;", "}"]
    errors("body_redeclare_keeps_the_first_binding", L, [err(L, 4, "x", "E0213", already)])
    L = ["manna v0;", "fn f() {", "    let x: u8 = 1;", "    let x: u8 = nope;", "}"]
    errors("body_redeclare_still_checks_the_initializer", L,
           [err(L, 4, "x", "E0213", already), err(L, 4, "nope", "E0230", "unknown name 'nope'")])

    # ---------- 7b: assignment ----------
    L = ["manna v0;",
         "fn f(a: u8, b: bool) {",
         "    a = true;",
         "    b = 1;",
         "    a = b;",
         "    a = 300;",
         "}"]
    errors("body_assign_mismatch", L, [err(L, 3, "true", "E0223", "type mismatch: expected u8, found bool"),
                                       err(L, 4, "1", "E0223", "type mismatch: expected bool, found integer literal"),
                                       err(L, 5, "b", "E0223", "type mismatch: expected u8, found bool"),
                                       err(L, 6, "300", "E0224", "integer literal 300 does not fit in u8")])
    L = ["manna v0;", "fn f() {", "    y = 1;", "    y = z;", "}"]
    errors("body_assign_unknown_target", L, [err(L, 3, "y", "E0230", "unknown name 'y'"),
                                              err(L, 4, "y", "E0230", "unknown name 'y'"),
                                              err(L, 4, "z", "E0230", "unknown name 'z'")])
    L = ["manna v0;", "fn g() { }", "struct P { }", "fn f() {", "    g = 1;", "    P = 1;", "}"]
    errors("body_assign_to_function_or_struct_name", L, [err(L, 5, "g", "E0231", "'g' is a function, not a value"),
                                                           err(L, 6, "P", "E0231", "'P' is a struct, not a value")])
    msg = "can't assign to this expression"
    L = ["manna v0;",
         "struct P { x: u8, }",
         "fn g() { }",
         "fn f(x: u8, a: u8, b: u8, p: P) {",
         "    1 = 2;",
         "    g(x) = 3;",
         "    u8(x) = 1;",
         "    [1, 2][0] = 3;",
         "    (a + b) = 1;",
         "    P(x: 1) = p;",
         "    (a) = 1;",
         "    a[0..2] = 1;",
         "    g(x).y = 1;",
         "    (p).x = 1;",
         "}"]
    errors("body_assign_not_an_lvalue", L, [err(L, 5, "1", "E0270", msg), err(L, 6, "g(x)", "E0270", msg),
                                               err(L, 7, "u8(x)", "E0270", msg), err(L, 8, "[1, 2][0]", "E0270", msg),
                                               err(L, 9, "(a + b)", "E0270", msg), err(L, 10, "P(x: 1)", "E0270", msg),
                                               err(L, 11, "(a)", "E0270", msg), err(L, 12, "a[0..2]", "E0270", msg),
                                               err(L, 13, "g(x).y", "E0270", msg), err(L, 14, "(p).x", "E0270", msg)])

    # ---------- 7c: if and block scopes ----------
    def blk_span(L, open_ln, close_ln, open_occ_last=True, close_col=None):
        c1 = L[open_ln - 1].rindex("{") + 1
        c2 = (L[close_ln - 1].index("}") + 2) if close_col is None else close_col
        return span2(open_ln, c1, close_ln, c2)

    L = ["manna v0;",
         "fn f(c: bool) {",
         "    if c {",
         "        let a: u8 = 1;",
         "    } else if c {",
         "        let a: u8 = 2;",
         "    } else {",
         "        let a: u8 = 3;",
         "    }",
         "}"]
    # then block 3..5 ends at the `}` of line 5; the else-if is a nested if from `if` on line 5 to the final `}` on 9
    inner_if = N("if", span2(5, L[4].index("if") + 1, 9, 6), "",
                 local(L, 5, "c", 0, "bool"),
                 block(blk_span(L, 5, 7), stmt(L, 6, "let a: u8 = 2;", "let", "a #2 u8", num(L, 6, "2", "u8"))),
                 block(blk_span(L, 7, 9), stmt(L, 8, "let a: u8 = 3;", "let", "a #3 u8", num(L, 8, "3", "u8"))))
    outer_if = N("if", span2(3, 5, 9, 6), "",
                 local(L, 3, "c", 0, "bool"),
                 block(blk_span(L, 3, 5), stmt(L, 4, "let a: u8 = 1;", "let", "a #1 u8", num(L, 4, "1", "u8"))),
                 inner_if)
    put("body_if_else_chain_reuses_a_name", L, func(L, 2, 10, "f", [("c: bool", "c", "bool")], "", body(L, 2, 10, outer_if)))

    L = ["manna v0;",
         "fn f(c: bool) {",
         "    if c {",
         "        let y: u8 = 1;",
         "        y;",
         "    }",
         "    if c {",
         "        let y: u8 = 2;",
         "        y;",
         "    }",
         "}"]
    put("body_sibling_blocks_reuse_a_name_with_their_own_slots", L, func(L, 2, 11, "f", [("c: bool", "c", "bool")], "", body(L, 2, 11,
        N("if", span2(3, 5, 6, 6), "", local(L, 3, "c", 0, "bool"),
          block(blk_span(L, 3, 6), stmt(L, 4, "let y: u8 = 1;", "let", "y #1 u8", num(L, 4, "1", "u8")),
                stmt(L, 5, "y;", "expr_stmt", "", local(L, 5, "y", 1, "u8")))),
        N("if", span2(7, 5, 10, 6), "", local(L, 7, "c", 0, "bool"),
          block(blk_span(L, 7, 10), stmt(L, 8, "let y: u8 = 2;", "let", "y #2 u8", num(L, 8, "2", "u8")),
                stmt(L, 9, "y;", "expr_stmt", "", local(L, 9, "y", 2, "u8")))))))

    L = ["manna v0;",
         "fn f(c: bool) {",
         "    let x: u8 = 1;",
         "    if c {",
         "        let y: u8 = x;",
         "        if c {",
         "            let z: u8 = y;",
         "        }",
         "    }",
         "}"]
    put("body_outer_names_are_visible_inside_blocks", L, func(L, 2, 10, "f", [("c: bool", "c", "bool")], "", body(L, 2, 10,
        stmt(L, 3, "let x: u8 = 1;", "let", "x #1 u8", num(L, 3, "1", "u8")),
        N("if", span2(4, 5, 9, 6), "", local(L, 4, "c", 0, "bool"),
          block(blk_span(L, 4, 9),
                stmt(L, 5, "let y: u8 = x;", "let", "y #2 u8", local(L, 5, "x", 1, "u8", occ=0)),
                N("if", span2(6, 9, 8, 10), "", local(L, 6, "c", 0, "bool"),
                  block(blk_span(L, 6, 8), stmt(L, 7, "let z: u8 = y;", "let", "z #3 u8", local(L, 7, "y", 2, "u8")))))))))

    L = ["manna v0;", "fn f(c: bool) {", "    let x: u8 = 1;", "    if c {", "        let x: u8 = 2;", "    }", "}"]
    errors("body_redeclare_in_inner_block", L, [err(L, 5, "x", "E0213", already)])
    L = ["manna v0;", "fn f(x: u8, c: bool) {", "    if c {", "        let x: u8 = 1;", "    }", "}"]
    errors("body_redeclare_parameter_in_inner_block", L, [err(L, 4, "x", "E0213", already)])
    L = ["manna v0;", "fn f(c: bool) {", "    if c {", "        let y: u8 = 1;", "    }", "    y;", "}"]
    errors("body_name_is_gone_after_its_block", L, [err(L, 6, "y", "E0230", "unknown name 'y'")])
    L = ["manna v0;", "fn f(c: bool) {", "    if c {", "        let y: u8 = 1;", "    } else {", "        y;", "    }", "}"]
    errors("body_then_names_are_not_visible_in_else", L, [err(L, 6, "y", "E0230", "unknown name 'y'")])
    L = ["manna v0;", "fn f(a: u8, c: bool) {", "    if 1 { }", "    if a { }", "    if nope { }", "    if c { }", "}"]
    errors("body_condition_must_be_bool", L, [err(L, 3, "1", "E0223", "type mismatch: expected bool, found integer literal"),
                                               err(L, 4, "a", "E0223", "type mismatch: expected bool, found u8"),
                                               err(L, 5, "nope", "E0230", "unknown name 'nope'")])
    L = ["manna v0;", "fn f() {", "    if true {", "        if false { }", "    }", "}"]
    put("body_condition_may_be_a_bool_literal", L, func(L, 2, 6, "f", [], "", body(L, 2, 6,
        N("if", span2(3, 5, 5, 6), "", boolean(L, 3, "true"),
          block(blk_span(L, 3, 5), N("if", span2(4, 9, 4, 21), "", boolean(L, 4, "false"), block(blk_span(L, 4, 4, close_col=21))))))))
