#!/usr/bin/env python3
"""Tests of tools/coverage_check.py (step 20i): the matching of uncovered lines to reasons (missing, stale, repeated text), and the clang fact the assert rule rests on,
shown by compiling a 3-line function with coverage. Run from the repo root: python3 tools/coverage_check_test.py (make test-coverage-check-script)"""
import os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import coverage_check as cc

fails = []


def check(name, cond):
    print(("PASS: " if cond else "FAIL: ") + name)
    if not cond:
        fails.append(name)


def run(unc, art, reasons):
    """cc.main with a fake measurement and a fake reasons file"""
    t = tempfile.mkdtemp()
    p = os.path.join(t, "j.txt")
    open(p, "w").write("\n".join("\t".join(r) for r in reasons) + "\n")
    cc.uncovered = lambda: (unc, art)
    cc.JUSTIFIED = p
    import io, contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = cc.main(["x"])
    return rc, buf.getvalue()


U = [("a.c", 3, "x = 1;"), ("a.c", 9, "break;"), ("a.c", 20, "break;")]
rc, out = run(U, [], [("a.c", "x = 1;", "r1"), ("a.c", "break;", "r2"), ("a.c", "break;", "r3")])
check("every uncovered line has a reason: ok", rc == 0 and "0 without a reason; 0 stale" in out)
rc, out = run(U, [], [("a.c", "x = 1;", "r1"), ("a.c", "break;", "r2")])
check("a repeated line text needs one reason per occurrence", rc == 1 and "NO REASON a.c:20" in out)
rc, out = run(U, [], [("a.c", "x = 1;", "r1"), ("a.c", "break;", "r2"), ("a.c", "break;", "r3"), ("a.c", "y = 2;", "r4")])
check("a reason whose line is covered now or gone is stale and fails", rc == 1 and "STALE REASON" in out and "y = 2;" in out)
rc, out = run(U, [], [])
check("no reasons file entries: every uncovered line is reported", rc == 1 and out.count("NO REASON") == 3)
rc, out = run([], [("a.c", 5, "int y = x + 1;")], [])
check("the zero-count line after an assert needs no reason and is counted", rc == 0 and "1 further lines" in out)

# the clang fact: the statement after an assert reads 0 although the function ran
t = tempfile.mkdtemp()
src = os.path.join(t, "a.c")
open(src, "w").write('#include <assert.h>\n#include <stdio.h>\nint f(int x) {\n    assert(x > 0);\n    int y = x + 1;\n    return y;\n}\nint main(void) { printf("%d\\n", f(2)); return 0; }\n')
exe = os.path.join(t, "a")
cc_bin = os.environ.get("CLANG", "clang")
r = subprocess.run([cc_bin, "-std=c11", "-g", "-fprofile-instr-generate", "-fcoverage-mapping", "-o", exe, src], capture_output=True, text=True)
check("the coverage experiment compiles", r.returncode == 0)
if r.returncode == 0:
    env = dict(os.environ, LLVM_PROFILE_FILE=os.path.join(t, "a.profraw"))
    subprocess.run([exe], env=env, capture_output=True)
    tool = cc.llvm_cov()
    prof = [x for x in (["xcrun", "llvm-profdata"] if tool[0] == "xcrun" else [tool[0].replace("llvm-cov", "llvm-profdata")])]
    subprocess.run(prof + ["merge", "-sparse", os.path.join(t, "a.profraw"), "-o", os.path.join(t, "a.profdata")], check=True)
    lcov = subprocess.run(tool + ["export", "-format=lcov", exe, "-instr-profile=" + os.path.join(t, "a.profdata")], capture_output=True, text=True, check=True).stdout
    da = {int(l[3:].split(",")[0]): int(l[3:].split(",")[1]) for l in lcov.split("\n") if l.startswith("DA:")}
    check("clang: the assert line ran (count > 0) and the next statement reads 0 although it ran", da.get(4, 0) > 0 and da.get(5) == 0 and da.get(6, 0) > 0)

print("coverage-check tests: %d failed" % len(fails) if fails else "coverage-check tests: all passed")
sys.exit(1 if fails else 0)
