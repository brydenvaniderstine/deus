# Generates tests/ir_fix/* for typed-IR step 22h (round-4 finding 6): a field rename leaves the spelling inside an E0241 parenthesized name stale. `P((myX): 1)` is E0241
# ("left of ':' must be a field name"), but `myX` is still the spelling of a field: --fixed renamed the field to my_x and left `(myX)`, and dropping the parentheses then gave E0242.
# M2_PLAN.md "Naming conventions" promises a rename is "renamed along rather than left stale". The spelling inside the parentheses (the name token only) is now a use of the field, or
# written-but-unresolved when it is no field. Every other construction shape in which a field's spelling appears without resolving (E0240, E0242, E0244, E0245) is pinned too. Each
# .fixed.mn is written by hand here, independent of the C builder; .remaining is what the fixed source must still report. Run from the repo root with PYTHONPATH=tools.
import gen_ir_fix_cases_step14 as f14
import gen_ir_goldens_step7 as g

write, rename = f14.write, f14.rename
not_field = "left of ':' must be a field name"
positional = "struct construction names every field: use 'field: value'"


def mistakes(name, n):
    open("tests/ir_fix/%s.mistakes" % name, "w").write("%d\n" % n)


if __name__ == "__main__":
    # the auditor's input: the parentheses stay, the spelling inside is renamed with the field
    L = ["manna v0;", "struct P { myX: u8, }", "fn f() -> P {", "    return P((myX): 1);", "}"]
    F = rename(L, {"myX": "my_x"})
    n = "fix_field_rename_reaches_a_parenthesized_name_in_a_construction"
    write(n, L, F, [g.err(F, 4, "(my_x)", "E0241", not_field)])
    # two levels of parentheses: still the one name token
    L = ["manna v0;", "struct P { myX: u8, }", "fn f() -> P {", "    return P(((myX)): 1);", "}"]
    F = rename(L, {"myX": "my_x"})
    write("fix_field_rename_reaches_a_doubly_parenthesized_name_in_a_construction", L, F, [g.err(F, 4, "((my_x))", "E0241", not_field)])
    # beside E0240 (a positional argument): the shape error stops the name checks, the spelling is still a use
    L = ["manna v0;", "struct Q { a: u8, myX: u8, }", "fn f() -> Q {", "    return Q(1, (myX): 2);", "}"]
    F = rename(L, {"myX": "my_x"})
    write("fix_field_rename_reaches_a_parenthesized_name_beside_a_positional_argument", L, F, [g.err(F, 4, "1", "E0240", positional)])
    # the parenthesized spelling is the rename's TARGET and resolves to nothing now: the rename is refused (it would resolve once the parentheses go)
    L = ["manna v0;", "struct P { myX: u8, }", "fn f() -> P {", "    return P((my_x): 1);", "}"]
    write("fix_field_rename_refused_when_a_parenthesized_name_writes_its_target", L, L, [g.err(L, 2, "myX", "E0280", f14.snake("myX")), g.err(L, 4, "(my_x)", "E0241", not_field)])
    mistakes("fix_field_rename_refused_when_a_parenthesized_name_writes_its_target", 2)
    # a left side that is not an identifier has no spelling: nothing to rename, the field is renamed and the E0241 stays
    L = ["manna v0;", "struct P { myX: u8, }", "fn f() -> P {", "    return P(1: 1);", "}"]
    F = rename(L, {"myX": "my_x"})
    write("fix_field_rename_with_a_number_left_of_the_colon_in_a_construction", L, F, [g.err(F, 4, "1:", "E0241", not_field)])
    # E0245 (out of order): both names are uses and are renamed; the order mistake stays
    L = ["manna v0;", "struct R { a: u8, myB: u8, }", "fn f() -> R {", "    return R(myB: 1, a: 2);", "}"]
    F = rename(L, {"myB": "my_b"})
    write("fix_field_rename_reaches_the_names_of_a_construction_in_the_wrong_order", L, F, [g.err(F, 4, "a:", "E0245", "fields must be given in declaration order: 'a' before 'my_b'")])
