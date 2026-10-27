#!/usr/bin/env python3
"""The batch cap (step 20i; CLAUDE.md "A batch is at most 3 steps, then STOP"; register R1): a push carries at most three distinct STEPS.

  tools/check_batch_cap.py [BASE]      BASE defaults to $BATCH_BASE, else origin/main. BASE = last-run: the head of the newest ci.yml run on main whose jobs RAN
                                       to a conclusion, success OR failure (gh); cancelled runs and runs that never started (billing, no step ran) are skipped.

The commits in BASE..HEAD are read; a commit names its step in its subject ("Step 20f-3: ...", "IR 19c-6: ...": the step id is the number and letter, 20f, 19c).
Rules (Linux review of batch C, finding 2):
  - every commit that changes a path outside docs/ and *.md must carry a step id in its subject, or the check FAILS (a commit cannot escape the count by its wording);
    commits that change only docs/ and *.md need none;
  - more than three distinct ids fail, listing the commits per step;
  - the base is the head of the newest ci.yml run on main that ran (green or red), not github.event.before (a cancelled run's head would hide the commits of the push it
    replaced), and not only a green one (a red run's commits were already seen and reported; a push that follows a red run must not be charged for them again);
    it must be an ancestor of HEAD;
  - no base found (an all-zero base, no successful run, gh failing, an unknown commit) FAILS: there is no "not checked" exit 0.
What this cannot check: that the batch was REPORTED and APPROVED before the next one began (that stays a review rule, register R1, each tag).
"""
import json, os, re, subprocess, sys

STEP = re.compile(r"^(?:Step|IR) (\d+[a-z]?)(?=[ :\-(])")
CAP = 3


def is_docs(path):
    return path.startswith("docs/") or path.endswith(".md")


def _gh(args, root=None):
    """(parsed JSON, None) or (None, why)"""
    try:
        r = subprocess.run(["gh"] + args, capture_output=True, text=True, cwd=root)
    except OSError as e:
        return None, "cannot run gh: %s" % e
    if r.returncode != 0:
        return None, "gh %s failed: %s" % (" ".join(args[:2]), r.stderr.strip()[:200])
    try:
        return json.loads(r.stdout), None
    except ValueError:
        return None, "gh %s printed no JSON: %s" % (" ".join(args[:2]), r.stdout[:100])


def ran_to_a_conclusion(jobs):
    """True if some step of some job actually executed and finished (success or failure). A run that GitHub refused to start (billing, a runner outage) concludes
    `failure` with jobs that have no steps at all, and a cancelled run's steps are cancelled or skipped: neither says anything about the code."""
    return any(st.get("conclusion") in ("success", "failure") for j in jobs for st in j.get("steps", []))


def last_run_base(root=None, limit=30):
    """(sha, message): the head of the newest ci.yml run on main whose jobs RAN to a conclusion, green OR red; or (None, why).
    Skipped: a run still going, a cancelled one, a skipped one, and one that never started (conclusion failure, no step ever ran: the billing block of run 40546449691).
    A red run is a valid base because the batch cap counts what the push adds to the history Bryden has already seen run, not what passed; the steps of a red run were
    reported and are not new work."""
    runs, why = _gh(["run", "list", "--workflow", "ci.yml", "--branch", "main", "--status", "completed", "-L", str(limit), "--json", "databaseId,headSha,conclusion"], root)
    if runs is None:
        return None, why
    for run in runs:
        if run.get("conclusion") not in ("success", "failure"):
            continue
        if not re.fullmatch(r"[0-9a-f]{40}", run.get("headSha", "")):
            continue
        view, why = _gh(["run", "view", str(run["databaseId"]), "--json", "jobs"], root)
        if view is None:
            return None, why
        if ran_to_a_conclusion(view.get("jobs", [])):
            return run["headSha"], "newest ci.yml run on main whose jobs ran (%s)" % run["conclusion"]
    return None, "no ci.yml run on main (of the last %d) had jobs that ran to a conclusion" % limit


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
    if base == "last-run":
        base, why = last_run_base()
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
