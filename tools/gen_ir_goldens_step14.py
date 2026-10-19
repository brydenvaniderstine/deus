# Generates the typed-IR step 14 goldens (tests/ir/naming_*: E0280 snake_case, E0281 PascalCase) from a spec whose columns come from the source text, independent of the C builder; run from the repo root. (The rename fix-its are checked end to end by tests/ir_fix, tools/gen_ir_fix_cases_step14.py.)
import gen_ir_goldens_step7 as g
import gen_ir_goldens_step10 as c
import gen_ir_goldens_step11 as s11

plain, ret, nm, cl = c.plain, c.ret, c.nm, c.cl
sdecl = s11.sdecl
unk = lambda n: "unknown name '%s'" % n
snake = lambda n: "'%s' is not snake_case" % n
pascal = lambda n: "'%s' is not PascalCase" % n
e = g.err

if __name__ == "__main__":
    L = ["manna v0;", "struct my_point { x: u8, }"]
    g.errors("naming_struct_not_pascal_case", L, [e(L, 2, "my_point", "E0281", pascal("my_point"))])
    L = ["manna v0;", "fn doIt() {", "}"]
    g.errors("naming_function_not_snake_case", L, [e(L, 2, "doIt", "E0280", snake("doIt"))])
    L = ["manna v0;", "struct P { fooBar: u8, Baz: u8, }"]
    g.errors("naming_field_not_snake_case", L, [e(L, 2, "fooBar", "E0280", snake("fooBar")), e(L, 2, "Baz", "E0280", snake("Baz"))])
    L = ["manna v0;", "fn f(Count: u8, ok: u8) {", "}"]
    g.errors("naming_parameter_not_snake_case", L, [e(L, 2, "Count", "E0280", snake("Count"))])
    L = ["manna v0;", "fn f() {", "    let myVar: u8 = 1;", "}"]
    g.errors("naming_local_not_snake_case", L, [e(L, 3, "myVar", "E0280", snake("myVar"))])
    L = ["manna v0;", "fn f() {", "    for let Idx: u8 = 0; Idx < 3; Idx = Idx + 1 {", "    }", "}"]
    g.errors("naming_loop_variable_not_snake_case", L, [e(L, 3, "Idx", "E0280", snake("Idx"))])
    L = ["manna v0;", "fn f(Bad: u8) -> u8 {", "    let a = Bad;", "    let b = Bad + a;", "    return Bad + b + Bad;", "}", "fn g() -> u8 {", "    return Bad;", "}"]
    g.errors("naming_reported_once_at_the_declaration_not_per_use", L, [e(L, 2, "Bad", "E0280", snake("Bad")), e(L, 8, "Bad", "E0230", unk("Bad"))])
    L = ["manna v0;", "fn f(_x: u8, y_: u8, a__b: u8, X: u8, _: u8) {", "}"]
    g.errors("naming_underscores_and_capitals", L, [e(L, 2, "_x", "E0280", snake("_x")), e(L, 2, "y_", "E0280", snake("y_")), e(L, 2, "a__b", "E0280", snake("a__b")),
                                                    e(L, 2, "X", "E0280", snake("X")), e(L, 2, "_", "E0280", snake("_"))])
    # accepted forms
    L = ["manna v0;", "struct POINT { a_1: u8, x1: u8, }", "struct HTTPServer { x: u8, }", "fn a_1_b(a_1: u8) {", "    let x1: u8 = a_1;", "}"]
    g.put("naming_accepted_forms", L, sdecl(L, 2, 2, "POINT", 2, 1, [(2, "a_1: u8", "a_1", "u8", 0), (2, "x1: u8", "x1", "u8", 1)]) +
          sdecl(L, 3, 3, "HTTPServer", 1, 1, [(3, "x: u8", "x", "u8", 0)]) +
          plain(L, 4, 6, "a_1_b", [("a_1: u8", "a_1", "u8")], "", g.stmt(L, 5, "let x1: u8 = a_1;", "let", "x1 #1 u8", cl(L, 5, "a_1", 0, "u8"))))
    # a naming error does not stop the body pass
    L = ["manna v0;", "fn badName() {", "    let x = nope;", "}"]
    g.errors("naming_does_not_stop_body_checking", L, [e(L, 2, "badName", "E0280", snake("badName")), e(L, 3, "nope", "E0230", unk("nope"))])
    # a declaration that binds nothing, and `len`, are not also naming-checked
    L = ["manna v0;", "struct my_s { }", "struct my_s { }"]
    g.errors("naming_duplicate_struct_is_not_also_checked", L, [e(L, 2, "my_s", "E0281", pascal("my_s")), e(L, 3, "my_s", "E0210", "'my_s' is already declared at 2:8")])
    L = ["manna v0;", "struct P { Ab: u8, Ab: u8, }", "fn f(Cd: u8, Cd: u8) {", "}"]
    g.errors("naming_duplicate_field_and_parameter_are_not_also_checked", L, [e(L, 2, "Ab", "E0280", snake("Ab")), e(L, 2, "Ab", "E0211", "duplicate field 'Ab' in struct 'P'", 1),
                                                                             e(L, 3, "Cd", "E0280", snake("Cd")), e(L, 3, "Cd", "E0212", "duplicate parameter 'Cd'", 1)])
    L = ["manna v0;", "fn f() {", "    let Ab: u8 = 1;", "    let Ab: u8 = 2;", "}"]
    g.errors("naming_duplicate_local_is_not_also_checked", L, [e(L, 3, "Ab", "E0280", snake("Ab")), e(L, 4, "Ab", "E0213", "'Ab' is already declared in this function")])
    L = ["manna v0;", "struct len { }", "struct BadName2 { }", "struct bad_name { }"]
    g.errors("naming_len_struct_is_e0257_only_and_stops_the_build", L, [e(L, 2, "len", "E0257", "'len' is a builtin and can't be declared"), e(L, 4, "bad_name", "E0281", pascal("bad_name"))])
