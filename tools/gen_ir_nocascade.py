# Generates tests/nocascade/* (typed-IR step 21f/21g; the step 21 plan, "Golden generator"): one program per (consumer hole, poison producer).
#
# A HOLE is a place a poisoned value can sit (tools/nocascade_holes.py derives the list from ast.h; SKELETONS below has a program shape for each, and
# `nocascade_holes.py --check` fails if one is missing, rule F5). A PRODUCER is a source of exactly one real mistake: an expression (an unknown name, an unknown
# function, a unit call, an unknown field, an operator the operand's type lacks, an index or a field of an integer) or a type (an unknown type name). Every program is
# otherwise valid, so the ONLY thing the builder may report is the producer's own diagnostic: a second diagnostic is a cascade.
#
# The expected output is written HERE, from the mistake the producer is: its code and message come from docs/error_codes.md, its column is computed from where the
# producer's text was put (never read back from ir_dump). tests/nocascade/INDEX.tsv records, for every golden, the hole, the producer, the number of mistakes and the
# intended diagnostics; tools/check_nocascade_index.py compares the golden, the INDEX and the builder's output (rule F4).
#
# Run from the repo root with no arguments (writes tests/nocascade) or `--out DIR`. `--list` prints the plan without writing.
import os, sys

PREAMBLE = [
    "manna v0;",
    "struct P {",
    "    x: u8,",
    "}",
    "fn nothing() {",
    "}",
    "fn take(v: u8) -> u8 {",
    "    return v;",
    "}",
    "fn two(v: u8, w: u8) -> u8 {",
    "    return v;",
    "}",
]
SIG = "fn f(p: P, a: u8, b: bool, xs: [4]u8, n: u32) -> %s {"

# ---- producers: (name, text, offset of the diagnostic inside the text, code, message) ----
EXPR_PRODUCERS = [
    ("unknown_name", "nope", 0, "E0230", "unknown name 'nope'"),
    ("unknown_function", "nofn(1)", 0, "E0232", "unknown function 'nofn'"),
    ("unit_call", "nothing()", 0, "E0226", "a 'unit' value can't be used here"),
    ("unknown_field", "p.zz", 2, "E0242", "struct 'P' has no field 'zz'"),
    ("bad_operator", "~b", 0, "E0260", "operator '~' is not defined for bool"),
    ("not_indexable", "a[0]", 0, "E0252", "can't index/slice type u8"),
    ("no_fields", "a.x", 0, "E0251", "type u8 has no fields"),
]
TYPE_PRODUCERS = [
    ("unknown_type", "Nope", 0, "E0220", "unknown type 'Nope'"),
]

# a hole nothing can fill with a poisoned value, with the reason (printed by --list and kept checked in)
EXCLUDED_HOLES = {
    "Expr.named_arg.name": "the name slot of `P(x: 1)` is a field NAME: any other expression there is itself the mistake (E0243), not a poisoned value passing through",
}

# A BAD ASSIGNMENT TARGET (step 21i, register C11, Bryden 2026-10-27): the target of `=` that is not a place is reported once (E0270, at the target's first token), and the
# target is then checked with diagnostics ON and nothing blamed on its type (ir_body.c build_assign: ir_check_silent), so a real mistake inside it is an independent second
# mistake and is reported beside the E0270, in that order. Each shape holds one producer inside the target: TWO mistakes, E0270 and the producer's own. (Until C11 was fixed
# the inner mistake was muted and the output was the E0270 alone; the shape names keep the `muted_` prefix because renaming a golden is a removed path to the ratchet.)
# One combination has no second mistake: `(nothing()) = 1` - a parenthesised call that returns nothing is a unit VALUE, not an error, and only its being no place is wrong.
MUTED_TARGETS = [
    ("muted_paren", "    (@E@) = 1;"),
    ("muted_call_arg", "    take(@E@) = 1;"),
    ("muted_operand", "    (@E@ + 1) = 1;"),
]
PAREN_IS_THE_EXPRESSION = {"unknown_name", "bad_operator"}   # their diagnostics sit on the whole expression `nope` / `~b`, which `(...)` wraps: the span starts at the paren
ONE_MISTAKE = {("muted_paren", "unit_call"): "a parenthesised call that returns nothing is a unit value, which is no mistake in itself: only the E0270"}

