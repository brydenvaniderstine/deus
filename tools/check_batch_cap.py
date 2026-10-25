#!/usr/bin/env python3
"""The batch cap (step 20i; CLAUDE.md "A batch is at most 3 steps, then STOP"; register R1): a push carries at most three distinct STEPS.

  tools/check_batch_cap.py [BASE]      BASE defaults to $BATCH_BASE, else origin/main

The commits in BASE..HEAD are read; a commit names its step in its subject ("Step 20f-3: ...", "IR 19c-6: ...": the step id is the number and letter, 20f, 19c). Commits
without a step id (docs, comments) are ignored. More than three distinct ids fail, listing the commits per step. What this cannot check: that the batch was REPORTED and
APPROVED before the next one began (that stays a review rule, register R1, each tag). An all-zero BASE (the first push) is printed as not checked.
"""
import os, re, subprocess, sys

STEP = re.compile(r"^(?:Step|IR) (\d+[a-z]?)(?=[ :\-(])")
CAP = 3


def check(base, root=None):
    if re.fullmatch(r"0+", base):
        return None, "BASE is all zeros (a first push): the batch cap is NOT checked", {}
    r = subprocess.run(["git", "log", "--format=%h %s", "%s..HEAD" % base], capture_output=True, text=True, cwd=root)
    if r.returncode != 0:
        return False, "git log %s..HEAD failed: %s" % (base, r.stderr.strip()[:200]), {}
    steps = {}
    for line in r.stdout.split("\n"):
        if not line.strip():
            continue
        h, _, subject = line.partition(" ")
        m = STEP.match(subject)
        if m:
            steps.setdefault(m.group(1), []).append("%s %s" % (h, subject[:70]))
    return len(steps) <= CAP, "%d distinct step(s) in %s..HEAD (cap %d): %s" % (len(steps), base[:12], CAP, ", ".join(sorted(steps)) or "none"), steps


def main(argv):
    base = argv[1] if len(argv) > 1 else (os.environ.get("BATCH_BASE") or "origin/main")
    ok, msg, steps = check(base)
    if ok is None:
        print("check_batch_cap: " + msg)
        return 0
    if not ok:
        print("check_batch_cap: FAILED: " + msg)
        for s in sorted(steps):
            for c in steps[s]:
                print("  [%s] %s" % (s, c))
        return 1
    print("check_batch_cap: ok (" + msg + ")")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
