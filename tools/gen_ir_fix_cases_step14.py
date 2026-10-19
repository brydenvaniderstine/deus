# Generates tests/ir_fix/*: the typed-IR step 14 rename fix-it cases. Each case is NAME.mn (input), NAME.fixed.mn (the
# source with EVERY offered fix applied: computed here by renaming whole words on the lines that mention the name,
# independent of the C builder) and, when some diagnostic is deliberately not fixable, NAME.remaining (what the
# fixed source must still report: columns computed from the fixed source). Run from the repo root.
import os, re
import gen_ir_goldens_step7 as g

OUT = "tests/ir_fix"

def write(name, lines, fixed_lines, remaining=None):
    with open(os.path.join(OUT, name + ".mn"), "w") as f: f.write("\n".join(lines) + "\n")
    with open(os.path.join(OUT, name + ".fixed.mn"), "w") as f: f.write("\n".join(fixed_lines) + "\n")
    if remaining is not None:
        with open(os.path.join(OUT, name + ".remaining"), "w") as f: f.write("\n".join(["ERRORS:"] + remaining) + "\n")

def rename(lines, mapping):
    """whole-word renames on every line (the cases are written so each mapped word is only the entity renamed)"""
    out = []
    for l in lines:
        for old, new in mapping.items():
            l = re.sub(r"(?<![A-Za-z0-9_])" + re.escape(old) + r"(?![A-Za-z0-9_])", new, l)
        out.append(l)
    return out

