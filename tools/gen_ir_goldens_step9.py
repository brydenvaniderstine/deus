# Generates the typed-IR step 9 goldens (tests/ir/ret_*: return shape E0276 and the structural missing-return analysis E0277) from a spec whose columns come from the source text, independent of the C builder; run from the repo root.
import gen_ir_goldens_step7 as g

def blk(L, open_ln, close_ln, *stmts, close_occ=0):
    """a block from the last `{` on open_ln to the (close_occ-th) `}` on close_ln."""
    c1 = L[open_ln - 1].rindex("{") + 1
    pos = -1
    for _ in range(close_occ + 1):
        pos = L[close_ln - 1].index("}", pos + 1)
    return g.block(g.span2(open_ln, c1, close_ln, pos + 2), *stmts)

def ret(L, ln, text, val=None, t="u8", occ=0):
    kids = [g.num(L, ln, val, t, occ)] if val is not None else []
    return g.stmt(L, ln, text, "return", "", *kids)

def iff(L, l1, l2, cond, slot, then_blk, else_node=None, start_needle="if", start_occ=0):
    c1 = g.col(L, l1, start_needle, start_occ)
    c2 = L[l2 - 1].rindex("}") + 2
    kids = [g.local(L, l1, cond, slot, "bool"), then_blk] + ([else_node] if else_node is not None else [])
    return g.N("if", g.span2(l1, c1, l2, c2), "", *kids)

def fnd(L, first, last, name, params, ret_t, body_stmts):
    return g.func(L, first, last, name, params, ret_t, g.body(L, first, last, *body_stmts))

msg_missing = lambda f: "function '%s' may end without returning a value" % f

