# Generates the typed-IR step 19 goldens (tests/ir/*): the third audit round's boundary cases. Columns come from the source text,
# independent of the C builder; run from the repo root with PYTHONPATH=tools.
import gen_ir_goldens_step7 as g
import gen_ir_goldens_step12 as s12

e = s12.e
e261 = lambda x, y: "operands have different types: %s and %s; convert one explicitly" % (x, y)

if __name__ == "__main__":
    # The E0261 fix wraps an operand in `T(...)`, one more level; the parser bound is 512 (E0111). The diagnostic is the same at 510 and 511
    # chained operations (the fix is what differs: tests/ir_fix/fix_e0261_*_chain_*); these pin the diagnostic at both sides of the line.
    for n in (510, 511):
        body = "a" + " + a" * (n - 1) + " + b"      # n operations, the last one mixes u8 and u32
        L = ["manna v0;", "fn f(a: u8, b: u32) {", "    let r: u32 = " + body + ";", "}"]
        g.errors("e0261_on_a_%d_operation_chain" % n, L, [e(L, 3, body, "E0261", e261("u8", "u32"))])

    # 19f: an ERROR index or slice bound is consumed silently (ir_expr.c check_index_operand): one unknown name, one E0230, nothing after it. The
    # auditor measured this line as run by no golden. One program each for the index read, the index as an assignment target and a slice bound.
    unk = lambda n: "unknown name '%s'" % n
    L = ["manna v0;", "fn f(a: [4]u8) -> u8 {", "    let x = a[nope];", "    return 0;", "}"]
    g.errors("one_error_unknown_index_bound", L, [e(L, 3, "nope", "E0230", unk("nope"))])
    L = ["manna v0;", "fn f(a: [4]u8) {", "    a[nope] = 1;", "}"]
    g.errors("one_error_unknown_index_bound_as_an_assignment_target", L, [e(L, 3, "nope", "E0230", unk("nope"))])
    L = ["manna v0;", "fn f(a: [4]u8) {", "    let s = a[nope..2];", "}"]
    g.errors("one_error_unknown_slice_bound", L, [e(L, 3, "nope", "E0230", unk("nope"))])
