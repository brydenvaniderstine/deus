# Generates the typed-IR step 10 goldens (tests/ir/call_*, unit_*, recursion_*: calls, E0232-E0235, the E0226 unit rule) from a spec whose columns come from the source text, independent of the C builder; run from the repo root.
import gen_ir_goldens_step7 as g

def ret(L, ln, text, *kids):
    return g.stmt(L, ln, text, "return", "", *kids)

def call(L, ln, text, t, name, *args, outer=None):
    return g.N("call", g.sp(L, ln, text), "fn:%s %s" % (name, t), *args)

def plain(L, first, last, name, params, rt, *stmts):
    return g.func(L, first, last, name, params, rt, g.body(L, first, last, *stmts))

def cl(L, ln, name, slot, t, occ=0): return g.local(L, ln, name, slot, t, occ)
def nm(L, ln, text, t, occ=0): return g.num(L, ln, text, t, occ)

msg_unit = "a 'unit' value can't be used here"
def arity(f, params, args):
    return "'%s' takes %d argument%s, found %d" % (f, params, "" if params == 1 else "s", args)
named = lambda f: "'%s' is a function; named arguments are for struct construction" % f
rec = "recursion is not allowed in v0"

if __name__ == "__main__":
    # ---------- accepted ----------
    L = ["manna v0;", "fn add(a: u8, b: u8) -> u8 {", "    return a + b;", "}", "fn f() -> u8 {", "    return add(1, 2);", "}"]
    add = plain(L, 2, 4, "add", [("a: u8", "a", "u8"), ("b: u8", "b", "u8")], "u8",
                ret(L, 3, "return a + b;", g.N("binary", g.sp(L, 3, "a + b"), "+ u8", cl(L, 3, "a", 0, "u8"), cl(L, 3, "b", 1, "u8"))))
    g.put("call_basic", L, add + plain(L, 5, 7, "f", [], "u8", ret(L, 6, "return add(1, 2);", call(L, 6, "add(1, 2)", "u8", "add", nm(L, 6, "1", "u8"), nm(L, 6, "2", "u8")))))

    L = ["manna v0;", "fn f() -> u8 {", "    return later(7);", "}", "fn later(a: u8) -> u8 {", "    return a;", "}"]
    g.put("call_forward_reference", L,
          plain(L, 2, 4, "f", [], "u8", ret(L, 3, "return later(7);", call(L, 3, "later(7)", "u8", "later", nm(L, 3, "7", "u8")))) +
          plain(L, 5, 7, "later", [("a: u8", "a", "u8")], "u8", ret(L, 6, "return a;", cl(L, 6, "a", 0, "u8"))))

    L = ["manna v0;", "fn id(a: i8) -> i8 {", "    return a;", "}", "fn f() -> i8 {", "    return id(-128);", "}"]
    g.put("call_literal_takes_the_parameter_type", L,
          plain(L, 2, 4, "id", [("a: i8", "a", "i8")], "i8", ret(L, 3, "return a;", cl(L, 3, "a", 0, "i8"))) +
          plain(L, 5, 7, "f", [], "i8", ret(L, 6, "return id(-128);", call(L, 6, "id(-128)", "i8", "id", g.N("int", g.sp(L, 6, "-128"), "-128 i8")))))

    L = ["manna v0;", "fn mix(a: u8, b: bool, c: i64) -> i64 {", "    return c;", "}", "fn f(x: u8, y: bool, z: i64) -> i64 {", "    return mix(x, y, z);", "}"]
    g.put("call_arguments_stay_in_written_order", L,
          plain(L, 2, 4, "mix", [("a: u8", "a", "u8"), ("b: bool", "b", "bool"), ("c: i64", "c", "i64")], "i64", ret(L, 3, "return c;", cl(L, 3, "c", 2, "i64"))) +
          plain(L, 5, 7, "f", [("x: u8", "x", "u8"), ("y: bool", "y", "bool"), ("z: i64", "z", "i64")], "i64",
                ret(L, 6, "return mix(x, y, z);", call(L, 6, "mix(x, y, z)", "i64", "mix", cl(L, 6, "x", 0, "u8", 0), cl(L, 6, "y", 1, "bool", 0), cl(L, 6, "z", 2, "i64", 0)))))

    L = ["manna v0;", "fn one() -> u8 {", "    return 1;", "}", "fn nothing() {", "}", "fn f() {", "    one();", "    nothing();", "}"]
    g.put("call_statements_may_discard_any_result", L,
          plain(L, 2, 4, "one", [], "u8", ret(L, 3, "return 1;", nm(L, 3, "1", "u8"))) + plain(L, 5, 6, "nothing", [], "") +
          plain(L, 7, 10, "f", [], "", g.stmt(L, 8, "one();", "expr_stmt", "", call(L, 8, "one()", "u8", "one")),
                g.stmt(L, 9, "nothing();", "expr_stmt", "", call(L, 9, "nothing()", "unit", "nothing"))))

    L = ["manna v0;", "fn g() -> bool {", "    return true;", "}", "fn f() -> bool {", "    let g = false;", "    return g;", "}", "fn h() -> bool {", "    return g();", "}"]
    g.put("call_looks_only_at_top_level_names", L,
          plain(L, 2, 4, "g", [], "bool", ret(L, 3, "return true;", g.boolean(L, 3, "true"))) +
          plain(L, 5, 8, "f", [], "bool", g.stmt(L, 6, "let g = false;", "let", "g #0 bool", g.boolean(L, 6, "false")), ret(L, 7, "return g;", cl(L, 7, "g", 0, "bool"))) +
          plain(L, 9, 11, "h", [], "bool", ret(L, 10, "return g();", call(L, 10, "g()", "bool", "g"))))

    L = ["manna v0;", "fn add(a: u8, b: u8) -> u8 {", "    return a + b;", "}", "fn f() -> u8 {", "    return add(add(1, 2), 3);", "}"]
    inner = call(L, 6, "add(1, 2)", "u8", "add", nm(L, 6, "1", "u8"), nm(L, 6, "2", "u8"))
    g.put("call_nested", L,
          plain(L, 2, 4, "add", [("a: u8", "a", "u8"), ("b: u8", "b", "u8")], "u8",
                ret(L, 3, "return a + b;", g.N("binary", g.sp(L, 3, "a + b"), "+ u8", cl(L, 3, "a", 0, "u8"), cl(L, 3, "b", 1, "u8")))) +
          plain(L, 5, 7, "f", [], "u8", ret(L, 6, "return add(add(1, 2), 3);", call(L, 6, "add(add(1, 2), 3)", "u8", "add", inner, nm(L, 6, "3", "u8")))))

    L = ["manna v0;", "fn one() -> u8 {", "    return 1;", "}", "fn f() -> bool {", "    return one() + 2 == 3;", "}"]
    plus = g.N("binary", g.sp(L, 6, "one() + 2"), "+ u8", call(L, 6, "one()", "u8", "one"), nm(L, 6, "2", "u8"))
    g.put("call_as_an_operand", L,
          plain(L, 2, 4, "one", [], "u8", ret(L, 3, "return 1;", nm(L, 3, "1", "u8"))) +
          plain(L, 5, 7, "f", [], "bool", ret(L, 6, "return one() + 2 == 3;", g.N("binary", g.sp(L, 6, "one() + 2 == 3"), "== bool", plus, nm(L, 6, "3", "u8")))))

    L = ["manna v0;", "fn sum(a: [3]u8) -> u8 {", "    return 0;", "}", "fn f(a: [3]u8) -> u8 {", "    return sum(a);", "}"]
    g.put("call_with_an_array_argument", L,
          plain(L, 2, 4, "sum", [("a: [3]u8", "a", "[3]u8")], "u8", ret(L, 3, "return 0;", nm(L, 3, "0", "u8"))) +
          plain(L, 5, 7, "f", [("a: [3]u8", "a", "[3]u8")], "u8", ret(L, 6, "return sum(a);", call(L, 6, "sum(a)", "u8", "sum", cl(L, 6, "a", 0, "[3]u8", 0)))))

    # ---------- E0232 / E0233 / E0234 / argument types ----------
    L = ["manna v0;", "fn f() {", "    nope(1, 2, 3);", "    let a = nope2(unknown);", "    let b = a;", "}"]
    g.errors("call_unknown_function", L, [g.err(L, 3, "nope", "E0232", "unknown function 'nope'"),
                                          g.err(L, 4, "nope2", "E0232", "unknown function 'nope2'"),
                                          g.err(L, 4, "unknown", "E0230", "unknown name 'unknown'")])
    L = ["manna v0;", "fn f(x: u8) {", "    x();", "}"]
    g.errors("call_a_local_is_not_a_function", L, [g.err(L, 3, "x", "E0232", "unknown function 'x'")])
    L = ["manna v0;", "fn add(a: u8, b: u8) -> u8 {", "    return a;", "}", "fn one(a: u8) -> u8 {", "    return a;", "}", "fn zero() -> u8 {", "    return 0;", "}",
         "fn f() {", "    add(1);", "    add(1, 2, 3);", "    one(1, 2);", "    zero(1);", "    one();", "}"]
    g.errors("call_wrong_number_of_arguments", L, [g.err(L, 12, "add(1)", "E0233", arity("add", 2, 1)), g.err(L, 13, "add(1, 2, 3)", "E0233", arity("add", 2, 3)),
                                                    g.err(L, 14, "one(1, 2)", "E0233", arity("one", 1, 2)), g.err(L, 15, "zero(1)", "E0233", arity("zero", 0, 1)),
                                                    g.err(L, 16, "one()", "E0233", arity("one", 1, 0))])
    L = ["manna v0;", "fn add(a: u8, b: u8) -> u8 {", "    return a;", "}", "fn f() {", "    add(nope);", "    let x = add(1);", "    let b: bool = x;", "}"]
    g.errors("call_arity_error_keeps_the_result_type_and_checks_the_arguments", L, [
        g.err(L, 6, "add(nope)", "E0233", arity("add", 2, 1)), g.err(L, 6, "nope", "E0230", "unknown name 'nope'"),
        g.err(L, 7, "add(1)", "E0233", arity("add", 2, 1)), g.err(L, 8, "x", "E0223", "type mismatch: expected bool, found u8")])
    L = ["manna v0;", "fn add(a: u8, b: u8) -> u8 {", "    return a;", "}", "fn f(flag: bool) {", "    add(true, 2);", "    add(1, 300);", "    add(flag, 1);", "    add(1, flag);", "}"]
    g.errors("call_argument_types", L, [g.err(L, 6, "true", "E0223", "type mismatch: expected u8, found bool"), g.err(L, 7, "300", "E0224", "integer literal 300 does not fit in u8"),
                                         g.err(L, 8, "flag", "E0223", "type mismatch: expected u8, found bool"), g.err(L, 9, "flag", "E0223", "type mismatch: expected u8, found bool")])
    L = ["manna v0;", "fn add(a: u8, b: u8) -> u8 {", "    return a;", "}", "fn f() {", "    add(a: 1, b: 2);", "    add(1, b: 2);", "    add(a: nope);", "}"]
    g.errors("call_named_arguments_to_a_function", L, [g.err(L, 6, "a: 1", "E0234", named("add")), g.err(L, 7, "b: 2", "E0234", named("add")),
                                                       g.err(L, 8, "a: nope", "E0234", named("add")), g.err(L, 8, "nope", "E0230", "unknown name 'nope'")])

    # ---------- E0226 ----------
    L = ["manna v0;", "fn nothing() {", "}", "fn take(a: u8) {", "}", "fn f(c: bool) -> u8 {",
         "    let x = nothing();", "    let y: u8 = nothing();", "    take(nothing());", "    let a = nothing() + 1;", "    let b = 1 + nothing();",
         "    let d = -nothing();", "    let e = !nothing();", "    return nothing();", "}"]
    g.errors("unit_value_cannot_be_bound_passed_operated_or_returned", L, [
        g.err(L, 7, "nothing()", "E0226", msg_unit), g.err(L, 8, "nothing()", "E0226", msg_unit), g.err(L, 9, "nothing()", "E0226", msg_unit),
        g.err(L, 10, "nothing()", "E0226", msg_unit), g.err(L, 11, "nothing()", "E0226", msg_unit), g.err(L, 12, "nothing()", "E0226", msg_unit),
        g.err(L, 13, "nothing()", "E0226", msg_unit), g.err(L, 14, "nothing()", "E0226", msg_unit)])
    L = ["manna v0;", "fn nothing() {", "}", "fn f(x: u8) {", "    if nothing() {", "    }", "    while nothing() {", "    }", "    for let i: u8 = 0; nothing(); i = i + 1 {", "    }", "    x = nothing();", "    let r = nothing() == nothing();", "}"]
    g.errors("unit_value_cannot_be_a_condition_or_assigned", L, [
        g.err(L, 5, "nothing()", "E0226", msg_unit), g.err(L, 7, "nothing()", "E0226", msg_unit), g.err(L, 9, "nothing()", "E0226", msg_unit),
        g.err(L, 11, "nothing()", "E0226", msg_unit), g.err(L, 12, "nothing()", "E0226", msg_unit)])
    L = ["manna v0;", "fn nothing() {", "}", "fn f() {", "    return nothing();", "}", "fn g() {", "    nope(nothing());", "    1 = nothing();", "}"]
    g.errors("unit_value_is_not_blamed_where_another_error_already_says_so", L, [
        g.err(L, 5, "nothing()", "E0276", "unexpected return value: function 'f' returns nothing"),
        g.err(L, 8, "nope", "E0232", "unknown function 'nope'"), g.err(L, 9, "1", "E0270", "can't assign to this expression")])

    # ---------- E0235 ----------
    L = ["manna v0;", "fn f() {", "    f();", "}"]
    g.errors("recursion_self", L, [g.err(L, 3, "f()", "E0235", rec)])
    L = ["manna v0;", "fn a() {", "    b();", "}", "fn b() {", "    a();", "}"]
    g.errors("recursion_two_functions", L, [g.err(L, 3, "b()", "E0235", rec)])
    L = ["manna v0;", "fn a() {", "    b();", "}", "fn b() {", "    c();", "}", "fn c() {", "    a();", "}"]
    g.errors("recursion_three_functions", L, [g.err(L, 3, "b()", "E0235", rec)])
    L = ["manna v0;", "fn outside() {", "    a();", "}", "fn a() {", "    b();", "}", "fn b() {", "    a();", "}", "fn also_outside() {", "    outside();", "    b();", "}"]
    g.errors("recursion_callers_outside_the_cycle_stay_silent", L, [g.err(L, 6, "b()", "E0235", rec)])
    L = ["manna v0;", "fn f(c: bool) {", "    if c {", "        f(true);", "    }", "}"]
    g.errors("recursion_self_call_inside_an_if", L, [g.err(L, 4, "f(true)", "E0235", rec)])
    L = ["manna v0;", "fn a() {", "    z();", "    b();", "    c();", "}", "fn z() {", "}", "fn b() {", "    a();", "}", "fn c() {", "    a();", "}"]
    g.errors("recursion_overlapping_cycles_are_one_error", L, [g.err(L, 4, "b()", "E0235", rec)])
    L = ["manna v0;", "fn a() {", "    a();", "}", "fn b() {", "    c();", "}", "fn c() {", "    b();", "}"]
    g.errors("recursion_two_separate_cycles_two_errors", L, [g.err(L, 3, "a()", "E0235", rec), g.err(L, 6, "c()", "E0235", rec)])
    L = ["manna v0;", "fn top() {", "    left();", "    right();", "}", "fn left() {", "    bottom();", "}", "fn right() {", "    bottom();", "}", "fn bottom() {", "}"]
    g.put("recursion_a_diamond_is_not_recursion", L,
          plain(L, 2, 5, "top", [], "", g.stmt(L, 3, "left();", "expr_stmt", "", call(L, 3, "left()", "unit", "left")), g.stmt(L, 4, "right();", "expr_stmt", "", call(L, 4, "right()", "unit", "right"))) +
          plain(L, 6, 8, "left", [], "", g.stmt(L, 7, "bottom();", "expr_stmt", "", call(L, 7, "bottom()", "unit", "bottom"))) +
          plain(L, 9, 11, "right", [], "", g.stmt(L, 10, "bottom();", "expr_stmt", "", call(L, 10, "bottom()", "unit", "bottom"))) +
          plain(L, 12, 13, "bottom", [], ""))
    L = ["manna v0;", "fn f() {", "    f();", "    let x = nope;", "}"]
    g.errors("recursion_is_reported_after_the_bodys_own_errors", L, [g.err(L, 4, "nope", "E0230", "unknown name 'nope'"), g.err(L, 3, "f()", "E0235", rec)])
    L = ["manna v0;", "fn f(a: u8) {", "    f();", "}"]
    g.errors("recursion_through_a_call_with_a_wrong_arity", L, [g.err(L, 3, "f()", "E0233", arity("f", 1, 0)), g.err(L, 3, "f()", "E0235", rec)])
