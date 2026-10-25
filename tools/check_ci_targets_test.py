#!/usr/bin/env python3
"""Tests of tools/check_ci_targets.py (step 20c) on scratch trees. Run from the repo root: python3 tools/check_ci_targets_test.py (make test-ci-targets-script)"""
import os, shutil, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_ci_targets as ct

fails = []


def check(name, cond):
    print(("PASS: " if cond else "FAIL: ") + name)
    if not cond:
        fails.append(name)


MK = ".PHONY: all test-a test-b audit-c check-d fuzz test-wx\n\nall:\n\ttrue\ntest-a:\n\ttrue\ntest-b:\n\ttrue\naudit-c:\n\ttrue\ncheck-d:\n\ttrue\nfuzz:\n\ttrue\ntest-wx:\n\ttrue\n"
CI = "steps:\n  - run: make test-a\n  - run: make test-b\n  - run: make audit-c\n  - run: make check-d\n"
REG = "### K4 - w\n\n- **Status:** OPEN\n"


def tree(mk=MK, ci=CI, reg=REG):
    r = tempfile.mkdtemp()
    os.makedirs(os.path.join(r, ".github", "workflows"))
    os.makedirs(os.path.join(r, "docs"))
    open(os.path.join(r, "Makefile"), "w").write(mk)
    open(os.path.join(r, ".github", "workflows", "ci.yml"), "w").write(ci)
    open(os.path.join(r, "docs", "DEGRADATION.md"), "w").write(reg)
    return r


def problems(r):
    p = ct.check(r)[0]
    shutil.rmtree(r)
    return p


check("a fully wired tree passes (test-wx skipped under an open K4)", problems(tree()) == [])
check("a check target no workflow runs fails", any("test-b" in p for p in problems(tree(ci=CI.replace("  - run: make test-b\n", "")))))
check("a skip whose register entry is CLOSED fails", any("K4, which is CLOSED" in p for p in problems(tree(reg=REG.replace("OPEN", "CLOSED (x)")))))
check("a skip whose register entry is missing fails", any("missing" in p for p in problems(tree(reg=""))))
check("a skipped target that CI now runs must leave SKIPPED", any("remove it" in p for p in problems(tree(ci=CI + "  - run: make test-wx\n"))))
check("a workflow step for an undefined target fails", any("test-zz" in p for p in problems(tree(ci=CI + "  - run: make test-zz\n"))))
check("a .PHONY entry without a rule fails", any("test-q" in p for p in problems(tree(mk=MK.replace("fuzz test-wx", "fuzz test-wx test-q")))))
check("non-check targets (fuzz) need no step", problems(tree()) == [])
# finding 4 (Linux review of batch A): only real `run:` steps count, outside YAML comments, and they must be able to fail the job
check("finding 4: a commented-out step does not count (the reviewer's repro)", any("test-b" in p for p in problems(tree(ci=CI.replace("  - run: make test-b\n", "  # - run: echo skipped # make test-b\n")))))
check("finding 4: a whole commented-out make line does not count", any("test-b" in p for p in problems(tree(ci=CI.replace("  - run: make test-b\n", "  # - run: make test-b\n")))))
check("finding 4: a text that only mentions make does not count", any("test-b" in p for p in problems(tree(ci=CI.replace("make test-b", "echo make test-b")))))
check("finding 4: continue-on-error: true fails the target", any("test-b" in p and "continue-on-error" in p for p in problems(tree(ci=CI.replace("  - run: make test-b\n", "  - run: make test-b\n    continue-on-error: true\n")))))
check("finding 4: `|| true` fails the target", any("test-b" in p and "|| true" in p for p in problems(tree(ci=CI.replace("make test-b", "make test-b || true")))))
check("finding 4: `if: false` fails the target", any("test-b" in p and "if: false" in p for p in problems(tree(ci=CI.replace("  - run: make test-b\n", "  - if: false\n    run: make test-b\n")))))
check("finding 4: a block `run: |` with make lines counts", problems(tree(ci="steps:\n  - run: |\n      make test-a\n      make test-b && make audit-c\n      make check-d\n")) == [])
check("finding 4: a trailing comment after the command is fine", problems(tree(ci=CI.replace("make test-b", "make test-b  # the b suite"))) == [])
check("finding 4: continue-on-error: false is fine", problems(tree(ci=CI.replace("  - run: make test-b\n", "  - run: make test-b\n    continue-on-error: false\n"))) == [])
check("the real tree passes", ct.check(os.path.dirname(HERE))[0] == [])

print("ci-target tests: %d failed" % len(fails) if fails else "ci-target tests: all passed")
sys.exit(1 if fails else 0)
