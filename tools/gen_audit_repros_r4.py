# Writes the inputs of tests/audit_repros/R4_*: the round-4 auditor's inputs for findings 1-6 (re-audit of 604213f, reproduced on Linux by the reviewer). The expected outputs are recorded
# by running the build and READ against the expectations in tests/audit_repros/README.md (as for R2 and R3): `tools/run_audit_repros.sh` compares them on every run (make test-audit-repros).
# Finding 3 (a rename whose fixed source overflows the parser's 16 MiB arena, `fn f(N: u8) { N; ... }`, a 200-character camelCase N used 35,711 times) is NOT stored: the input is ~10 MB;
# `make test-ir-fix-limits` (tools/ir_fix_limits_test.py) generates it and finds the boundary. Run from the repo root.
import os

OUT = "tests/audit_repros"


def put(name, text):
    with open(os.path.join(OUT, name + ".mn"), "w") as f:
        f.write(text)


if __name__ == "__main__":
    # finding 1: the self-check keyed a diagnostic on its line and exact message; the E0215 fix deletes a newline, E0210's message quotes L:C. Both exited 4.
    put("R4_f1a_e0215_two_line_attribute_before_a_rename", "manna v0;\n[kernel_hook] [\nkernel_hook] fn f() {}\nfn g_G() {}\n")
    put("R4_f1b_e0210_quotes_a_position_the_e0215_fix_moves", "manna v0;\n[kernel_hook] [kernel_hook] fn f() {}\nstruct f {}\n")
    # finding 2: the E0261 wrap passed the nesting-depth limit (E0103); the fixed source did not parse (exit 4)
    put("R4_f2a_e0261_wrap_past_the_depth_limit_254_parentheses", "manna v0;\nfn f(a: u8, b: u32) -> u32 {\n    return " + "(" * 254 + "a + b" + ")" * 254 + ";\n}\n")
    put("R4_f2b_e0261_wrap_past_the_depth_limit_127_ifs", "manna v0;\nfn f(a: u8, b: u32) -> u32 {\n" + "    if true {\n" * 127 + "    return a + b;\n" + "    }\n" * 127 + "    return b;\n}\n")
    # finding 4: E0222 withheld beside an unrelated field mistake (today only the unrelated one)
    big = "[4294967296]u8"
    put("R4_f4a_e0222_after_an_unknown_type", "manna v0;\nstruct S { a: Nope, b: %s, }\nfn main() {\n}\n" % big)
    put("R4_f4b_e0222_before_an_unknown_type", "manna v0;\nstruct S { a: %s, b: Nope, }\nfn main() {\n}\n" % big)
    put("R4_f4c_e0222_beside_a_duplicate_field", "manna v0;\nstruct S { a: u8, a: Nope, b: %s, }\nfn main() {\n}\n" % big)
    put("R4_f4d_e0222_in_a_struct_that_contains_itself", "manna v0;\nstruct A { a: A, b: %s, }\nfn main() {\n}\n" % big)
    # finding 5: D1 for conversions and indexes (the auditor's input, verbatim but for the line breaks)
    put("R4_f5_conversion_and_index_beside_a_poisoned_part",
        "manna v0;\nfn f(b: bool, x: u8, xs: [4]u8) -> bool {\n    let c = nope < b;\n    let d = bool(nope2);\n    let e = nope3[b];\n    let g = xs[b];\n    return true;\n}\n")
    # finding 6: a field rename leaves the spelling inside an E0241 parenthesized name stale
    put("R4_f6_field_rename_and_a_parenthesized_construction_name", "manna v0;\nstruct P { myX: u8, }\nfn f() -> P {\n    return P((myX): 1);\n}\n")
