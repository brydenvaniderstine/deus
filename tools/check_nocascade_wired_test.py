#!/usr/bin/env python3
"""Tests of tools/check_nocascade_wired.py (step 21j) on scratch trees. make test-nocascade-wired-script"""
import os, shutil, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_nocascade_wired as cw

fails = []


def check(name, cond):
    print(("PASS: " if cond else "FAIL: ") + name)
    if not cond:
        fails.append(name)


MK = """.PHONY: nocascade-all check-poison-paths check-nocascade-prove check-nocascade-sinks test-nocascade-inject test-nocascade-inject-script check-inject-hook-absent check-generated
nocascade-all: check-poison-paths check-nocascade-prove check-nocascade-sinks test-nocascade-inject \\
	check-inject-hook-absent check-generated
check-poison-paths:
	true
check-nocascade-prove:
	true
check-nocascade-sinks:
	true
test-nocascade-inject:
	true
test-nocascade-inject-script:
	true
check-inject-hook-absent:
	true
check-generated:
	true
"""
CI = """jobs:
  ci:
    runs-on: x
    steps:
      - run: make check-poison-paths
      - run: make check-generated
  nocascade:
    runs-on: x
    timeout-minutes: 45
    steps:
      - run: make check-inject-hook-absent
      - run: make test-nocascade-inject
      - run: make check-nocascade-prove
      - run: make check-nocascade-sinks
"""


def tree(mk=MK, ci=CI, secs="487"):
    r = tempfile.mkdtemp()
    os.makedirs(os.path.join(r, ".github", "workflows"))
    os.makedirs(os.path.join(r, "tests", "nocascade"))
    open(os.path.join(r, "Makefile"), "w").write(mk)
    open(os.path.join(r, ".github", "workflows", "ci.yml"), "w").write(ci)
    if secs is not None:
        open(os.path.join(r, "tests", "nocascade", "JOB_TIME"), "w").write(secs + "\n")
    return r


def problems(r):
    p = cw.check(r)
    shutil.rmtree(r)
    return p


check("a fully wired tree passes (the -script test of the injection sweep is not part of the proof)", problems(tree()) == [])
check("W1: a new nocascade check that nocascade-all does not list fails", any("W1" in p and "check-nocascade-new" in p for p in problems(
    tree(mk=MK.replace(".PHONY: ", ".PHONY: check-nocascade-new ") + "check-nocascade-new:\n\ttrue\n"))))
check("W1: a new inject check that nocascade-all does not list fails", any("W1" in p and "test-inject-more" in p for p in problems(tree(mk=MK + "test-inject-more:\n\ttrue\n"))))
check("W1: a *-script test is not the proof and needs no place in the aggregate", problems(tree(mk=MK + "test-nocascade-other-script:\n\ttrue\n")) == [])
check("W2: a part of the proof that no CI step runs fails", any("W2" in p and "check-generated" in p for p in problems(tree(ci=CI.replace("      - run: make check-generated\n", "")))))
check("W2: a step that cannot fail the job does not count (|| true)", any("W2" in p and "|| true" in p for p in problems(tree(ci=CI.replace("make check-generated", "make check-generated || true")))))
check("W2: a step with continue-on-error does not count", any("W2" in p and "continue-on-error" in p for p in problems(tree(ci=CI.replace("      - run: make check-generated\n", "      - run: make check-generated\n        continue-on-error: true\n")))))
check("W3: the ablation proof in another job fails (D4: its own job)", any("W3" in p and "check-nocascade-prove" in p for p in problems(
    tree(ci=CI.replace("      - run: make check-nocascade-prove\n", "").replace("      - run: make check-poison-paths\n", "      - run: make check-poison-paths\n      - run: make check-nocascade-prove\n")))))
check("W3: no job named nocascade fails", any("W3" in p and "no job named" in p for p in problems(tree(ci=CI.replace("  nocascade:", "  other:")))))
check("W4: a job without timeout-minutes fails", any("W4" in p and "no timeout" in p for p in problems(tree(ci=CI.replace("    timeout-minutes: 45\n", "")))))
check("W4: a timeout under twice the measured time fails (487 s -> 17 min needed, 15 given)", any("W4" in p and "twice" in p for p in problems(tree(ci=CI.replace("timeout-minutes: 45", "timeout-minutes: 15")))))
check("W4: exactly twice the measured time passes (487 s: 17 min is 1020 s >= 974)", problems(tree(ci=CI.replace("timeout-minutes: 45", "timeout-minutes: 17"))) == [])
check("W4: a missing JOB_TIME fails", any("W4" in p and "JOB_TIME" in p for p in problems(tree(secs=None))))
check("a Makefile without nocascade-all is a problem, not a pass", problems(tree(mk="all:\n\ttrue\n")) == ["the Makefile has no nocascade-all target"])
check("a tree with no workflow is a problem, not a pass", cw.check(tempfile.mkdtemp()) == ["the Makefile or .github/workflows/ci.yml is missing"])

print("nocascade-wired tests: %d failed" % len(fails) if fails else "nocascade-wired tests: all passed")
sys.exit(1 if fails else 0)
