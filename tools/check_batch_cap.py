#!/usr/bin/env python3
"""The batch cap (step 20i; CLAUDE.md "A batch is at most 3 steps, then STOP"; register R1): a push carries at most three distinct STEPS.

  tools/check_batch_cap.py [BASE]      BASE defaults to $BATCH_BASE, else origin/main. BASE = last-green: the head of the last SUCCESSFUL ci.yml run on main (gh)

The commits in BASE..HEAD are read; a commit names its step in its subject ("Step 20f-3: ...", "IR 19c-6: ...": the step id is the number and letter, 20f, 19c).
Rules (Linux review of batch C, finding 2):
  - every commit that changes a path outside docs/ and *.md must carry a step id in its subject, or the check FAILS (a commit cannot escape the count by its wording);
    commits that change only docs/ and *.md need none;
  - more than three distinct ids fail, listing the commits per step;
  - the base is the head of the last successful ci.yml run on main, not github.event.before (a cancelled run's head would hide the commits of the push it replaced);
    it must be an ancestor of HEAD;
  - no base found (an all-zero base, no successful run, gh failing, an unknown commit) FAILS: there is no "not checked" exit 0.
What this cannot check: that the batch was REPORTED and APPROVED before the next one began (that stays a review rule, register R1, each tag).
"""
import json, os, re, subprocess, sys

STEP = re.compile(r"^(?:Step|IR) (\d+[a-z]?)(?=[ :\-(])")
CAP = 3


def is_docs(path):
    return path.startswith("docs/") or path.endswith(".md")


def last_green_base(root=None):
    """(sha, message): the head of the last successful ci.yml run on main, or (None, why)."""
    try:
        r = subprocess.run(["gh", "run", "list", "--workflow", "ci.yml", "--branch", "main", "--status", "success", "-L", "1", "--json", "headSha"],
                           capture_output=True, text=True, cwd=root)
    except OSError as e:
        return None, "cannot run gh: %s" % e
    if r.returncode != 0:
        return None, "gh run list failed: %s" % r.stderr.strip()[:200]
    try:
        runs = json.loads(r.stdout)
    except ValueError:
        return None, "gh run list printed no JSON: %s" % r.stdout[:100]
    if not runs or not re.fullmatch(r"[0-9a-f]{40}", runs[0].get("headSha", "")):
        return None, "no successful ci.yml run on main was found"
    return runs[0]["headSha"], "last successful ci.yml run on main"


def check(base, root=None):
    """(ok, message, steps): ok is True or False, never None - a base that cannot be found is a failure"""
    if not base or re.fullmatch(r"0+", base):
        return False, "no base: BASE is empty or all zeros, so the batch cap cannot be checked, and an unchecked push is a failure", {}
    if subprocess.run(["git", "rev-parse", "--verify", "-q", base + "^{commit}"], capture_output=True, cwd=root).returncode != 0:
        return False, "base %s is not a commit of this checkout (fetch-depth 0?)" % base[:40], {}
    if subprocess.run(["git", "merge-base", "--is-ancestor", base, "HEAD"], capture_output=True, cwd=root).returncode != 0:
        return False, "base %s is not an ancestor of HEAD" % base[:12], {}
    r = subprocess.run(["git", "log", "--format=%x01%h %s", "--name-only", "%s..HEAD" % base], capture_output=True, text=True, cwd=root)
    if r.returncode != 0:
        return False, "git log %s..HEAD failed: %s" % (base, r.stderr.strip()[:200]), {}
    steps, nameless = {}, []
    for chunk in r.stdout.split("\x01"):
        if not chunk.strip():
            continue
        head, *files = chunk.split("\n")
        files = [f for f in files if f.strip()]
        h, _, subject = head.partition(" ")
        m = STEP.match(subject)
        if m:
            steps.setdefault(m.group(1), []).append("%s %s" % (h, subject[:70]))
        else:
            code = [f for f in files if not is_docs(f)]
            if code:
                nameless.append("%s %s  (changes %s%s)" % (h, subject[:60], code[0], " and %d more" % (len(code) - 1) if len(code) > 1 else ""))
    msg = "%d distinct step(s) in %s..HEAD (cap %d): %s" % (len(steps), base[:12], CAP, ", ".join(sorted(steps)) or "none")
    if nameless:
        return False, msg + "; %d commit(s) change non-docs paths without a step id ('Step 20x:' / 'IR 19x:')" % len(nameless), dict(steps, **{"?": nameless})
    return len(steps) <= CAP, msg, steps


def main(argv):
    base = argv[1] if len(argv) > 1 else (os.environ.get("BATCH_BASE") or "origin/main")
    if base == "last-green":
        base, why = last_green_base()
        if base is None:
            print("check_batch_cap: FAILED: %s: the batch cap cannot be checked" % why)
            return 1
        print("check_batch_cap: base %s (%s)" % (base[:12], why))
    ok, msg, steps = check(base)
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
