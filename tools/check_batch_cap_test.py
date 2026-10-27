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

# last_run_base: a fake gh on PATH. `gh run list` prints the runs (newest first), `gh run view ID --json jobs` the jobs of ID: a file per run id in $FAKE/view_ID
def fake_gh(runs, views=None, view_rc=0):
    d = tempfile.mkdtemp()
    open(os.path.join(d, "runs"), "w").write(runs)
    for rid, body in (views or {}).items():
        open(os.path.join(d, "view_" + rid), "w").write(body)
    script = '#!/bin/sh\nD=%s\nif [ "$2" = list ]; then cat "$D/runs"; exit 0; fi\n' % d
    script += 'if [ "$2" = view ]; then [ %d -ne 0 ] && { echo boom >&2; exit %d; }; cat "$D/view_$3"; exit 0; fi\nexit 9\n' % (view_rc, view_rc)
    open(os.path.join(d, "gh"), "w").write(script)
    os.chmod(os.path.join(d, "gh"), 0o755)
    return d


def run_json(rid, sha, conclusion):
    return '{"databaseId":%s,"headSha":"%s","conclusion":"%s"}' % (rid, sha, conclusion)


def steps_json(*pairs):
    return '{"jobs":[{"steps":[%s]}]}' % ",".join('{"name":"%s","conclusion":"%s"}' % p for p in pairs)


SETUP = ("Set up job", "success")
CHECKOUT_OK = ("Run actions/checkout@v4", "success")
POST = [("Post Run actions/checkout@v4", "success"), ("Complete job", "success")]
RAN_OK = steps_json(SETUP, CHECKOUT_OK, ("Run make test-lexer", "success"), ("Run make test-parser", "success"), *POST)
RAN_RED = steps_json(SETUP, CHECKOUT_OK, ("Run make test-lexer", "success"), ("Run make check-batch-cap", "failure"), ("Run make all", "skipped"), *POST)
NEVER = '{"jobs":[{"steps":[]},{"steps":[]}]}'                                   # the billing block: failure, jobs with no steps
CANCELLED_STEPS = steps_json(SETUP, CHECKOUT_OK, ("Run make test-lexer", "cancelled"), ("Run make test-parser", "skipped"), *POST)
CHECKOUT_FAILED = steps_json(SETUP, ("Run actions/checkout@v4", "failure"), ("Run make test-lexer", "skipped"), *POST)   # Set up job succeeded, nothing after the failed checkout ran
CHECKOUT_ONLY = steps_json(SETUP, CHECKOUT_OK, ("Run make test-lexer", "skipped"), *POST)                                  # the checkout worked and then nothing was run
NO_CHECKOUT_JOB = steps_json(SETUP, ("Run make test-lexer", "success"), *POST)                                              # a job with no checkout: its own steps count


def base_of(runs, views=None, view_rc=0):
    d = fake_gh(runs, views, view_rc)
    old = os.environ["PATH"]
    os.environ["PATH"] = d + os.pathsep + old
    try:
        return cb.last_run_base()
    finally:
        os.environ["PATH"] = old
        shutil.rmtree(d)