snake = lambda n: "'%s' is not snake_case" % n
pascal = lambda n: "'%s' is not PascalCase" % n

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)

    L = ["manna v0;", "fn doIt(x: u8) -> u8 {", "    return x;", "}", "fn a() -> u8 {", "    return doIt(1);", "}", "fn b() -> u8 {", "    let v: u8 = doIt(2);", "    return doIt(v);", "}"]
    write("fix_function_called_from_several_places", L, rename(L, {"doIt": "do_it"}))

    L = ["manna v0;", "struct my_point { x: u8, }", "struct Holder { p: my_point, ps: [2]my_point, }", "fn make() -> my_point {", "    return my_point(x: 1);", "}",
         "fn take(a: my_point, s: []my_point) -> my_point {", "    let b: my_point = a;", "    return b;", "}"]
    write("fix_struct_in_every_type_position_and_construction", L, rename(L, {"my_point": "MyPoint"}))

    L = ["manna v0;", "struct P { fooBar: u8, other: u8, }", "fn make() -> P {", "    return P(fooBar: 1, other: 2);", "}", "fn f(p: P) -> u8 {", "    p.fooBar = 3;", "    return p.fooBar + p.other;", "}"]
    write("fix_field_read_written_and_constructed", L, rename(L, {"fooBar": "foo_bar"}))

    L = ["manna v0;", "fn f(Count: u8) -> u8 {", "    let myVar: u8 = Count;", "    for let Idx: u8 = 0; Idx < Count; Idx = Idx + 1 {", "        if Idx == myVar {", "            myVar = myVar + Count;",
         "        }", "    }", "    return myVar + Count;", "}"]
    write("fix_parameter_local_and_loop_variable_in_nested_blocks", L, rename(L, {"Count": "count", "myVar": "my_var", "Idx": "idx"}))

    L = ["manna v0;", "fn doIt(a: u8, b: u32) -> u32 {", "    return a + b;", "}"]
    F = rename(L, {"doIt": "do_it"}); F[2] = "    return u32(a) + b;"
    write("fix_rename_next_to_another_fix_it", L, F)

    # collisions: the first violation gets the fix, a later one that would take the same name does not
    L = ["manna v0;", "fn a(fooBar: u8, foo_Bar: u8) -> u8 {", "    return fooBar + foo_Bar;", "}"]
    F = rename(L, {"fooBar": "foo_bar"})
    write("fix_two_names_converting_to_the_same_name", L, F, [g.err(F, 2, "foo_Bar", "E0280", snake("foo_Bar"))])
    L = ["manna v0;", "fn a(fooBar: u8, foo_bar: u8) -> u8 {", "    return fooBar + foo_bar;", "}"]
    write("fix_target_is_an_existing_name", L, L, [g.err(L, 2, "fooBar", "E0280", snake("fooBar"))])
    L = ["manna v0;", "fn Let() {", "}", "fn Len() {", "}", "fn BOOL() {", "}", "fn f(_: u8, U8: u8) {", "}"]
    write("fix_no_fix_for_a_keyword_len_or_an_empty_result", L, L,
          [g.err(L, 2, "Let", "E0280", snake("Let")), g.err(L, 4, "Len", "E0280", snake("Len")), g.err(L, 6, "BOOL", "E0280", snake("BOOL")),
           g.err(L, 8, "_", "E0280", snake("_")), g.err(L, 8, "U8", "E0280", snake("U8"))])
    # a misnamed field that is not the first one: its uses must land on its own rename (and `ok` stays untouched)
    L = ["manna v0;", "struct P { ok: u8, badName: u8, }", "fn f(p: P) -> u8 {", "    let q = P(ok: 1, badName: 2);", "    return p.ok + q.badName;", "}"]
    write("fix_second_field_is_misnamed", L, rename(L, {"badName": "bad_name"}))
    # a field's target is another field of the same struct: no fix for it
    L = ["manna v0;", "struct P { fooBar: u8, foo_bar: u8, }"]
    write("fix_field_target_is_an_existing_field", L, L, [g.err(L, 2, "fooBar", "E0280", snake("fooBar"))])
    # a use that comes before the declaration in the source: the edits are still in source order
    L = ["manna v0;", "fn g() -> u8 {", "    return doIt(1);", "}", "fn doIt(x: u8) -> u8 {", "    return x;", "}"]
    write("fix_use_before_declaration", L, rename(L, {"doIt": "do_it"}))
    L = ["manna v0;", "struct my_s { }", "struct MyS { }"]
    write("fix_struct_target_is_an_existing_struct", L, L, [g.err(L, 2, "my_s", "E0281", pascal("my_s"))])
    # another declaration error stops the build before the bodies: the uses are unknown, so no rename fix; the other fix applies
    L = ["manna v0;", "[kernel_hook] [kernel_hook] fn doIt() {", "}", "fn g() {", "    doIt();", "}"]
    F = list(L); F[1] = "[kernel_hook]  fn doIt() {"  # the E0215 fix deletes exactly the repeated `[kernel_hook]`, leaving its space
    write("fix_rename_omitted_when_another_declaration_error_hides_the_uses", L, F, [g.err(F, 2, "doIt", "E0280", snake("doIt"))])
    with open(os.path.join(OUT, "fix_rename_omitted_when_another_declaration_error_hides_the_uses.fixed2.mn"), "w") as f:  # the second round: the rename is now offered
        f.write("\n".join(rename(F, {"doIt": "do_it"})) + "\n")
    # more than 20 violations: the E0203 cap unwinds the build, the uses are incomplete, so no fix is offered at all
    n = 25
    L = ["manna v0;"] + ["fn bad%s() {\n}" % chr(ord('A') + i) for i in range(n)]
    L = "\n".join(L).split("\n")
    errs = []
    fn_lines = [i + 1 for i, l in enumerate(L) if l.startswith("fn ")]
    for k in range(20):
        errs.append(g.err(L, fn_lines[k], "bad" + chr(ord('A') + k), "E0280", snake("bad" + chr(ord('A') + k))))
    errs.append(g.err(L, fn_lines[19], "badT", "E0203", "too many errors, stopping"))
    write("fix_none_offered_when_the_error_cap_stops_the_build", L, L, errs)
    # many uses
    n = 5000
    L = ["manna v0;", "fn f(Big: u64) -> u64 {"] + ["    Big = Big + 1;"] * n + ["    return Big;", "}"]
    write("fix_a_name_with_many_uses", L, rename(L, {"Big": "big"}))
    # nothing to fix
    L = ["manna v0;", "struct P { x: u8, }", "fn f(p: P) -> u8 {", "    return p.x;", "}"]
    write("fix_nothing_to_fix", L, L)
