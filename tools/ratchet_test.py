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
# finding 1 (Linux review of batch A): a removed path counts as moved only if its content is at a NEW path, one removed path per new path
check("a hash that already lives at an old path does not excuse a removal",
      len(ratchet.compare(b, snap(b["metrics"], {"t/a.ir": [2, "h1"], "t/c.ir": [1, "h2"]}), {})) == 0 and
      len(ratchet.compare(snap(b["metrics"], {"t/a.ir": [2, "h1"], "t/b.ir": [1, "h1"]}), snap(b["metrics"], {"t/a.ir": [2, "h1"]}), {})) == 1)
check("one new path accounts for only one removed path",
      len(ratchet.compare(snap(b["metrics"], {"t/a.ir": [2, "h1"], "t/b.ir": [2, "h1"]}), snap(b["metrics"], {"u/a.ir": [2, "h1"]}), {})) == 1)
check("a removed file with a waiver passes", ratchet.compare(b, snap(b["metrics"], {"t/a.ir": [2, "h1"]}), {("t/b.ir", 1, 0): "C1"}) == [])

# ---- the whole script on a scratch git repository (the CLI, so these also run against an older ratchet.py)
ZERO = None
tmp = tempfile.mkdtemp()
try:
    repo = os.path.dirname(HERE)
    GIT = ["git", "-c", "user.name=t", "-c", "user.email=t@t"]

    def scratch(name):
        root = os.path.join(tmp, name)
        os.makedirs(os.path.join(root, "tools", "mannac"))
        os.makedirs(os.path.join(root, "docs"))
        shutil.copytree(os.path.join(repo, "tests"), os.path.join(root, "tests"))
        os.remove(os.path.join(root, "tests", "ratchet.json"))   # the scratch repository's first version is made by its own `update`
        for f in ("docs/DEGRADATION.md", "docs/error_codes.md", "tools/audit_ir_poison_reasons.txt", "tools/ir_memory_stress.py", "tools/ratchet_test.py",
                  "tools/check_register_test.py", "tools/check_ci_targets_test.py", "tools/ratchet.py"):
            shutil.copy(os.path.join(repo, f), os.path.join(root, f))
        for f in os.listdir(os.path.join(repo, "tools", "mannac")):
            if f.endswith("_test.c"):
                shutil.copy(os.path.join(repo, "tools", "mannac", f), os.path.join(root, "tools", "mannac", f))
        sh(root, "init", "-q")
        commit(root, "base")
        return root

    def sh(root, *a):
        return subprocess.run(GIT + ["-C", root] + list(a), capture_output=True, text=True)

    def commit(root, msg):
        sh(root, "add", "-A")
        return sh(root, "commit", "-qm", msg, "--allow-empty")

    def run(root, *a):
        return subprocess.run([sys.executable, "tools/ratchet.py"] + list(a), cwd=root, capture_output=True, text=True)

    def force_write(root):
        """write tests/ratchet.json from the computed counts WITHOUT the update command's refusal (what a careless or hostile commit does)"""
        subprocess.run([sys.executable, "-c", "import sys,json; sys.path.insert(0,'tools'); import ratchet; "
                        "open('tests/ratchet.json','w').write(json.dumps(ratchet.compute(), indent=1, sort_keys=True)+'\\n')"], cwd=root, check=True)

    def baseline(name):
        root = scratch(name)
        assert run(root, "update").returncode == 0
        commit(root, "ratchet v1")
        return root

    ir_dir = lambda root: os.path.join(root, "tests", "ir")
    zero_golden = next(f[:-3] for f in sorted(os.listdir(os.path.join(repo, "tests", "ir"))) if f.endswith(".ir") and
                       ratchet.count_diags(open(os.path.join(repo, "tests", "ir", f)).read()) == 0)

    # basics
    root = baseline("basic")
    r = run(root, "check")
    check("e2e: an unchanged tree passes", r.returncode == 0 and "replayed 1 committed version" in r.stdout)
    victim = os.path.join(ir_dir(root), sorted(f for f in os.listdir(ir_dir(root)) if f.endswith(".ir"))[0])
    keep = open(victim).read()
    os.remove(victim)
    r = run(root, "check")
    check("e2e: a removed golden fails", r.returncode == 1 and "was removed" in r.stdout)
    open(victim, "w").write(keep)
    multi = next(os.path.join(ir_dir(root), f) for f in sorted(os.listdir(ir_dir(root))) if f.endswith(".ir") and
                 ratchet.count_diags(open(os.path.join(ir_dir(root), f)).read()) >= 2)
    text = open(multi).read()
    lines = text.split("\n")
    i = next(k for k, l in enumerate(lines) if ratchet.count_diags("ERRORS:\n" + l) == 1)
    open(multi, "w").write("\n".join(lines[:i] + lines[i + 1:]))
    r = run(root, "check")
    check("e2e: a golden that lost a diagnostic fails", r.returncode == 1 and "lost diagnostics" in r.stdout)
    r = run(root, "update")
    check("e2e: update refuses to lower a count", r.returncode == 1 and "refused" in r.stdout)
    open(multi, "w").write(text)
    cases = sorted(f for f in os.listdir(os.path.join(root, "tests", "ir_fix")) if f.endswith(".mn") and ".fixed" not in f)
    n = len(cases)
    for ext in (".mn", ".fixed.mn", ".remaining"):
        p = os.path.join(root, "tests", "ir_fix", cases[0][:-3] + ext)
        if os.path.exists(p):
            os.rename(p, p + ".bak")
    r = run(root, "check")
    check("e2e: a removed fix case fails", r.returncode == 1 and "ir_fix_cases" in r.stdout)

    # finding 1: the reviewer's case (a golden deleted, a 0-diagnostic golden copied under a new name, update, commit)
    root = baseline("moved")
    gone = "assign_target_calls_count_toward_recursion"
    for ext in (".mn", ".ir"):
        os.remove(os.path.join(ir_dir(root), gone + ext))
        shutil.copy(os.path.join(ir_dir(root), zero_golden + ext), os.path.join(ir_dir(root), "copy_of_" + zero_golden + ext))
    r = run(root, "update")
    check("finding 1: update refuses a deleted golden whose bytes live at an old path", r.returncode == 1 and gone in r.stdout)
    force_write(root)
    commit(root, "delete a golden, copy another")
    r = run(root, "check")
    check("finding 1: the history replay catches the committed deletion", r.returncode == 1 and gone in r.stdout)

    # findings 2 and 3: a lowered version followed by a normal commit
    root = baseline("replay")
    for ext in (".mn", ".fixed.mn", ".remaining"):
        p = os.path.join(root, "tests", "ir_fix", cases[0][:-3] + ext)
        if os.path.exists(p):
            os.remove(p)
    force_write(root)
    commit(root, "a lowered version (its CI run was cancelled or red)")
    shutil.copy(os.path.join(ir_dir(root), zero_golden + ".mn"), os.path.join(root, "tests", "ir_fix", "fix_a_new_case.mn"))
    shutil.copy(os.path.join(ir_dir(root), zero_golden + ".mn"), os.path.join(root, "tests", "ir_fix", "fix_a_new_case.fixed.mn"))
    force_write(root)
    commit(root, "a normal commit on top")
    r = run(root, "check")
    check("findings 2+3: a lowered version is still caught after a normal commit", r.returncode == 1 and "ir_fix_cases" in r.stdout)
    with open(os.path.join(root, "docs", "DEGRADATION.md"), "a") as f:
        f.write("\nRATCHET-WAIVER ir_fix_cases %d -> %d V7\n" % (n, n - 1))
    r = run(root, "check")
    check("findings 2+3: the metric passes once waived (its files need their own waivers)", "metric ir_fix_cases" not in r.stdout)

    # an unreadable version, a shallow clone, a single version
    root = baseline("garbage")
    open(os.path.join(root, "tests", "ratchet.json"), "w").write("{not json")
    commit(root, "garbage")
    force_write(root)
    commit(root, "repaired")
    r = run(root, "check")
    check("an unreadable committed version fails", r.returncode == 1 and "unreadable" in r.stdout)
    root = baseline("source")
    clone = os.path.join(tmp, "shallow")
    subprocess.run(["git", "clone", "-q", "--depth", "1", "file://" + root, clone], capture_output=True)
    r = run(clone, "check")
    check("a shallow clone fails instead of falling back", r.returncode == 1 and "shallow" in r.stdout)
    root = baseline("single")
    r = run(root, "check")
    check("the very first version has nothing before it and passes", r.returncode == 0)

    # finding 6: a waiver must name an open entry
    root = baseline("closedwaiver")
    for ext in (".mn", ".fixed.mn", ".remaining"):
        p = os.path.join(root, "tests", "ir_fix", cases[0][:-3] + ext)
        if os.path.exists(p):
            os.remove(p)
    with open(os.path.join(root, "docs", "DEGRADATION.md"), "a") as f:
        f.write("\nRATCHET-WAIVER ir_fix_cases %d -> %d V4\n" % (n, n - 1))   # V4 is CLOSED in the register
    r = run(root, "check")
    check("finding 6: a waiver naming a CLOSED entry does not apply", r.returncode == 1 and "metric ir_fix_cases" in r.stdout)

    # finding 6: a removed test call is a lowered unit-test count
    root = baseline("uncalled")
    uc = os.path.join(root, "tools", "mannac", "arena_test.c")
    src = open(uc).read()
    call = next(l for l in src.split("\n") if l.strip().startswith("test_") and l.strip().endswith("();"))
    open(uc, "w").write(src.replace(call + "\n", "", 1))
    r = run(root, "check")
    check("finding 6: a test that is defined but no longer called fails", r.returncode == 1 and ("never called" in r.stdout or "unit_test" in r.stdout))
finally:
    shutil.rmtree(tmp, ignore_errors=True)

print("ratchet tests: %d failed" % len(fails) if fails else "ratchet tests: all passed")
sys.exit(1 if fails else 0)