# ---- skeletons: hole -> [variant]; variant = (name, return type, body lines with one @E@, {producer: reason it does not apply}) ----
POSTFIX = {"bad_operator": "a prefix operator binds looser than a postfix one: `~b.x` is `~(b.x)`, not `(~b).x`"}
NOT_PLACE = {p[0]: "the target of an assignment must be a place; this producer is not one" for p in EXPR_PRODUCERS if p[0] != "unknown_name"}
STMT_OK = {"unit_call": "a call to a function that returns nothing is a valid statement"}
SKELETONS = {
    "Expr.unary.operand": [
        ("neg", "u32", ["    return -@E@;"], {}),
        ("bitnot", "u32", ["    return ~@E@;"], {}),
        ("not", "bool", ["    return !@E@;"], {}),
    ],
    "Expr.binary.lhs": [(nm, rt, ["    return %s;" % (ex.replace("@L@", "@E@"))], {}) for nm, rt, ex in (
        ("add", "u32", "@L@ + n"), ("bitand", "u32", "@L@ & n"), ("shl", "u32", "@L@ << 1"), ("lt", "bool", "@L@ < n"),
        ("eq", "bool", "@L@ == n"), ("and", "bool", "@L@ && b"))],
    "Expr.binary.rhs": [(nm, rt, ["    return %s;" % (ex.replace("@R@", "@E@"))], {}) for nm, rt, ex in (
        ("add", "u32", "n + @R@"), ("bitand", "u32", "n & @R@"), ("shl", "u32", "n << @R@"), ("lt", "bool", "n < @R@"),
        ("eq", "bool", "n == @R@"), ("and", "bool", "b && @R@"))],
    "Expr.call.args[]": [
        ("one", "u8", ["    return take(@E@);"], {}),
        ("first_of_two", "u8", ["    return two(@E@, 1);"], {}),
        ("second_of_two", "u8", ["    return two(1, @E@);"], {}),
    ],
    "Expr.index.base": [("read", "u8", ["    return @E@[0];"], POSTFIX)],
    "Expr.index.index": [("read", "u8", ["    return xs[@E@];"], {})],
    "Expr.slice.base": [("both", "u8", ["    let s = @E@[0..2];", "    return 0;"], POSTFIX)],
    "Expr.slice.lo": [("lo", "u8", ["    let s = xs[@E@..2];", "    return 0;"], {})],
    "Expr.slice.hi": [("hi", "u8", ["    let s = xs[0..@E@];", "    return 0;"], {})],
    "Expr.field_access.base": [("read", "u8", ["    return @E@.x;"], POSTFIX)],
    "Expr.convert.operand": [("u8", "u8", ["    return u8(@E@);"], {})],
    "Expr.array.elems[]": [
        ("first", "u8", ["    let v: [2]u8 = [@E@, 1];", "    return 0;"], {}),
        ("second", "u8", ["    let v: [2]u8 = [1, @E@];", "    return 0;"], {}),
        ("inferred_first", "u8", ["    let v = [@E@, 1];", "    return 0;"], {}),
        # `let v = [1, @E@];` is not a valid skeleton: the FIRST element decides the type, so the literal `1` is its own E0225 whatever follows
    ],
    "Expr.array_repeat.value": [
        ("repeat", "u8", ["    let v: [3]u8 = [@E@; 3];", "    return 0;"], {}),
        ("inferred", "u8", ["    let v = [@E@; 3];", "    return 0;"], {}),
    ],
    "Expr.named_arg.value": [("construct", "u8", ["    let q: P = P(x: @E@);", "    return 0;"], {})],
    "Stmt.let_stmt.init": [
        ("annotated", "u8", ["    let y: u32 = @E@;", "    return 0;"], {}),
        ("inferred", "u8", ["    let y = @E@;", "    return 0;"], {}),
        ("for_init", "u8", ["    for let i: u32 = @E@; i < 3; i = i + 1 {", "    }", "    return 0;"], {}),
    ],
    "Stmt.assign_stmt.target": [("name", "u8", ["    @E@ = 1;", "    return 0;"], NOT_PLACE)],
    "Stmt.assign_stmt.value": [
        ("to_local", "u8", ["    n = @E@;", "    return 0;"], {}),
        ("to_element", "u8", ["    xs[0] = @E@;", "    return 0;"], {}),
        ("for_step", "u8", ["    for let i: u8 = 0; i < 3; i = @E@ {", "    }", "    return 0;"], {}),
    ],
    "Stmt.if_stmt.cond": [("if", "u8", ["    if @E@ {", "    }", "    return 0;"], {})],
    "Stmt.while_stmt.cond": [("while", "u8", ["    while @E@ {", "    }", "    return 0;"], {})],
    "Stmt.for_stmt.cond": [("for", "u8", ["    for let i: u8 = 0; @E@; i = i + 1 {", "    }", "    return 0;"], {})],
    "Stmt.return_stmt.value": [
        ("u32", "u32", ["    return @E@;"], {}),
        ("bool", "bool", ["    return @E@;"], {}),
    ],
    "Stmt.expr_stmt.expr": [("statement", "u8", ["    @E@;", "    return 0;"], STMT_OK)],
}

