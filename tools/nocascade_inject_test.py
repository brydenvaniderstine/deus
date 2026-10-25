#!/usr/bin/env python3
"""Tests of tools/nocascade_inject.py (typed-IR step 21b): the sweep is clean on the real builder and FINDS the cascade when a silencer is removed.
Proof that the oracle can fail: a scratch copy of tools/mannac whose has_error() always says "not poison" must produce findings on a small corpus, and the
unmodified sources must produce none. make test-nocascade-inject-script"""
import os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
fails = []


def check(name, cond):
    print(("PASS: " if cond else "FAIL: ") + name)
    if not cond:
        fails.append(name)


CORPUS = {
    "binary.mn": "manna v0;\nfn f(a: u32, b: u32) -> u32 {\n    let x: u32 = a + b;\n    return x * 2;\n}\n",
    "cmp.mn": "manna v0;\nfn f(a: u8, b: u8) -> bool {\n    return a < b && a != 3;\n}\n",
    "array.mn": "manna v0;\nfn f(a: u32) -> u32 {\n    let xs = [a, 2, 3];\n    return xs[1];\n}\n",
    "bad.mn": "manna v0;\nfn f() -> u32 {\n    return nope;\n}\n",  # does not build clean: skipped, not swept
}


def sweep(root):
    r = subprocess.run([sys.executable, os.path.join(HERE, "nocascade_inject.py"), "--corpus", "corpus", "-j", "2"], cwd=root, capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def scratch(mutate):
    root = tempfile.mkdtemp(prefix="nc_test_")
    shutil.copytree(os.path.join(REPO, "tools", "mannac"), os.path.join(root, "tools", "mannac"))
    os.makedirs(os.path.join(root, "corpus"))
    for n, s in CORPUS.items():
        open(os.path.join(root, "corpus", n), "w").write(s)
    if mutate:
        p = os.path.join(root, "tools", "mannac", "ir_expr.c")
        s = open(p).read()
        old = "static bool has_error(Synth s) { return !s.group && s.node->type->kind == IR_T_ERROR; }"
        assert old in s, "the test's mutation target moved: update this test with has_error"
        open(p, "w").write(s.replace(old, "static bool has_error(Synth s) { (void)s; return false; }"))
    return root


root = scratch(False)
try:
    rc, out = sweep(root)
    m = re.search(r"(\d+) programs swept, (\d+) injection points", out)
    check("the unmodified builder: no finding, no crash (exit 0)", rc == 0 and "0 finding(s), 0 crash(es)" in out)
    check("the sweep really swept: 3 programs, more than 20 points, 1 skipped (does not build clean)", bool(m) and m.group(1) == "3" and int(m.group(2)) > 20 and "1 do not build clean" in out)
finally:
    shutil.rmtree(root, ignore_errors=True)

root = scratch(True)
try:
    rc, out = sweep(root)
    check("a builder whose has_error never fires: the sweep FAILS (exit 1) and prints findings", rc == 1 and "FINDING" in out)
    check("a finding names the program, the point and the diagnostics it produced", re.search(r"FINDING \S+ k=\d+ site=\w[\w-]* what=\d+ status=errors diagnostics=\d+ fix_offered=(YES|no)\n  \d+:\d+ E\d{4} ", out) is not None)
finally:
    shutil.rmtree(root, ignore_errors=True)

print("\n%d failure(s)" % len(fails) if fails else "\nall passed")
sys.exit(1 if fails else 0)
