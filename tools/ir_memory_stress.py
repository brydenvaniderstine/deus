#!/usr/bin/env python3
# Memory stress for the rename bookkeeping (typed-IR step 19a; re-audit-3 finding 1).
#
# Usage: tools/ir_memory_stress.py <ir_dump>        (make test-ir-memory-stress)
#
# Each case is a small-to-large program whose diagnostics are known by construction and whose use lists are large. Cases 1-6 keep their bulk through VALID uses
# (step 21i, D3: calls of a misnamed function and of well-named functions, next to misnamed locals), as many as the arena limits allow; cases 4, 7 and 8 have
# 30,000 well-named locals or 20,000-character names. SINCE C11 (Bryden 2026-10-27) the target of a non-assignable assignment is checked with diagnostics ON, so a
# use of a name that is not a local is always an error, and the 20-error cap bounds how many one build can note (at step 19 a muted target noted 3,000 or 150,000
# silently). The path of step 18 that cost 17 bytes per name byte per use (ir_note_free_name, in the IR arena) runs ONLY for such a use, so cases 1-6 do not
# discriminate against e45c0bc (they pass there too: measured in step 21i, D3); case 7 (long names, 5 errors) still does (E0201 there; case 8 guards collides(), step 19a-2, and passes there too). The limits a case hits are
# named in its comment: the IR arena (16 MiB, E0201) or the parser's (16 MiB, E0110). At step 18, 5 KB reached E0201 ("input too large"); the arena use must be linear in
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


def case(name, src, want, fixed_has=None, fixed_keeps=None, fixed_count=None):
    p = os.path.join(tmp, name + ".mn")
    open(p, "w").write(src)
    r, dt = run([p])
    got = codes(r.stdout)
    ok = r.returncode == 0 and not r.stderr and got == want and dt < TIME_LIMIT
    # the naming fix-its must still be there (an E0201 unwinding the build drops them)
    if ok and fixed_has is not None:
        r2, _ = run(["--fixed", p])
        ok = fixed_has in r2.stdout and (fixed_keeps is None or fixed_keeps in r2.stdout)
        # every spelling of a renamed name must be renamed: the old spelling is gone and the new one stands exactly where the old ones stood
        if ok and fixed_count is not None:
            ok = r2.stdout.count(fixed_count[0]) == fixed_count[1]
    print(("PASS" if ok else "FAIL"), name, "%d bytes" % len(src), "%.2fs" % dt, "codes=%s" % ",".join(got[:8]) + ("..." if len(got) > 8 else ""))
    if not ok:
        print("  expected codes:", want[:8], "status", r.returncode, "stderr", r.stderr[:200], "time limit", TIME_LIMIT)
        sys.exit(1)


tmp = tempfile.mkdtemp()

# 1. the audit's mem_min.mn, with valid uses: two misnamed 2,000-character locals, then 300 calls of a well-named function (fn g)
pad = "a" * 1996
case("two_long_misnamed_locals_300_valid_uses",
     "manna v0;\nfn g() -> u8 {\n    return 1;\n}\nfn f() {\n    let loc0X%s: u8 = 0;\n    let loc1X%s: u8 = 0;\n%s}\n" % (pad, pad, "    g();\n" * 300),
     ["E0280", "E0280"], fixed_has="loc0_x" + pad)

# 2. 18 misnamed 500-character locals (E0280 each; 20 is the cap), then 3,000 calls of a well-named function
names = ["loc%dX%s" % (i, "a" * 500) for i in range(18)]
case("eighteen_misnamed_locals_3000_valid_uses",
     "manna v0;\nfn g() -> u8 {\n    return 1;\n}\nfn f() {\n" + "".join("    let %s: u8 = 0;\n" % n for n in names) + "    g();\n" * 3000 + "}\n",
     ["E0280"] * 18, fixed_has="loc17_x" + "a" * 500)

# 3. the audit's mem_fn.mn, with valid uses: ONE misnamed 1,000-character function CALLED 1,100 times (the audit used it as a value, which is an error);
# every one of the 1,101 spellings must be renamed by the fix
fname = "doIt" + "x" * 996
case("one_misnamed_function_1100_valid_calls",
     "manna v0;\nfn %s() -> u8 {\n    return 1;\n}\nfn f() {\n%s}\n" % (fname, ("    %s();\n" % fname) * 1100),
     ["E0280"], fixed_has="do_itx", fixed_count=("do_it" + "x" * 996, 1101))

# 4. 30,000 well-named locals, one misnamed local, then 30,000 calls of a well-named function
body = "".join("    let v%d: u8 = 0;\n" % i for i in range(30000))
case("thirty_thousand_locals_one_misnamed_30000_valid_uses",
     "manna v0;\nfn g() -> u8 {\n    return 1;\n}\nfn f() {\n" + body + "    let badName: u8 = 0;\n" + "    g();\n" * 30000 + "}\n",
     ["E0280"], fixed_has="bad_name")

# 5. a misnamed function, and 19,000 DISTINCT well-named functions called once each (30,000 unknown names before C11; each function is a declaration of its own, so the
# IR arena (16 MiB, E0201) is the limit: 19,917 functions fit, 19,974 do not)
case("misnamed_function_and_19000_distinct_valid_calls",
     "manna v0;\nfn doIt() -> u8 {\n    return 1;\n}\n" + "".join("fn g%d() -> u8 {\n    return 1;\n}\n" % i for i in range(19000)) + "fn f() {\n    doIt();\n" + "".join("    g%d();\n" % i for i in range(19000)) + "}\n",
     ["E0280"], fixed_has="do_it")

# 6. the same misnamed function called 60,000 times (150,000 before C11 and before the valid-use rewrite). The most one build can hold is a limit of the IR arena
# (16 MiB, E0201), measured at 65,063 call statements of this shape: 60,000 stays under it
case("misnamed_function_and_60000_repeated_valid_calls",
     "manna v0;\nfn doIt() -> u8 {\n    return 1;\n}\nfn f() {\n" + "    doIt();\n" * 60000 + "}\n",
     ["E0280"], fixed_has="do_it", fixed_count=("do_it", 60001))

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