if __name__ == "__main__":
    # ---------- accepted shapes ----------
    L = ["manna v0;", "fn f() -> u8 {", "    return 1;", "}"]
    g.put("ret_ok_ends_in_return", L, fnd(L, 2, 4, "f", [], "u8", [ret(L, 3, "return 1;", "1")]))

    L = ["manna v0;", "fn f(c: bool) -> u8 {", "    if c {", "        return 1;", "    } else {", "        return 2;", "    }", "}"]
    g.put("ret_ok_if_else_both_return", L, fnd(L, 2, 8, "f", [("c: bool", "c", "bool")], "u8", [
        iff(L, 3, 7, "c", 0, blk(L, 3, 5, ret(L, 4, "return 1;", "1")), blk(L, 5, 7, ret(L, 6, "return 2;", "2")))]))

    L = ["manna v0;", "fn f(c: bool, d: bool) -> u8 {", "    if c {", "        return 1;", "    } else if d {", "        return 2;", "    } else {", "        return 3;", "    }", "}"]
    inner = iff(L, 5, 9, "d", 1, blk(L, 5, 7, ret(L, 6, "return 2;", "2")), blk(L, 7, 9, ret(L, 8, "return 3;", "3")), start_needle="if")
    g.put("ret_ok_else_if_chain_with_final_else", L, fnd(L, 2, 10, "f", [("c: bool", "c", "bool"), ("d: bool", "d", "bool")], "u8", [
        iff(L, 3, 9, "c", 0, blk(L, 3, 5, ret(L, 4, "return 1;", "1")), inner)]))

    L = ["manna v0;", "fn f(c: bool, d: bool) -> u8 {", "    if c {", "        if d {", "            return 1;", "        } else {", "            return 2;", "        }",
         "    } else {", "        return 3;", "    }", "}"]
    inner = iff(L, 4, 8, "d", 1, blk(L, 4, 6, ret(L, 5, "return 1;", "1")), blk(L, 6, 8, ret(L, 7, "return 2;", "2")))
    g.put("ret_ok_nested_if_else", L, fnd(L, 2, 12, "f", [("c: bool", "c", "bool"), ("d: bool", "d", "bool")], "u8", [
        iff(L, 3, 11, "c", 0, blk(L, 3, 9, inner), blk(L, 9, 11, ret(L, 10, "return 3;", "3")))]))

    L = ["manna v0;", "fn f() -> u8 {", "    return 1;", "    let x: u8 = 2;", "}"]
    g.put("ret_ok_code_after_return", L, fnd(L, 2, 5, "f", [], "u8", [
        ret(L, 3, "return 1;", "1"), g.stmt(L, 4, "let x: u8 = 2;", "let", "x #0 u8", g.num(L, 4, "2", "u8"))]))

    L = ["manna v0;", "fn f(c: bool) -> u8 {", "    if c {", "        return 1;", "    } else {", "        return 2;", "    }", "    let y: u8 = 3;", "}"]
    g.put("ret_ok_code_after_if_else_that_returns", L, fnd(L, 2, 9, "f", [("c: bool", "c", "bool")], "u8", [
        iff(L, 3, 7, "c", 0, blk(L, 3, 5, ret(L, 4, "return 1;", "1")), blk(L, 5, 7, ret(L, 6, "return 2;", "2"))),
        g.stmt(L, 8, "let y: u8 = 3;", "let", "y #1 u8", g.num(L, 8, "3", "u8"))]))

    L = ["manna v0;", "fn f(c: bool) -> u8 {", "    while c {", "        return 1;", "    }", "    return 2;", "}"]
    wh = g.N("while", g.span2(3, 5, 5, 6), "", g.local(L, 3, "c", 0, "bool"), blk(L, 3, 5, ret(L, 4, "return 1;", "1")))
    g.put("ret_ok_loop_then_final_return", L, fnd(L, 2, 7, "f", [("c: bool", "c", "bool")], "u8", [wh, ret(L, 6, "return 2;", "2")]))

    L = ["manna v0;", "fn f(c: bool) -> u8 {", "    if c {", "        return 1;", "    }", "    return 2;", "}"]
    g.put("ret_ok_if_without_else_then_final_return", L, fnd(L, 2, 7, "f", [("c: bool", "c", "bool")], "u8", [
        iff(L, 3, 5, "c", 0, blk(L, 3, 5, ret(L, 4, "return 1;", "1"))), ret(L, 6, "return 2;", "2")]))

    L = ["manna v0;", "fn f(c: bool) -> u8 {", "    if c {", "    } else {", "        return 1;", "    }", "    return 2;", "}"]
    g.put("ret_ok_return_only_in_else_then_final", L, fnd(L, 2, 8, "f", [("c: bool", "c", "bool")], "u8", [
        iff(L, 3, 6, "c", 0, blk(L, 3, 4), blk(L, 4, 6, ret(L, 5, "return 1;", "1"))), ret(L, 7, "return 2;", "2")]))

    L = ["manna v0;", "fn f(c: bool) {", "    if c {", "        return;", "    }", "    return;", "}", "fn g() {", "}"]
    g.put("ret_ok_unit_functions", L,
          fnd(L, 2, 7, "f", [("c: bool", "c", "bool")], "", [iff(L, 3, 5, "c", 0, blk(L, 3, 5, ret(L, 4, "return;"))), ret(L, 6, "return;")]) +
          fnd(L, 8, 9, "g", [], "", []))

    # ---------- E0277 ----------
    def missing(name, L, fns):
        """fns: (function name, closing line) in order"""
        g.errors(name, L, [g.err(L, ln, "}", "E0277", msg_missing(f)) for f, ln in fns])

    L = ["manna v0;", "fn f() -> u8 {", "}"]
    missing("ret_missing_empty_body", L, [("f", 3)])
    L = ["manna v0;", "fn f() -> u8 { }"]
    missing("ret_missing_empty_body_one_line", L, [("f", 2)])
    L = ["manna v0;", "fn f(c: bool) -> u8 {", "    if c {", "        return 1;", "    }", "}"]
    missing("ret_missing_if_without_else", L, [("f", 6)])
    L = ["manna v0;", "fn f(c: bool, d: bool) -> u8 {", "    if c {", "        return 1;", "    } else if d {", "        return 2;", "    }", "}"]
    missing("ret_missing_else_if_chain_without_final_else", L, [("f", 8)])
    L = ["manna v0;", "fn f(c: bool) -> u8 {", "    if c {", "        return 1;", "    } else {", "    }", "}",
         "fn g(c: bool) -> u8 {", "    if c {", "    } else {", "        return 1;", "    }", "}"]
    missing("ret_missing_one_branch_does_not_return", L, [("f", 7), ("g", 13)])
    L = ["manna v0;", "fn f(c: bool, d: bool) -> u8 {", "    if c {", "        if d {", "            return 1;", "        }", "    } else {", "        return 3;", "    }", "}"]
    missing("ret_missing_nested_inner_if_without_else", L, [("f", 10)])
    L = ["manna v0;", "fn f() -> u8 {", "    while true {", "        return 1;", "    }", "}"]
    missing("ret_missing_while_true_never_counts", L, [("f", 6)])
    L = ["manna v0;", "fn f() -> u8 {", "    for let i: u8 = 0; i < 3; i = i + 1 {", "        return 1;", "    }", "}"]
    missing("ret_missing_return_inside_for_never_counts", L, [("f", 6)])
    L = ["manna v0;", "fn f(c: bool) -> u8 {", "    while c {", "        break;", "    }", "}"]
    missing("ret_missing_break_is_not_a_return", L, [("f", 6)])
    L = ["manna v0;", "fn f() -> u8 {", "    let x: u8 = 1;", "}"]
    missing("ret_missing_statements_only", L, [("f", 4)])
    L = ["manna v0;", "fn f() -> u8 {", "    if true {", "        return 1;", "    }", "}"]
    missing("ret_missing_constant_condition_is_not_folded", L, [("f", 6)])
    L = ["manna v0;", "fn a() -> u8 {", "}", "fn b() -> u8 {", "    return 1;", "}", "fn c() {", "}", "fn d() -> bool {", "}"]
    missing("ret_missing_reported_once_per_function", L, [("a", 3), ("d", 10)])
    L = ["manna v0;", "fn f() -> u8 {", "    let x: u8 = nope;", "}"]
    g.errors("ret_missing_after_the_bodys_own_errors", L, [g.err(L, 3, "nope", "E0230", "unknown name 'nope'"), g.err(L, 4, "}", "E0277", msg_missing("f"))])

    # ---------- E0276 ----------
    L = ["manna v0;", "fn f() -> u8 {", "    return;", "}"]
    g.errors("ret_bare_in_a_non_unit_function", L, [g.err(L, 3, "return;", "E0276", "missing return value: function 'f' returns u8")])
    L = ["manna v0;", "fn f() {", "    return 1;", "}"]
    g.errors("ret_value_in_a_unit_function", L, [g.err(L, 3, "1", "E0276", "unexpected return value: function 'f' returns nothing")])
    L = ["manna v0;", "fn f() {", "    return nope;", "}"]
    g.errors("ret_value_in_a_unit_function_is_still_checked", L, [g.err(L, 3, "nope", "E0276", "unexpected return value: function 'f' returns nothing"), g.err(L, 3, "nope", "E0230", "unknown name 'nope'")])
    L = ["manna v0;", "fn f(c: bool) {", "    if c {", "        return 1;", "    }", "}"]
    g.errors("ret_value_in_a_unit_function_inside_if", L, [g.err(L, 4, "1", "E0276", "unexpected return value: function 'f' returns nothing")])
    L = ["manna v0;", "fn f(c: bool) -> u8 {", "    if c {", "        return;", "    }", "}"]
    g.errors("ret_shape_and_missing_are_independent", L, [g.err(L, 4, "return;", "E0276", "missing return value: function 'f' returns u8"), g.err(L, 6, "}", "E0277", msg_missing("f"))])
    L = ["manna v0;", "fn f() -> u8 {", "    return true;", "}"]
    g.errors("ret_wrong_type_is_e0223_not_e0276", L, [g.err(L, 3, "true", "E0223", "type mismatch: expected u8, found bool")])
    L = ["manna v0;", "fn f() -> u8 {", "    return nope;", "}"]
    g.errors("ret_a_failed_return_still_counts_as_returning", L, [g.err(L, 3, "nope", "E0230", "unknown name 'nope'")])
    L = ["manna v0;", "fn f() -> u8 {", "    return;", "}", "fn g() {", "    return 2;", "}"]
    g.errors("ret_two_functions_two_shape_errors", L, [g.err(L, 3, "return;", "E0276", "missing return value: function 'f' returns u8"), g.err(L, 6, "2", "E0276", "unexpected return value: function 'g' returns nothing")])
