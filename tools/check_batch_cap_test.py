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
        if isinstance(s, tuple):    # (subject, [files]): a commit that changes these paths
            for f in s[1]:
                os.makedirs(os.path.dirname(os.path.join(root, f)) or root, exist_ok=True)
                open(os.path.join(root, f), "a").write("x\n")
            g("add", "-A")
            g("commit", "-q", "-m", s[0])
        else:
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
# Linux review of batch C, finding 2
ok, msg, steps = run([("Step 21a: a", ["k/a.c"]), ("Step 21b: b", ["k/b.c"]), ("Step 21c: c", ["k/c.c"]),
                      ("21d: d", ["k/d.c"]), ("Kernel 21e: e", ["k/e.c"]), ("step 21f: f", ["k/f.c"])])
check("six steps cannot pass by wording the last three without 'Step': the unnamed commits that change code fail", ok is False and "without a step id" in msg and len(steps["?"]) == 3)
ok, msg, steps = run([("Step 21a: a", ["k/a.c"]), ("fix the thing", ["k/b.c"])])
check("one unnamed commit that changes a non-docs path fails", ok is False and "k/b.c" in steps["?"][0])
ok, msg, steps = run([("Step 21a: a", ["k/a.c"]), ("docs: x", ["docs/x.md"]), ("notes", ["README.md", "docs/y.txt"])])
check("unnamed commits that change only docs/ and *.md need no step id", ok is True and list(steps) == ["21a"])
ok, msg, steps = run([("Step 21a: a", ["k/a.c"]), ("notes and a source file", ["docs/y.md", "k/z.c"])])
check("an unnamed commit that changes docs AND a source file fails", ok is False)
ok, msg, steps = cb.check("0" * 40)
check("an all-zero base FAILS (no 'not checked' pass)", ok is False and "no base" in msg)
ok, msg, steps = cb.check("")
check("an empty base FAILS", ok is False and "no base" in msg)
ok, msg, steps = cb.check("deadbeefdeadbeef")
check("an unknown base fails", ok is False)
root, base = repo(["Step 20a: x"])
subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", "-C", root, "checkout", "-q", "--orphan", "other"], capture_output=True)
subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", "-C", root, "commit", "-q", "--allow-empty", "-m", "Step 20z: y"], capture_output=True)
ok, msg, steps = cb.check(base, root)
check("a base that is not an ancestor of HEAD fails", ok is False and "not an ancestor" in msg)
shutil.rmtree(root)

# last_green_base: a fake gh on PATH
def fake_gh(body):
    d = tempfile.mkdtemp()
    open(os.path.join(d, "gh"), "w").write("#!/bin/sh\n" + body + "\n")
    os.chmod(os.path.join(d, "gh"), 0o755)
    return d


def green(body):
    d = fake_gh(body)
    old = os.environ["PATH"]
    os.environ["PATH"] = d + os.pathsep + old
    try:
        return cb.last_green_base()
    finally:
        os.environ["PATH"] = old
        shutil.rmtree(d)


sha = "a" * 40
b, why = green("echo '[{\"headSha\":\"%s\"}]'" % sha)
check("the base is the headSha of the last successful ci.yml run", b == sha)
b, why = green("echo '[]'")
check("no successful run: no base (the caller fails)", b is None and "no successful" in why)
b, why = green("echo boom >&2; exit 1")
check("gh failing: no base", b is None and "failed" in why)
b, why = green("echo 'not json'")
check("gh printing no JSON: no base", b is None)
b, why = green("echo '[{\"headSha\":\"zz\"}]'")
check("a headSha that is not a commit id: no base", b is None)
rc = subprocess.run([sys.executable, os.path.join(HERE, "check_batch_cap.py"), "last-green"], capture_output=True, text=True,
                    env=dict(os.environ, PATH=fake_gh("echo '[]'") + os.pathsep + os.environ["PATH"]), cwd=HERE)
check("main with last-green and no successful run exits 1, not 0", rc.returncode == 1 and "FAILED" in rc.stdout)

print("batch-cap tests: %d failed" % len(fails) if fails else "batch-cap tests: all passed")
sys.exit(1 if fails else 0)