sha, sha2, sha3 = "a" * 40, "b" * 40, "c" * 40
b, why = base_of("[%s]" % run_json(1, sha, "success"), {"1": RAN_OK})
check("the base is the headSha of the newest ci.yml run whose jobs ran (green)", b == sha and "success" in why)
b, why = base_of("[%s,%s]" % (run_json(2, sha2, "failure"), run_json(1, sha, "success")), {"2": RAN_RED, "1": RAN_OK})
check("a RED run whose jobs ran is the base, ahead of an older green one", b == sha2 and "failure" in why)
b, why = base_of("[%s,%s]" % (run_json(2, sha2, "cancelled"), run_json(1, sha, "success")), {"1": RAN_OK})
check("a cancelled run is skipped (the older run is the base; its jobs are not even asked for)", b == sha)
b, why = base_of("[%s,%s]" % (run_json(2, sha2, "failure"), run_json(1, sha, "success")), {"2": NEVER, "1": RAN_OK})
check("a run that never started (conclusion failure, no step ever ran) is skipped", b == sha)
b, why = base_of("[%s,%s]" % (run_json(2, sha2, "failure"), run_json(1, sha, "success")), {"2": CANCELLED_STEPS, "1": RAN_OK})
check("a failure whose steps were only cancelled or skipped did not run either", b == sha)
b, why = base_of("[%s,%s,%s]" % (run_json(3, sha3, "cancelled"), run_json(2, sha2, "failure"), run_json(1, sha, "failure")), {"2": NEVER, "1": RAN_RED})
check("cancelled, then never started, then red-and-ran: the third is the base", b == sha)
b, why = base_of("[%s,%s]" % (run_json(2, sha2, "failure"), run_json(1, sha, "cancelled")), {"2": NEVER})
check("every run cancelled or never started: no base (the caller fails)", b is None and "no ci.yml run" in why)
b, why = base_of("[]")
check("no runs at all: no base", b is None)
b, why = base_of("[%s,%s]" % (run_json(2, sha2, "failure"), run_json(1, sha, "success")), {"2": CHECKOUT_FAILED, "1": RAN_OK})
check("a run whose checkout FAILED (Set up job succeeded, nothing after the checkout ran) did not run: skipped", b == sha)
b, why = base_of("[%s,%s]" % (run_json(2, sha2, "failure"), run_json(1, sha, "success")), {"2": CHECKOUT_ONLY, "1": RAN_OK})
check("a checkout that worked and then nothing else ran did not run either", b == sha)
b, why = base_of("[%s,%s]" % (run_json(2, sha2, "failure"), run_json(1, sha, "success")), {"2": NO_CHECKOUT_JOB, "1": RAN_OK})
check("a job with no checkout step counts its own steps (the bookkeeping steps never count)", b == sha2)
check("ran_to_a_conclusion: one job that ran among jobs that did not is enough", cb.ran_to_a_conclusion(
    [{"steps": [{"name": "Set up job", "conclusion": "success"}, {"name": "Run actions/checkout@v4", "conclusion": "failure"}]},
     {"steps": [{"name": "Set up job", "conclusion": "success"}, {"name": "Run actions/checkout@v4", "conclusion": "success"}, {"name": "Run make x", "conclusion": "failure"}]}]))
b, why = base_of("[%s]" % run_json(1, sha, "success"), {"1": RAN_OK}, view_rc=1)
check("gh run view failing: no base, not a guess", b is None and "failed" in why)
b, why = base_of("[%s]" % run_json(1, "zz", "success"), {"1": RAN_OK})
check("a headSha that is not a commit id is not a base", b is None)
d = fake_gh("boom")
old = os.environ["PATH"]
os.environ["PATH"] = d + os.pathsep + old
b, why = cb.last_run_base()
os.environ["PATH"] = old
shutil.rmtree(d)
check("gh printing no JSON: no base", b is None and "no JSON" in why)
rc = subprocess.run([sys.executable, os.path.join(HERE, "check_batch_cap.py"), "last-run"], capture_output=True, text=True,
                    env=dict(os.environ, PATH=fake_gh("[]") + os.pathsep + os.environ["PATH"]), cwd=HERE)
check("main with last-run and no run that ran exits 1, not 0", rc.returncode == 1 and "FAILED" in rc.stdout)
check("the old name last-green is gone (a stale workflow fails loudly instead of being read as a ref)", subprocess.run([sys.executable, os.path.join(HERE, "check_batch_cap.py"), "last-green"], capture_output=True, text=True, cwd=HERE).returncode == 1)
print("batch-cap tests: %d failed" % len(fails) if fails else "batch-cap tests: all passed")
sys.exit(1 if fails else 0)
