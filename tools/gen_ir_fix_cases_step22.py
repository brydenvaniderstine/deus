# Generates tests/ir_fix/* for typed-IR step 22A (post-audit fixes, round 4). Each .fixed.mn is written by hand here (independent of the C builder); .remaining is
# what the fixed source must still report, columns computed from the fixed source. Run from the repo root with PYTHONPATH=tools.
#
# Finding 1: the fix-it self-check compared diagnostics by line and exact message and assumed a fix never changes lines. The E0215 fix deletes a whole attribute
# (a newline with it, when the attribute spans two lines), and E0210's message quotes the line and column of the first declaration, so a correct fix was rejected
# with exit 4. The only message of the compiler that quotes a position is E0210's ("'f' is already declared at L:C", diag.c diag_ir_duplicate_decl); the cases
# below move the quoted position (a deleted attribute before it on the same line, a deleted multi-line attribute before it; a rename on its line is not testable: a declaration error withholds renames, register C10) and the line of the diagnostic itself.
import gen_ir_fix_cases_step14 as f14
import gen_ir_goldens_step7 as g

write = f14.write
dup = lambda name, ln, col: "'%s' is already declared at %d:%d" % (name, ln, col)

if __name__ == "__main__":
    # the auditor's (a): the repeated attribute spans two lines; the rename E0280 that comes after it moves up one line
    L = ["manna v0;", "[kernel_hook] [", "kernel_hook] fn f() {}", "fn g_G() {}"]
    # round 1: only the attribute goes (a declaration error withholds renames, register C10); the E0280 is still reported, one line up. Round 2: the rename is offered.
    F = ["manna v0;", "[kernel_hook]  fn f() {}", "fn g_G() {}"]
    n = "fix_selfcheck_a_deleted_two_line_attribute_moves_a_rename_up_a_line"
    write(n, L, F, [g.err(F, 3, "g_G", "E0280", f14.snake("g_G"))])
    with open(f14.os.path.join(f14.OUT, n + ".fixed2.mn"), "w") as f: f.write("\n".join(["manna v0;", "[kernel_hook]  fn f() {}", "fn g_g() {}"]) + "\n")

    # the auditor's (b): E0210 quotes where `fn f` was, and the E0215 fix before it (same line) moves it left
    L = ["manna v0;", "[kernel_hook] [kernel_hook] fn f() {}", "struct f {}"]
    F = ["manna v0;", "[kernel_hook]  fn f() {}", "struct f {}"]
    write("fix_selfcheck_e0210_quotes_a_position_that_a_deleted_attribute_moves", L, F,
          [g.err(F, 3, "f {", "E0210", dup("f", 2, g.col(F, 2, "f() ")))])

    # E0210's quoted position is on a LATER line than the deletion (multi-line E0215): both the line of the diagnostic and the quoted line change
    L = ["manna v0;", "[kernel_hook] [", "kernel_hook] fn f() {}", "struct f {}"]
    F = ["manna v0;", "[kernel_hook]  fn f() {}", "struct f {}"]
    write("fix_selfcheck_e0210_after_a_deleted_two_line_attribute", L, F,
          [g.err(F, 3, "f {", "E0210", dup("f", 2, g.col(F, 2, "f() ")))])

    # the duplicate itself is on the line of the deleted attribute, after it
    L = ["manna v0;", "fn f() {}", "[kernel_hook] [", "kernel_hook] fn f() {}"]
    F = ["manna v0;", "fn f() {}", "[kernel_hook]  fn f() {}"]
    write("fix_selfcheck_e0210_on_the_line_where_a_two_line_attribute_ended", L, F,
          [g.err(F, 3, "f() ", "E0210", dup("f", 2, g.col(F, 2, "f() ")))])

    # ---- 22b (finding 2): the E0261 wrap adds nesting depth (the parser's limit is 256 units, E0103). The fix is offered only where the fixed source parses.
    # Boundaries measured on 761eb7a with the plain build: a parenthesised `a + b` holds up to 253 parentheses (254 failed: exit 4, "the fixed source does not parse: E0103"),
    # `return a + b;` inside nested `if true {` holds up to 126 (127 failed).
    e261 = lambda: "operands have different types: u8 and u32; convert one explicitly"
    def parens(n):
        return ["manna v0;", "fn f(a: u8, b: u32) -> u32 {", "    return " + "(" * n + "a + b" + ")" * n + ";", "}"]
    def ifs(n):
        return ["manna v0;", "fn f(a: u8, b: u32) -> u32 {"] + ["    if true {"] * n + ["    return a + b;"] + ["    }"] * n + ["    return b;", "}"]
    for n in (253, 254):
        L = parens(n)
        if n == 253:
            F = list(L); F[2] = "    return " + "(" * n + "u32(a) + b" + ")" * n + ";"
            write("fix_e0261_wrap_holds_at_253_nested_parentheses", L, F)
        else:
            write("fix_e0261_wrap_would_pass_the_nesting_limit_at_254_nested_parentheses_so_there_is_no_fix", L, L, [g.err(L, 3, "(", "E0261", e261())])  # the diagnostic is at the outermost parenthesis: a group's span includes them
    for n in (126, 127):
        L = ifs(n)
        ln = 2 + n + 1
        if n == 126:
            F = list(L); F[ln - 1] = "    return u32(a) + b;"
            write("fix_e0261_wrap_holds_at_126_nested_ifs", L, F)
        else:
            write("fix_e0261_wrap_would_pass_the_nesting_limit_at_127_nested_ifs_so_there_is_no_fix", L, L, [g.err(L, ln, "a + b", "E0261", e261())])