# A literal array or repeat whose element is poisoned has no element type: the local it initialises is then unknown, and every use of it must stay silent. Four uses are
# enough to tell an unknown local from an array of unknown elements (the shape the builder must never invent).
for _un, _lines in (("arith", "    let r = x + 1;"), ("index", "    let r = x[0];"), ("compare", "    let r = x < 1;"), ("call_arg", "    let r = take(x);")):
    SKELETONS["Expr.array.elems[]"].append(("inferred_first_then_" + _un, "u8", ["    let x = [@E@, 1];", _lines, "    return 0;"], {}))
    SKELETONS["Expr.array_repeat.value"].append(("inferred_then_" + _un, "u8", ["    let x = [@E@; 3];", _lines, "    return 0;"], {}))

# The type holes. A poisoned DECLARATION is only consumed where it is used, so each type skeleton is crossed with the ways a name of that type is used.
# USES: (name, lines using the poisoned name `x`, ends the function with its own return)
USES = [
    ("arith", ["    let r = x + 1;"], False),
    ("compare", ["    let r = x < 1;"], False),
    ("equal", ["    let r = x == x;"], False),
    ("logic", ["    let r = x && b;"], False),
    ("negate", ["    let r = -x;"], False),
    ("not", ["    let r = !x;"], False),
    ("index", ["    let r = x[0];"], False),
    ("field", ["    let r = x.x;"], False),
    ("call_arg", ["    let r = take(x);"], False),
    ("convert", ["    let r = u8(x);"], False),
    ("length", ["    let r = len(x);"], False),
    ("condition", ["    if x {", "    }"], False),
    ("assign", ["    x = 1;"], False),
    ("return", ["    return x;"], True),
]


def type_variants():
    """hole -> [(variant, return type, preamble extras, body lines with @E@, {producer: reason})] for the five type holes"""
    out = {"Stmt.let_stmt.type": [], "Field.type": [], "Decl.function_decl.return_type": [], "Type.array.elem": [], "Type.slice.elem": []}
    for un, lines, ends in USES:
        tail = [] if ends else ["    return 0;"]
        out["Stmt.let_stmt.type"].append(("local_" + un, "u8", [], ["    let x: @E@ = 1;"] + lines + tail, {}))
        out["Type.array.elem"].append(("local_" + un, "u8", [], ["    let x: [3]@E@ = [1, 2, 3];"] + lines + tail, {}))
    # a parameter or a return type is not a statement of f: they live in a function g that f calls
    for un, lines, ends in USES:
        g = ["fn g(x: @E@, b: bool) -> u8 {"] + lines + ([] if ends else ["    return 0;"]) + ["}"]
        out["Field.type"].append(("param_" + un, "u8", g, ["    return 0;"], {}))
        gs = ["fn g(x: []@E@, b: bool) -> u8 {"] + lines + ([] if ends else ["    return 0;"]) + ["}"]
        out["Type.slice.elem"].append(("param_" + un, "u8", gs, ["    return 0;"], {}))
    # a struct with a field of an unknown type, and the ways a program meets that struct
    for un, uses_ in (("construct", ["    let q = Q(y: 1);"]), ("construct_and_read", ["    let q = Q(y: 1);", "    let r = q.y + 1;"]),
                      ("pass_on", ["    let r = use_q(Q(y: 1));"]), ("read_param", ["    let r = read_q(qq);"])):
        pre = ["struct Q {", "    y: @E@,", "}", "fn use_q(q: Q) -> u8 {", "    return 0;", "}", "fn read_q(q: Q) -> u8 {", "    return 0;", "}"]
        body = uses_ + ["    return 0;"]
        if un == "read_param":
            body = ["    let qq = Q(y: 1);", "    let r = read_q(qq);", "    return 0;"]
        out["Field.type"].append(("struct_field_" + un, "u8", pre, body, {}))
    out["Decl.function_decl.return_type"] = [
        ("call_" + un, "u8", ["fn g() -> @E@ {", "    return 1;", "}"], ["    let x = g();"] + lines + ([] if ends else ["    return 0;"]), {}) for un, lines, ends in USES]
    return out


TYPE_SKELETONS = type_variants()


def skeleton_holes():
    return sorted(set(SKELETONS) | set(TYPE_SKELETONS))


