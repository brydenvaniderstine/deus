#!/usr/bin/env python3
"""Tests of tools/check_ci_targets.py (step 20c) on scratch trees. Run from the repo root: python3 tools/check_ci_targets_test.py (make test-ci-targets-script)"""
import os, shutil, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_ci_targets as ct

fails = []
ct.SKIPPED = {"test-wx": "K4"}   # the real table is empty since step 20h; the synthetic trees below still exercise the skip rules


def check(name, cond):
    print(("PASS: " if cond else "FAIL: ") + name)
    if not cond:
        fails.append(name)


MK = ".PHONY: all test-a test-b audit-c check-d fuzz test-wx\n\nall:\n\ttrue\ntest-a:\n\ttrue\ntest-b:\n\ttrue\naudit-c:\n\ttrue\ncheck-d:\n\ttrue\nfuzz:\n\ttrue\ntest-wx:\n\ttrue\n"
CI = "jobs:\n  one:\n    runs-on: x\n    env:\n      MANNAC_WERROR: -Werror\n    steps:\n  - run: make test-a\n  - run: make test-b\n  - run: make audit-c\n  - run: make check-d\n"
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
# -Werror (step 20f-5)
check("-Werror: a host compile without MANNAC_WERROR fails", any("without $(MANNAC_WERROR)" in p for p in problems(tree(mk=MK + "x:\n\t$(MANNAC_CC) -std=c11 -Wall -o a b.c\n"))))
check("-Werror: a host compile with MANNAC_WERROR passes", problems(tree(mk=MK + "x:\n\t$(MANNAC_CC) -std=c11 -Wall $(MANNAC_WERROR) -o a b.c\n")) == [])
check("-Werror: a job that does not set MANNAC_WERROR: -Werror fails", any("job 'one' does not set" in p for p in problems(tree(ci=CI.replace("      MANNAC_WERROR: -Werror\n", "")))))
ct.SKIPPED = {}
check("the real tree passes (nothing is skipped any more)", ct.check(os.path.dirname(HERE))[0] == [])

# the guard on every step (step 22e, round-4 finding 11): one failing step must not hide the steps after it
ct.SKIPPED = {"test-wx": "K4"}   # the synthetic trees again
GUARDED = ("jobs:\n  one:\n    runs-on: x\n    env:\n      MANNAC_WERROR: -Werror\n    steps:\n      - uses: actions/checkout@v4\n        with:\n          fetch-depth: 0\n\n"
           "      - name: Toolchain\n        run: sudo apt-get install -y gcc\n      - run: make test-a\n        if: ${{ !cancelled() }}\n      - run: make test-b\n        if: ${{ !cancelled() }}\n"
           "      - run: make audit-c\n        if: ${{ !cancelled() }}\n      - run: make check-d\n        if: ${{ !cancelled() }}\n      - name: Upload\n        if: failure()\n        uses: actions/upload-artifact@v4\n")
check("finding 11: a job whose steps all carry an if: passes (checkout and Toolchain at the start are setup)", problems(tree(ci=GUARDED)) == [])
check("finding 11: a step without an if: fails (the seeded defect)", any("make test-b" in p and "no `if:`" in p for p in problems(tree(ci=GUARDED.replace("      - run: make test-b\n        if: ${{ !cancelled() }}\n", "      - run: make test-b\n")))))
check("finding 11: a step with its own failure()/always() is accepted", problems(tree(ci=GUARDED.replace("if: failure()", "if: always()"))) == [])
check("finding 11: a Toolchain step that is NOT at the start needs an if:", any("Toolchain" in p for p in problems(tree(ci=GUARDED.replace("      - run: make test-a\n", "      - run: make test-a\n        if: ${{ !cancelled() }}\n      - name: Toolchain again\n        run: true\n      - run: make test-x\n", 1)))))
check("finding 11: a later checkout is not exempt", any("checkout" in p for p in problems(tree(ci=GUARDED.replace("      - run: make test-b\n", "      - uses: actions/checkout@v4\n      - run: make test-b\n", 1)))))
check("finding 11: a list inside a step (with:) is not a step", problems(tree(ci=GUARDED.replace("          fetch-depth: 0\n", "          fetch-depth: 0\n          sparse-checkout: |\n            a\n"))) == [])
print("ci-target tests: %d failed" % len(fails) if fails else "ci-target tests: all passed")
sys.exit(1 if fails else 0)
