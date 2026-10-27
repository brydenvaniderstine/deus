#!/usr/bin/env python3
# Memory stress for the rename bookkeeping (typed-IR step 19a; re-audit-3 finding 1).
#
# Usage: tools/ir_memory_stress.py <ir_dump>        (make test-ir-memory-stress)
#
# Each case is a small-to-medium program whose diagnostics are known by construction and whose use lists are large: value uses of
# a name that is not a local (an unknown name, a misnamed function) while misnamed locals are visible, distinct unknown names, long
# names, and 30,000 well-named locals. SINCE C11 (Bryden 2026-10-27) the target of a non-assignable assignment is checked with diagnostics ON,
# so every use in it is reported and the 20-error cap bounds the number of uses one build can note (at step 19 a muted target noted 3,000 or
# 150,000 silently): cases 1-6 keep their shape (the same names, the same misnamed declarations) with as many uses as fit under the cap, and
# the bulk dimensions that remain reachable - 30,000 well-named locals (4, 8) and 20,000-character names (1, 7) - are kept at full size. At step 18 the bookkeeping that notes such a use converted every visible misnamed local's name
# again per use (17 bytes per name byte, in the IR arena), so 5 KB reached E0201 ("input too large"); the arena use must be linear in
# the input. Every case asserts the EXACT diagnostic codes (so no E0201, and no fix lost to an unwinding build) and a time bound.
import subprocess, sys, os, tempfile, time, re

BIN = sys.argv[1]
TIME_LIMIT = 5.0  # seconds, per case, plain -O2 (the slowest case measured ~0.1 s at step 19)


def run(args):
    t = time.time()
    r = subprocess.run([BIN] + args, capture_output=True, text=True)
    return r, time.time() - t


def codes(out):
    return re.findall(r"^\d+:\d+ (E\d{4}) ", out, re.M)


def case(name, src, want, fixed_has=None, fixed_keeps=None):
    p = os.path.join(tmp, name + ".mn")
    open(p, "w").write(src)
    r, dt = run([p])
    got = codes(r.stdout)
    ok = r.returncode == 0 and not r.stderr and got == want and dt < TIME_LIMIT
    # the naming fix-its must still be there (an E0201 unwinding the build drops them)
    if ok and fixed_has is not None:
        r2, _ = run(["--fixed", p])
        ok = fixed_has in r2.stdout and (fixed_keeps is None or fixed_keeps in r2.stdout)
    print(("PASS" if ok else "FAIL"), name, "%d bytes" % len(src), "%.2fs" % dt, "codes=%s" % ",".join(got[:8]) + ("..." if len(got) > 8 else ""))
    if not ok:
        print("  expected codes:", want[:8], "status", r.returncode, "stderr", r.stderr[:200], "time limit", TIME_LIMIT)
        sys.exit(1)


tmp = tempfile.mkdtemp()

# 1. the audit's mem_min.mn: two misnamed 2,000-character locals, then a bad target with 14 uses of an unknown name (300 before C11; the cap is 20 errors:
# 2 naming errors + E0270 + the unknown function g + 14 = 18)
pad = "a" * 1996
case("two_long_misnamed_locals_300_unknown_uses",
     "manna v0;\nfn f() {\n    let loc0X%s: u8 = 0;\n    let loc1X%s: u8 = 0;\n    g(%s) = 1;\n}\n" % (pad, pad, ", ".join(["zz"] * 14)),
     ["E0280", "E0280", "E0270", "E0232"] + ["E0230"] * 14, fixed_has="loc0_x" + pad)

# 2. 14 misnamed locals, a bad target with 3 uses of an unknown name (18 misnamed locals and 3,000 uses before C11): 14 + E0270 + E0232 + 3 = 19 errors
names = ["loc%dX%s" % (i, "a" * 500) for i in range(14)]
case("eighteen_misnamed_locals_3000_unknown_uses",
     "manna v0;\nfn f() {\n" + "".join("    let %s: u8 = 0;\n" % n for n in names) + "    g(%s) = 1;\n}\n" % ", ".join(["zz"] * 3),
     ["E0280"] * 14 + ["E0270", "E0232"] + ["E0230"] * 3)

# 3. the audit's mem_fn.mn: ONE misnamed 1,000-character function used as a value 15 times (1,100 before C11) in one bad target: E0280 + E0270 + E0232 + 15 E0231
fname = "doIt" + "x" * 996
case("one_misnamed_function_1100_value_uses",
     "manna v0;\nfn %s() -> u8 {\n    return 1;\n}\nfn f() {\n    g(%s) = 1;\n}\n" % (fname, ", ".join([fname] * 15)),
     ["E0280", "E0270", "E0232"] + ["E0231"] * 15, fixed_has="do_itx")

# 4. 30,000 well-named locals, one misnamed local, a bad target with 15 unknown names (30,000 before C11; time: it was 0.72 s, quadratic)
body = "".join("    let v%d: u8 = 0;\n" % i for i in range(30000))
case("thirty_thousand_locals_one_misnamed_30000_unknown_uses",
     "manna v0;\nfn f() {\n" + body + "    let badName: u8 = 0;\n    g(%s) = 1;\n}\n" % ", ".join(["zz"] * 15),
     ["E0280", "E0270", "E0232"] + ["E0230"] * 15, fixed_has="bad_name")

# 5. a top-level rename exists (so unknown names are recorded) and 15 DISTINCT unknown names are used in a bad target (30,000 before C11)
case("misnamed_function_and_30000_distinct_unknown_names",
     "manna v0;\nfn doIt() -> u8 {\n    return 1;\n}\nfn f() {\n    g(%s) = 1;\n}\n" % ", ".join("un%d" % i for i in range(15)),
     ["E0280", "E0270", "E0232"] + ["E0230"] * 15, fixed_has="do_it")

# 6. the same unknown name 15 times next to a misnamed function (stored once; 150,000 before C11)
case("misnamed_function_and_150000_repeated_unknown_uses",
     "manna v0;\nfn doIt() -> u8 {\n    return 1;\n}\nfn f() {\n    g(%s) = 1;\n}\n" % ", ".join(["zz"] * 15),
     ["E0280", "E0270", "E0232"] + ["E0230"] * 15, fixed_has="do_it")

# 7. (never muted) 10 misnamed locals with 20,000-character names and 5 unknown names (14 errors, under the cap)
names = ["loc%dX%s" % (i, "a" * 19990) for i in range(10)]
case("ten_20000_char_misnamed_locals_five_unknown_names",
     "manna v0;\nfn f() {\n" + "".join("    let %s: u8 = 0;\n" % n for n in names) + "    let q: u8 = zz + zz + zz + zz + zz;\n}\n",
     ["E0280"] * 10 + ["E0230"] * 5)
# 8. collides() over a big function: 18 misnamed locals among 30,000 well-named ones (each rename looks its target up in an index of the
# function's locals, not in all of them); one of them collides with an existing name and gets no fix, the rest are renamed
body = "".join("    let v%d: u8 = 0;\n" % i for i in range(30000))
bad = "".join("    let badName%d: u8 = 0;\n" % i for i in range(17)) + "    let v7Z: u8 = 0;\n    let v7_z: u8 = 0;\n"
case("collisions_among_thirty_thousand_locals",
     "manna v0;\nfn f() {\n" + body + bad + "}\n",
     ["E0280"] * 18, fixed_has="bad_name16", fixed_keeps="let v7Z: u8") # v7Z would become v7_z: taken, so it keeps its name
print("memory stress: 8 cases, exact diagnostics, no E0201")
