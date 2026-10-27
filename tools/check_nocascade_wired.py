#!/usr/bin/env python3
"""The no-cascade proof is wired into CI as a whole (typed-IR step 21j; the step 21 plan, "CI rules"; decision D4). Run from the repo root:
  python3 tools/check_nocascade_wired.py [root]      (make check-nocascade-wired)

`make nocascade-all` is the list of the proof's checks. This fails when
  W1  a Makefile check or test target that belongs to the proof (its name holds "nocascade" or "inject", or it is check-poison-paths or check-generated; the *-script tests
      of the tools are not the proof) is not a prerequisite of nocascade-all, so a new part of the proof cannot be added to CI and left out of the aggregate;
  W2  a prerequisite of nocascade-all is run by no step of .github/workflows/ci.yml, or only by a step that cannot fail the job (continue-on-error, `if: false`, `|| true`);
  W3  one of the three expensive parts (the ablation proof, the sink cross-check, the injection sweep) is not in the job named `nocascade`: D4, they are never sampled and never
      folded into a job they would slow;
  W4  the `nocascade` job's timeout-minutes is missing, or is less than twice the measured run time recorded in tests/nocascade/JOB_TIME (seconds, from the hosted runner:
      8 min 07 s = 487 on the run of a4faf02), so a slow runner shows as a failure of the budget, not a silent cut-off.
Hard failure, never a skip: a missing Makefile, workflow or `nocascade` job is a problem, not a pass."""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import check_ci_targets as ct

EXPENSIVE = ("check-nocascade-prove", "check-nocascade-sinks", "test-nocascade-inject")
JOB = "nocascade"


def read(root, path):
    try:
        with open(os.path.join(root, path), encoding="utf-8") as f:
            return f.read()
    except OSError:
        return ""


def aggregate(mk):
    m = re.search(r"^nocascade-all:((?:[^\n]*\\\n)*[^\n]*)", mk, re.M)
    return set(m.group(1).replace("\\\n", " ").split()) if m else None


def proof_targets(mk):
    defined = set(re.findall(r"^([A-Za-z0-9_.-]+):(?!=)", mk, re.M))
    out = set()
    for t in defined:
        if not t.startswith(("check-", "test-")) or t.endswith("-script"):
            continue
        if "nocascade" in t or "inject" in t or t in ("check-poison-paths", "check-generated"):
            out.add(t)
    return out


def jobs_of(ci):
    """{job id: text}"""
    out = {}
    for j in re.split(r"^  (?=[a-z][a-z0-9_-]*:\s*$)", ci, flags=re.M):
        m = re.match(r"([a-z][a-z0-9_-]*):", j)
        if m and "runs-on:" in j:
            out[m.group(1)] = j
    return out


def runs_by_job(jobs):
    """{job: {target that a step can fail the job on}}, and {target: [why not]} for the steps that cannot"""
    ok, bad = {}, {}
    for name, text in jobs.items():
        ok[name] = set()
        for st in ct.steps(text):
            shell = ct.run_text(st)
            if shell is None:
                continue
            targets, why = ct.make_targets(shell)
            body = "\n".join(st)
            if re.search(r"^\s*(- )?continue-on-error:\s*true\b", body, re.M):
                why.append("continue-on-error: true")
            if re.search(r"^\s*(- )?if:\s*false\s*$", body, re.M):
                why.append("if: false")
            for t in targets:
                if why:
                    bad.setdefault(t, why)
                else:
                    ok[name].add(t)
    return ok, bad


def check(root):
    problems = []
    mk = read(root, "Makefile")
    ci = read(root, ".github/workflows/ci.yml")
    if not mk or not ci:
        return ["the Makefile or .github/workflows/ci.yml is missing"]
    agg = aggregate(mk)
    if agg is None:
        return ["the Makefile has no nocascade-all target"]
    for t in sorted(proof_targets(mk) - agg):
        problems.append("W1: `make %s` belongs to the proof but is not a prerequisite of nocascade-all" % t)
    jobs = jobs_of(ci)
    ok, bad = runs_by_job(jobs)
    ran = set().union(*ok.values()) if ok else set()
    for t in sorted(agg):
        if t not in ran:
            problems.append("W2: nocascade-all's `%s` is run by no CI step%s" % (t, " that can fail the job (%s)" % ", ".join(bad[t]) if t in bad else ""))
    if JOB not in jobs:
        problems.append("W3: ci.yml has no job named `%s`" % JOB)
    else:
        for t in EXPENSIVE:
            if t not in ok[JOB]:
                problems.append("W3: `make %s` is not a step of the `%s` job (D4: the expensive parts of the proof have their own job, never sampled)" % (t, JOB))
        m = re.search(r"^\s+timeout-minutes:\s*(\d+)\s*$", jobs[JOB], re.M)
        meas = read(root, "tests/nocascade/JOB_TIME").strip()
        if not m:
            problems.append("W4: the `%s` job has no timeout-minutes" % JOB)
        elif not re.fullmatch(r"\d+", meas):
            problems.append("W4: tests/nocascade/JOB_TIME is missing or not a number of seconds")
        elif int(m.group(1)) * 60 < 2 * int(meas):
            problems.append("W4: the `%s` job's timeout (%s min) is less than twice its measured run time (%s s)" % (JOB, m.group(1), meas))
    return problems


def main(argv):
    root = argv[1] if len(argv) > 1 else ROOT
    problems = check(root)
    if problems:
        print("check_nocascade_wired: FAILED")
        for p in problems:
            print("  " + p)
        return 1
    mk = read(root, "Makefile")
    print("check_nocascade_wired: ok (%d parts of the proof, all run by CI; the %d expensive ones in the `%s` job)" % (len(aggregate(mk)), len(EXPENSIVE), JOB))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