def programs():
    """[dict]: every program of the matrix, with its expected diagnostics computed from where the producer's text went"""
    out, skipped = [], []
    rows = []
    for hole, variants in sorted(SKELETONS.items()):
        for vn, ret, body, excl in variants:
            rows.append((hole, vn, ret, [], body, excl, EXPR_PRODUCERS))
    for hole, variants in sorted(TYPE_SKELETONS.items()):
        for vn, ret, pre, body, excl in variants:
            rows.append((hole, vn, ret, pre, body, excl, TYPE_PRODUCERS))
    for hole, vn, ret, pre, body, excl, producers in rows:
        for pn, text, off, code, msg in producers:
            if pn in excl:
                skipped.append((hole, vn, pn, excl[pn]))
                continue
            # the lines of the program: the preamble, any extra declarations of this skeleton, f's signature and body
            extra_pre = [l for l in pre]
            lines = PREAMBLE + extra_pre + [SIG % ret] + body + ["}"]
            if "@E@" not in "\n".join(lines):
                raise SystemExit("skeleton %s/%s has no hole" % (hole, vn))
            if "\n".join(lines).count("@E@") != 1:
                raise SystemExit("skeleton %s/%s must have exactly one hole" % (hole, vn))
            at = next(i for i, l in enumerate(lines) if "@E@" in l)
            col = lines[at].index("@E@") + 1 + off
            lines = [l.replace("@E@", text) for l in lines]
            name = "%s__%s__%s" % (hole.replace(".", "_").replace("[]", "_list"), vn, pn)
            out.append({"name": name, "hole": hole, "variant": vn, "producer": pn, "lines": lines, "mistakes": 1,
                        "diags": ["%d:%d %s %s" % (at + 1, col, code, msg)]})
    for vn, line in MUTED_TARGETS:
        for pn, text, off, code, msg in EXPR_PRODUCERS:
            lines = PREAMBLE + [SIG % "u8"] + [line, "    return 0;"] + ["}"]
            at = next(i for i, l in enumerate(lines) if "@E@" in l)
            col = lines[at].index(line.strip()[0]) + 1        # E0270 is at the target's first token: the `(` or the callee's name
            pcol = lines[at].index("@E@") + 1 + off           # the producer's own diagnostic, inside the target
            if vn == "muted_paren" and pn in PAREN_IS_THE_EXPRESSION:
                pcol -= 1                                      # the diagnosed node IS the parenthesised expression, whose span starts at its `(`
            lines = [l.replace("@E@", text) for l in lines]
            diags = ["%d:%d E0270 can't assign to this expression" % (at + 1, col)]
            if (vn, pn) not in ONE_MISTAKE:
                diags.append("%d:%d %s %s" % (at + 1, pcol, code, msg))
            out.append({"name": "Stmt_assign_stmt_target__%s__%s" % (vn, pn), "hole": "Stmt.assign_stmt.target", "variant": vn, "producer": pn, "lines": lines,
                        "mistakes": len(diags), "diags": diags})
    return out, skipped


def write(outdir):
    progs, skipped = programs()
    os.makedirs(outdir, exist_ok=True)
    names = set()
    for p in progs:
        assert p["name"] not in names, p["name"]
        names.add(p["name"])
        with open(os.path.join(outdir, p["name"] + ".mn"), "w") as f:
            f.write("\n".join(p["lines"]) + "\n")
        with open(os.path.join(outdir, p["name"] + ".ir"), "w") as f:
            f.write("\n".join(["ERRORS:"] + p["diags"]) + "\n")
        if p["mistakes"] != 1:     # the number of independent mistakes, read by tools/nocascade_prove.py and tools/check_generated.py
            with open(os.path.join(outdir, p["name"] + ".mistakes"), "w") as f:
                f.write("%d\n" % p["mistakes"])
    with open(os.path.join(outdir, "INDEX.tsv"), "w") as f:
        f.write("# generated by tools/gen_ir_nocascade.py; golden, hole, variant, producer, mistakes, intended diagnostics (line:col code message, ' | ' between)\n")
        for p in progs:
            f.write("\t".join([p["name"], p["hole"], p["variant"], p["producer"], str(p["mistakes"]), " | ".join(p["diags"])]) + "\n")
    with open(os.path.join(outdir, "EXCLUDED.tsv"), "w") as f:
        f.write("# generated: combinations the matrix leaves out, with the reason (a whole hole, or one producer in one skeleton)\n")
        for h, why in sorted(EXCLUDED_HOLES.items()):
            f.write("\t".join([h, "*", "*", why]) + "\n")
        for h, v, pn, why in skipped:
            f.write("\t".join([h, v, pn, why]) + "\n")
    return progs, skipped


if __name__ == "__main__":
    argv = sys.argv[1:]
    if "--list" in argv:
        progs, skipped = programs()
        print("%d programs, %d combinations excluded, %d holes with skeletons, %d holes excluded" % (len(progs), len(skipped), len(skeleton_holes()), len(EXCLUDED_HOLES)))
        for h, why in sorted(EXCLUDED_HOLES.items()):
            print("EXCLUDED HOLE %s: %s" % (h, why))
        sys.exit(0)
    out = argv[argv.index("--out") + 1] if "--out" in argv else "tests/nocascade"
    progs, skipped = write(out)
    print("gen_ir_nocascade: %d programs, %d excluded combinations -> %s" % (len(progs), len(skipped), out))
