#!/usr/bin/env python3
"""Tests of tools/ratchet.py (step 20a): the comparison on synthetic counts, then the whole script on a scratch copy of the repository's test
inputs with real deletions (a golden removed, a diagnostic dropped, a metric lowered), with and without a RATCHET-WAIVER line.
Run from the repo root: python3 tools/ratchet_test.py  (make test-ratchet-script)"""
import json, os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ratchet

fails = []


def check(name, cond):
    print(("PASS: " if cond else "FAIL: ") + name)
    if not cond:
        fails.append(name)


def snap(metrics=None, diags=None):
    return {"metrics": dict(metrics or {}), "diags": dict(diags or {})}


# ---- count_diags
check("count_diags counts the ERRORS lines only", ratchet.count_diags("ERRORS:\n1:2 E0001 a\n3:4 E0002 b\n\nTREE:\n5:6 not a diagnostic\n") == 2)
check("count_diags of a clean file is 0", ratchet.count_diags("(module)\n") == 0)

# ---- compare
b = snap({"goldens": 10, "audit_poison_reasons": 3}, {"t/a.ir": [2, "h1"], "t/b.ir": [1, "h2"]})
check("equal counts are fine", ratchet.compare(b, b, {}) == [])
check("an increase is fine", ratchet.compare(b, snap({"goldens": 11, "audit_poison_reasons": 3}, {"t/a.ir": [3, "h1"], "t/b.ir": [1, "h2"]}), {}) == [])
check("a lowered metric fails", len(ratchet.compare(b, snap({"goldens": 9, "audit_poison_reasons": 3}, b["diags"]), {})) == 1)
check("a lowered metric with a waiver passes", ratchet.compare(b, snap({"goldens": 9, "audit_poison_reasons": 3}, b["diags"]), {("goldens", 10, 9): "C1"}) == [])
check("a waiver for other numbers does not apply", len(ratchet.compare(b, snap({"goldens": 8, "audit_poison_reasons": 3}, b["diags"]), {("goldens", 10, 9): "C1"})) == 1)
check("a MAX metric may not grow", len(ratchet.compare(b, snap({"goldens": 10, "audit_poison_reasons": 4}, b["diags"]), {})) == 1)
check("a MAX metric may shrink", ratchet.compare(b, snap({"goldens": 10, "audit_poison_reasons": 2}, b["diags"]), {}) == [])
check("a file that lost a diagnostic fails", len(ratchet.compare(b, snap(b["metrics"], {"t/a.ir": [1, "h3"], "t/b.ir": [1, "h2"]}), {})) == 1)
check("a removed file fails", len(ratchet.compare(b, snap(b["metrics"], {"t/a.ir": [2, "h1"]}), {})) == 1)
check("a moved file is not a decrease", ratchet.compare(b, snap(b["metrics"], {"t/a.ir": [2, "h1"], "u/b.ir": [1, "h2"]}), {}) == [])
check("a removed file with a waiver passes", ratchet.compare(b, snap(b["metrics"], {"t/a.ir": [2, "h1"]}), {("t/b.ir", 1, 0): "C1"}) == [])

# ---- the whole script on a scratch copy
tmp = tempfile.mkdtemp()
try:
    root = os.path.join(tmp, "repo")
    os.makedirs(os.path.join(root, "tools", "mannac"))
    os.makedirs(os.path.join(root, "docs"))
    repo = os.path.dirname(HERE)
    shutil.copytree(os.path.join(repo, "tests"), os.path.join(root, "tests"))
    for f in ("docs/DEGRADATION.md", "docs/error_codes.md", "tools/audit_ir_poison_reasons.txt", "tools/ir_memory_stress.py", "tools/ratchet_test.py", "tools/check_register_test.py", "tools/check_ci_targets_test.py", "tools/ratchet.py"):
        shutil.copy(os.path.join(repo, f), os.path.join(root, f))
    for f in os.listdir(os.path.join(repo, "tools", "mannac")):
        if f.endswith("_test.c"):
            shutil.copy(os.path.join(repo, "tools", "mannac", f), os.path.join(root, "tools", "mannac", f))
    sh = lambda *a, **k: subprocess.run(a, cwd=root, capture_output=True, text=True, **k)
    sh("git", "init", "-q")
    sh("git", "-c", "user.name=t", "-c", "user.email=t@t", "add", "-A")
    sh("git", "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-qm", "base")
    run = lambda *a: sh(sys.executable, "tools/ratchet.py", *a)
    check("e2e: update writes a baseline", run("update").returncode == 0)
    sh("git", "-c", "user.name=t", "-c", "user.email=t@t", "add", "-A")
    sh("git", "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-qm", "baseline")
    r = run("check", "--base", "HEAD")
    check("e2e: an unchanged tree passes", r.returncode == 0 and "baseline = tests/ratchet.json at" in r.stdout)
    # a golden removed
    victim = os.path.join(root, "tests", "ir", sorted(f for f in os.listdir(os.path.join(root, "tests", "ir")) if f.endswith(".ir"))[0])
    keep = open(victim).read()
    os.remove(victim)
    r = run("check", "--base", "HEAD")
    check("e2e: a removed golden fails", r.returncode == 1 and "was removed" in r.stdout)
    open(victim, "w").write(keep)
    # a diagnostic dropped from a golden that has several
    multi = None
    for f in sorted(os.listdir(os.path.join(root, "tests", "ir"))):
        if f.endswith(".ir") and ratchet.count_diags(open(os.path.join(root, "tests", "ir", f)).read()) >= 2:
            multi = os.path.join(root, "tests", "ir", f)
            break
    text = open(multi).read()
    lines = text.split("\n")
    i = next(k for k, l in enumerate(lines) if k > 0 and lines[k - 1:k] and ratchet.count_diags("ERRORS:\n" + l) == 1)
    open(multi, "w").write("\n".join(lines[:i] + lines[i + 1:]))
    r = run("check", "--base", "HEAD")
    check("e2e: a golden that lost a diagnostic fails", r.returncode == 1 and "lost diagnostics" in r.stdout)
    r = run("update")
    check("e2e: update refuses to lower a count", r.returncode == 1 and "refused" in r.stdout)
    open(multi, "w").write(text)
    # a lowered fix-case count, waived
    cases = sorted(f for f in os.listdir(os.path.join(root, "tests", "ir_fix")) if f.endswith(".mn") and ".fixed" not in f)
    n = len(cases)
    for ext in (".mn", ".fixed.mn", ".remaining"):
        p = os.path.join(root, "tests", "ir_fix", cases[0][:-3] + ext)
        if os.path.exists(p):
            os.rename(p, p + ".bak")
    r = run("check", "--base", "HEAD")
    check("e2e: a removed fix case fails", r.returncode == 1 and "ir_fix_cases" in r.stdout)
    with open(os.path.join(root, "docs", "DEGRADATION.md"), "a") as f:
        f.write("\n### Z9 - a test entry\n\nRATCHET-WAIVER ir_fix_cases %d -> %d Z9\n" % (n, n - 1))
    r = run("check", "--base", "HEAD")
    check("e2e: the metric passes once waived (the removed case's own files still need a waiver)", "metric ir_fix_cases" not in r.stdout)
    # the fallback is announced
    r = run("check", "--base", "0000000000000000000000000000000000000000")
    check("e2e: an all-zero base falls back and says so", "FALLBACK" in r.stdout)
finally:
    shutil.rmtree(tmp, ignore_errors=True)

print("ratchet tests: %d failed" % len(fails) if fails else "ratchet tests: all passed")
sys.exit(1 if fails else 0)
