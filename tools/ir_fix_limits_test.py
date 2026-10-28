#!/usr/bin/env python3
# A rename fix whose fixed source exceeds the parser's 16 MiB arena (E0110) is not offered (step 22C, round-4 finding 3; docs/error_codes.md "Fix-it rules").
#
# Usage: tools/ir_fix_limits_test.py <ir_dump>        (make test-ir-fix-limits)
#
# `fn f(N: u8) { N; N; ... }` with a 200-character camelCase N: the original parses, the snake_case spelling is 300 characters (an underscore at each of the 100 humps) and the
# parser copies each identifier per use, so with enough uses the FIXED source overflows the arena while the original does not. The program is generated here, never stored
# (about 10 MB). The test finds the boundary by bisection - it does not hard-code the use count, which depends on the size of the AST nodes - and asserts, on BOTH sides of it
# and beyond: exit 0, nothing on stderr; below the boundary the fix is offered and the fixed source checks clean; at and above it the E0280 is reported with no fix (--fixed
# is the input unchanged), up to the use count where the original itself no longer parses (E0110 from the parser: the IR stage is not reached).
import os, subprocess, sys, tempfile

BIN = sys.argv[1]
NAME = "aB" * 100
tmp = tempfile.mkdtemp()
path = os.path.join(tmp, "p.mn")


def program(uses):
    return "manna v0;\nfn f(%s: u8) {\n" % NAME + ("    %s;\n" % NAME) * uses + "}\n"


def run(args, src=None):
    if src is not None:
        open(path, "w").write(src)
    r = subprocess.run([BIN] + args + [path], capture_output=True, text=True)
    if r.returncode != 0 or r.stderr:
        print("FAIL: %s exited %d: %s" % (" ".join(args) or "ir_dump", r.returncode, r.stderr[:200]))
        sys.exit(1)
    return r.stdout


def offered(uses):
    src = program(uses)
    out = run(["--fixed"], src)
    return out != src


def check(cond, what):
    if not cond:
        print("FAIL:", what)
        sys.exit(1)


# the original must parse at the largest size tried below the boundary and report exactly the one E0280
check(offered(35000), "35,000 uses: the rename should be offered")
lo, hi = 35000, 46000
check(not offered(hi), "%d uses: the rename should be withheld (fixed source over the arena)" % hi)
while hi - lo > 1:
    mid = (lo + hi) // 2
    if offered(mid):
        lo = mid
    else:
        hi = mid
print("boundary: the fix is offered at %d uses and withheld at %d" % (lo, hi))

# below: offered, and the fixed source checks clean
src = program(lo)
fixed = run(["--fixed"], src)
check(fixed != src and fixed.count("a_ba_b") > lo, "the offered fix renames every use")
check(run([], fixed).startswith("(fn"), "the fixed source at %d uses is not clean" % lo)
# at and above: reported without a fix
for uses in (hi, hi + 1, 40000, 45000):
    src = program(uses)
    dump = run([], src)
    check(dump.startswith("ERRORS:\n2:6 E0280 ") and dump.count("E0280") == 1 and dump.count("\n") == 2, "%d uses: exactly the E0280 should be reported" % uses)
    check(run(["--fixed"], src) == src, "%d uses: no fix may be applied" % uses)
print("fix limits: a rename whose fixed source overflows the parser arena is reported without a fix, exit 0 (boundary %d/%d)" % (lo, hi))
