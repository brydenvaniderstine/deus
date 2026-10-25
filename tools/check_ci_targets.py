#!/usr/bin/env python3
"""Every test target the Makefile defines is run by CI (step 20c; CLAUDE.md "Verify CI actually runs the tests you think it does").

A target is a *check* if its name starts with test-, audit- or check-. Each must appear as `make <target>` in a step of .github/workflows/ci.yml or
docs.yml, unless it is in SKIPPED below with a register entry that is still open (the entry says why and when it is wired; a closed entry
makes the skip stale and the check fails). A `make <target>` step in a workflow whose target the Makefile does not define fails too.
Run from the repo root: python3 tools/check_ci_targets.py [root]  (make check-ci-targets)"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIPPED = {"test-wx": "K4"}   # the QEMU W^X test: wired by step 20h
PREFIXES = ("test-", "audit-", "check-")
WORKFLOWS = (".github/workflows/ci.yml", ".github/workflows/docs.yml", ".github/workflows/fuzz.yml")


def read(root, path):
    try:
        with open(os.path.join(root, path)) as f:
            return f.read()
    except OSError:
        return ""


def check(root):
    problems = []
    mk = read(root, "Makefile")
    phony = set()
    for m in re.finditer(r"^\.PHONY:(.*(?:\\\n.*)*)", mk, re.M):
        phony |= set(m.group(1).replace("\\\n", " ").split())
    defined = set(re.findall(r"^([A-Za-z0-9_.-]+):(?!=)", mk, re.M))
    checks = sorted(t for t in phony if t.startswith(PREFIXES) and t in defined)
    for t in sorted(t for t in phony if t.startswith(PREFIXES) and t not in defined):
        problems.append("Makefile: %s is in .PHONY but has no rule" % t)
    ci_text = "\n".join(read(root, w) for w in WORKFLOWS)
    run = set()
    for m in re.finditer(r"make\s+([A-Za-z0-9_.-]+)", ci_text):
        run.add(m.group(1))
    reg = read(root, "docs/DEGRADATION.md")
    for t in checks:
        if t in run:
            if t in SKIPPED:
                problems.append("%s is run by CI but is still in SKIPPED (remove it)" % t)
            continue
        if t not in SKIPPED:
            problems.append("`make %s` is a check target that no workflow runs (add a step, or a SKIPPED entry tied to an open register entry)" % t)
            continue
        m = re.search(r"^### %s - .*?^- \*\*Status:\*\* (\S+)" % re.escape(SKIPPED[t]), reg, re.M | re.S)
        if not m or m.group(1).startswith("CLOSED"):
            problems.append("%s is skipped because of register entry %s, which is %s" % (t, SKIPPED[t], "missing" if not m else "CLOSED"))
    for t in sorted(run - defined - {"all"}):
        if t not in phony and t not in defined:
            problems.append("a workflow runs `make %s`, which the Makefile does not define" % t)
    return problems, checks, run


def main(argv):
    root = argv[1] if len(argv) > 1 else ROOT
    problems, checks, run = check(root)
    if problems:
        print("check_ci_targets: FAILED")
        for p in problems:
            print("  " + p)
        return 1
    print("check_ci_targets: ok (%d check targets; %d run by CI, %d skipped with an open register entry: %s)" %
          (len(checks), sum(t in run for t in checks), sum(t not in run for t in checks), ", ".join("%s (%s)" % (t, SKIPPED[t]) for t in sorted(SKIPPED) if t not in run) or "none"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
