#!/usr/bin/env python3
"""Tests of tools/check_retro_rules.py (step 20i) on a small scratch tree and on the real one. make test-retro-rules-script"""
import os, shutil, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_retro_rules as rr

fails = []


def check(name, cond):
    print(("PASS: " if cond else "FAIL: ") + name)
    if not cond:
        fails.append(name)


CLAUDE = """# x

## No degradation (hard rule)

- Nothing is stubbed, removed, withheld, weakened for progress.

**Fix-it policy.** text.

## Working conventions
- Small commits, one subsystem each, always.
  - A sub bullet that is a rule too, in full.

## Lessons

- **Verify CI actually runs the tests you think it does.** more text.
"""
REF = """# R

- **The rule.** Test harnesses and measurement probes must be leak-clean like product code.

## Retro-rule inventory (step 20i)

| Source | Anchor | Enforced by |
|---|---|---|
| CLAUDE.md | Nothing is stubbed, removed, withheld, weakened | make test-a |
| CLAUDE.md | Small commits, one subsystem each, always. | register R6 |
| CLAUDE.md | A sub bullet that is a rule too, in full. | make test-a; register R6 |
| CLAUDE.md | Verify CI actually runs the tests you think it does. | make test-b |
| docs/REFINEMENT.md | Test harnesses and measurement probes must be leak-clean | make test-b |
"""
MK = ".PHONY: test-a test-b test-c\ntest-a:\n\ttrue\ntest-b:\n\ttrue\ntest-c:\n\ttrue\n"
CI = "steps:\n  - run: make test-a\n  - run: make test-b\n"
REG = "### R6 - review rules\n\n- **Status:** OPEN\n\n### K9 - old\n\n- **Status:** CLOSED (x)\n"


def tree(claude=CLAUDE, ref=REF, mk=MK, ci=CI, reg=REG):
    r = tempfile.mkdtemp()
    os.makedirs(os.path.join(r, "docs"))
    os.makedirs(os.path.join(r, ".github", "workflows"))
    for p, t in (("CLAUDE.md", claude), ("docs/REFINEMENT.md", ref), ("Makefile", mk), (".github/workflows/ci.yml", ci), ("docs/DEGRADATION.md", reg)):
        open(os.path.join(r, p), "w").write(t)
    return r


def problems(**kw):
    r = tree(**kw)
    p, n, u, w = rr.check(r)
    shutil.rmtree(r)
    return p, n, u, w


p, n, u, w = problems()
check("a complete inventory passes: 5 rules, 1 unenforced, 1 partly (make + register), 3 enforced", p == [] and n == 5 and u == 1 and w == 1)
p, n, u, w = problems(claude=CLAUDE.replace("- Nothing is stubbed", "- Nothing is stubbed, and a new rule is added here.\n- Another rule that nobody put in the inventory.\n- Nothing is stubbed"))
check("a rule without a row fails", any("no row in the inventory" in x and "Another rule" in x for x in p))
p, n, u, w = problems(ref=REF.replace("| CLAUDE.md | Verify CI actually runs the tests you think it does. | make test-b |\n", ""))
check("a Lessons bullet whose row was deleted fails", any("Verify CI actually runs" in x for x in p))
p, n, u, w = problems(ref=REF.replace("Test harnesses and measurement probes must be leak-clean |", "Some anchor that is on no rule of the file, long enough |"))
check("an anchor on no rule fails (and the rule it replaced is uncovered)", any("is on no rule" in x for x in p) and any("leak-clean" in x for x in p))
p, n, u, w = problems(ref=REF.replace("make test-a |", "make test-zz |", 1))
check("a make target that does not exist fails", any("`make test-zz` is not a Makefile target" in x for x in p))
p, n, u, w = problems(ci="steps:\n  - run: make test-a\n")
check("a make target that no workflow runs fails", any("`make test-b` is run by no workflow" in x for x in p))
p, n, u, w = problems(ref=REF.replace("register R6", "register R7", 1))
check("a register entry that does not exist fails", any("register entry R7 does not exist" in x for x in p))
p, n, u, w = problems(ref=REF.replace("register R6", "register K9", 1))
check("a CLOSED register entry enforces nothing", any("K9 is CLOSED" in x for x in p))
p, n, u, w = problems(ref=REF.replace("| make test-a |\n| CLAUDE.md | Small", "| |\n| CLAUDE.md | Small"))
check("a row without an Enforced-by fails", p != [])
p, n, u, w = problems(ref=REF.replace("make test-a | \n", "make test-a | \n").replace("| make test-a |", "| run it sometimes |", 1))
check("an item that is neither make nor register fails", any("cannot read the item" in x for x in p))
p, n, u, w = problems(ref=REF.split("## Retro-rule inventory")[0])
check("a missing inventory fails", any("no '## Retro-rule inventory' table" in x for x in p))

real, n, u, w = rr.check(os.path.dirname(HERE))
check("the real inventory passes (%d rules, %d partly, %d unenforced)" % (n, w, u), real == [])
check("a row naming a make item AND a register entry is counted as partly, not as enforced", w > 0 and u + w >= 8)

print("retro-rule tests: %d failed" % len(fails) if fails else "retro-rule tests: all passed")
sys.exit(1 if fails else 0)
