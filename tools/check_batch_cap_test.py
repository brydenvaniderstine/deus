#!/usr/bin/env python3
"""Tests of tools/check_batch_cap.py (step 20i) on scratch git repositories. make test-batch-cap-script"""
import os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_batch_cap as cb

fails = []


def check(name, cond):
    print(("PASS: " if cond else "FAIL: ") + name)
    if not cond:
        fails.append(name)


def repo(subjects):
    root = tempfile.mkdtemp()
    g = lambda *a: subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", "-C", root] + list(a), capture_output=True, text=True)
    g("init", "-q")
    g("commit", "-q", "--allow-empty", "-m", "base")
    base = g("rev-parse", "HEAD").stdout.strip()
    for s in subjects:
        g("commit", "-q", "--allow-empty", "-m", s)
    return root, base


def run(subjects):
    root, base = repo(subjects)
    cwd = os.getcwd()
    os.chdir(root)
    try:
        return cb.check(base, root)
    finally:
        os.chdir(cwd)
        shutil.rmtree(root)


ok, msg, steps = run(["Step 20a: x", "Step 20b: y", "Step 20c: z"])
check("three steps pass", ok is True and sorted(steps) == ["20a", "20b", "20c"])
ok, msg, steps = run(["Step 20a: x", "Step 20b: y", "Step 20c: z", "Step 20d: w"])
check("four steps fail", ok is False and "4 distinct" in msg)
ok, msg, steps = run(["Step 20f-1: a", "Step 20f-2: b", "Step 20f-3 (batch B review, finding 1): c", "Step 20f-7: d"])
check("sub-commits of one step are one step", ok is True and list(steps) == ["20f"])
ok, msg, steps = run(["IR 19a: a", "IR 19b-2: b", "IR 19c-6: c", "docs: x", "comments: y"])
check("IR commits count, docs and comments commits do not", ok is True and sorted(steps) == ["19a", "19b", "19c"])
ok, msg, steps = run(["Step 20e-fix8: a", "Step 20d-fix7: b", "Step 20c: c", "IR 19d: d"])
check("the review follow-ups name their step (20e-fix8 is step 20e): four steps fail", ok is False and len(steps) == 4)
ok, msg, steps = run([])
check("no commits: ok, none", ok is True and "none" in msg)
ok, msg, steps = cb.check("0" * 40)
check("an all-zero base is announced, not silently passed", ok is None and "NOT checked" in msg)
ok, msg, steps = cb.check("deadbeefdeadbeef")
check("an unknown base fails", ok is False)

print("batch-cap tests: %d failed" % len(fails) if fails else "batch-cap tests: all passed")
sys.exit(1 if fails else 0)
