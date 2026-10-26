#!/usr/bin/env python3
"""The CI ratchet (step 20a): counts of tests only go up.

CLAUDE.md "No degradation": a test, golden, fix case, repro or diagnostic count never decreases unless the register lists why. This script
computes the counts and compares them with the history of the baseline file. Waivers for a pair of committed versions are read from the register as
committed at the later commit of the pair; waivers for the working tree are read from the working tree's register.

  tools/ratchet.py check    replay the history of tests/ratchet.json: every consecutive pair of committed versions (the first commit that
                            has the file up to HEAD, then the working-tree file, then the counts computed from the tree) must not go down,
                            waivers aside; and the file must be exactly the computed counts (a raise is committed with `update`)
  tools/ratchet.py update   write tests/ratchet.json from the computed counts (refuses to lower anything; a waived decrease is lowered
                            only when its RATCHET-WAIVER line is in docs/DEGRADATION.md)

Why a replay and not "the previous push" (Linux review of batch A, findings 2 and 3): both workflows cancel a run when a newer push lands, and a
red run does not stop the next push, so a push compared only with its predecessor can hide a decrease the predecessor made. Replaying every
consecutive pair means every lowering ever committed fails until it is waived, whichever run was cancelled or red. There is no fallback:
the first committed version has nothing before it, and a missing, unreadable or shallow history FAILS.

Counted (all "never decrease"): the golden / case / repro inputs per directory; the unit-test functions of each unit-test file that are
actually CALLED (a defined test that nothing calls is a failure); the documented error codes and those with a fix-it; the cases of the memory
stress test; the numbers in the two expected-count files (programs, fixes offered, fixes changed); the checks of the script tests; and, per file,
the number of diagnostics in every expected output under tests/ (the da9f01c rule: a golden that loses a diagnostic is a regression, not a
re-baseline). One metric goes the other way: the written reasons in tools/audit_ir_poison_reasons.txt may not grow ("max" metrics).

A decrease is allowed only with a line in docs/DEGRADATION.md of the form
    RATCHET-WAIVER <metric or path> <from> -> <to> <register entry id>
where the entry exists in the register and is OPEN or PARTLY CLOSED (a closed entry permits nothing). A removed test file counts as MOVED only
if its content is at a path that is NOT in the baseline, and each new path accounts for at most one removed path (identical expected outputs
are common: 229 files share content in 80 groups, so "the hash is somewhere" would excuse any deletion).
"""
import glob, hashlib, json, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE = "tests/ratchet.json"
REGISTER = "docs/DEGRADATION.md"
MAX_METRICS = {"audit_poison_reasons"}   # may only go down


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8", errors="replace") as f:
        return f.read()


def count_diags(text):
    """diagnostics in an expected-output file: the `L:C ...` lines of its ERRORS: section"""
    n, inside = 0, False
    for line in text.split("\n"):
        if line == "ERRORS:":
            inside = True
        elif inside and re.match(r"\d+:\d+ ", line):
            n += 1
        elif inside:
            inside = False
    return n


def files(pattern):
    return sorted(os.path.relpath(p, ROOT) for p in glob.glob(os.path.join(ROOT, pattern)))


def compute():
    metrics, diags = {}, {}
    metrics["lexer_goldens"] = len(files("tests/lexer/*.mn"))
    metrics["parser_goldens"] = len(files("tests/parser/*.mn"))
    metrics["ir_goldens"] = len(files("tests/ir/*.mn"))
    cases = [f for f in files("tests/ir_fix/*.mn") if not re.search(r"\.fixed2?\.mn$", f)]
    metrics["ir_fix_cases"] = len(cases)
    metrics["audit_repro_inputs"] = len([f for f in files("tests/audit_repros/*.mn") if not f.endswith(".fixed.mn")])
    uncalled = []
    for f in files("tools/mannac/*_test.c"):
        text = read(f)
        defined = re.findall(r"^static void (test_\w+)\(", text, re.M)
        called = [d for d in defined if re.search(r"(?<!void )\b%s\(" % d, text)]
        uncalled += ["%s: %s is defined and never called" % (f, d) for d in defined if d not in called]
        metrics["unit_test_functions." + os.path.basename(f)] = len(called)
    total = with_fix = 0
    for line in read("docs/error_codes.md").split("\n"):
        m = re.match(r"\| `(E\d{4})` \|(.*)\|\s*$", line)
        if m:
            total += 1
            cells = line.strip().strip("|").split("|")
            with_fix += not cells[-1].strip().startswith("None")
    metrics["error_codes_documented"] = total
    metrics["error_codes_with_fix"] = with_fix
    ex = read("tests/e0261_exhaustive.expected").split("\n")[0]
    metrics["e0261_exhaustive.programs"] = int(re.search(r"(\d+) programs", ex).group(1))
    metrics["e0261_exhaustive.fixes_offered"] = int(re.search(r"(\d+) offered a fix", ex).group(1))
    for i, line in enumerate([l for l in read("tests/rename_property.expected").split("\n") if l.startswith("rename property test")]):
        metrics["rename_property.part%d.programs" % (i + 1)] = int(re.search(r"(\d+) random programs", line).group(1))
        metrics["rename_property.part%d.fixes_changed" % (i + 1)] = int(re.search(r"fixes changed (\d+)", line).group(1))
    for f in ("ratchet_test.py", "check_register_test.py", "check_ci_targets_test.py", "boot_check_test.py", "check_batch_cap_test.py", "check_retro_rules_test.py", "coverage_check_test.py", "poison_paths_test.py", "nocascade_inject_test.py", "nocascade_sinks_test.py", "gen_ir_nocascade_test.py", "check_generated_test.py", "nocascade_prove_test.py"):
        metrics["script_checks." + f] = len(re.findall(r"^\s*check\(", read("tools/" + f), re.M))
    # step 21g (F8): the no-cascade proof only grows. The SINK count is not ratcheted: removing a redundant guard after the D3 procedure legitimately lowers it, and
    # that is caught by PATHS.lock (F2) instead, which changes with it and must be regenerated and reviewed.
    metrics["generators"] = len(files("tools/gen_ir_*.py"))
    metrics["mistakes_sidecars"] = len(files("tests/ir/*.mistakes")) + len(files("tests/ir_fix/*.mistakes"))
    metrics["nocascade_goldens"] = len(files("tests/nocascade/*.mn"))
    metrics["nocascade_index_rows"] = len([l for l in read("tests/nocascade/INDEX.tsv").split("\n") if l.strip() and not l.startswith("#")])
    for line in read("tests/nocascade/sweep.expected").split("\n"):
        if line.strip():
            k, v = line.split()
            metrics["nocascade_sweep_" + k] = int(v)
    metrics["memory_stress_cases"] = len(re.findall(r"^case\(", read("tools/ir_memory_stress.py"), re.M))
    reasons = [l for l in read("tools/audit_ir_poison_reasons.txt").split("\n") if l.strip() and not l.startswith("#")]
    metrics["audit_poison_reasons"] = len(reasons)
    for pat in ("tests/ir/*.ir", "tests/ir_fix/*.remaining", "tests/parser/*.ast", "tests/audit_repros/*.out", "tests/nocascade/*.ir"):
        for f in files(pat):
            text = read(f)
            diags[f] = [count_diags(text), hashlib.sha256(text.encode("utf-8", "replace")).hexdigest()[:16]]
    return {"metrics": metrics, "diags": diags, "uncalled": uncalled}


def waivers(text=None):
    """{(name, from, to): entry id} from the register text (default: the working tree's), only for entries that exist and are OPEN or PARTLY CLOSED"""
    if text is None:
        text = read(REGISTER)
    open_ids = set()
    for m in re.finditer(r"^### (\S+) - .*?^- \*\*Status:\*\* ([^\n]*)", text, re.M | re.S):   # the first Status line after the heading is the entry's own
        if m.group(2).startswith(("OPEN", "PARTLY CLOSED")):
            open_ids.add(m.group(1))
    open_ids |= {"7a", "7b"}   # sections 7a/7b are permanent / open as a block
    out = {}
    for m in re.finditer(r"^RATCHET-WAIVER (\S+) (\d+) -> (\d+) (\S+)\s*$", text, re.M):
        if m.group(4) in open_ids:
            out[(m.group(1), int(m.group(2)), int(m.group(3)))] = m.group(4)
    return out


def compare(base, cur, waived):
    """The list of problems: every decrease (every increase, for a MAX metric) that no waiver covers."""
    problems = []
    for name, b in sorted(base["metrics"].items()):
        c = cur["metrics"].get(name)
        if c is None:
            c = 0
        worse = c > b if name in MAX_METRICS else c < b
        if worse and (name, b, c) not in waived:
            problems.append("metric %s went %s: %d -> %d" % (name, "up (it may only go down)" if name in MAX_METRICS else "down", b, c))
    # a removed path is MOVED only if its content is at a path that is new (not in the baseline), one removed path per new path
    new_by_hash = {}
    for path, (n, h) in sorted(cur["diags"].items()):
        if path not in base["diags"]:
            new_by_hash.setdefault(h, []).append(path)
    for path, (n, h) in sorted(base["diags"].items()):
        now = cur["diags"].get(path)
        if now is None:
            if new_by_hash.get(h):
                new_by_hash[h].pop()
                continue
            if (path, n, 0) not in waived:
                problems.append("%s was removed (it held %d diagnostics)" % (path, n))
        elif now[0] < n and (path, n, now[0]) not in waived:
            problems.append("%s lost diagnostics: %d -> %d" % (path, n, now[0]))
    return problems


def git(*args):
    return subprocess.run(["git", "-C", ROOT] + list(args), capture_output=True, text=True)


def history():
    """(list of (short commit, snapshot, full commit), problems): every committed version of tests/ratchet.json, oldest first"""
    problems = []
    if git("rev-parse", "--is-shallow-repository").stdout.strip() == "true":
        return [], ["the clone is shallow: the ratchet replays the whole history of %s (check out with fetch-depth: 0)" % BASELINE]
    log = git("log", "--format=%H", "--reverse", "--", BASELINE)
    if log.returncode != 0:
        return [], ["git log failed: %s" % log.stderr.strip()[:200]]
    versions = []
    for sha in log.stdout.split():
        r = git("show", "%s:%s" % (sha, BASELINE))
        if r.returncode != 0:
            problems.append("%s: %s is missing at this commit (deleted or renamed)" % (sha[:12], BASELINE))
            continue
        try:
            snap = json.loads(r.stdout)
            snap["metrics"], snap["diags"]
        except (ValueError, KeyError, TypeError):
            problems.append("%s: %s is unreadable at this commit" % (sha[:12], BASELINE))
            continue
        versions.append((sha[:12], snap, sha))
    return versions, problems


def main(argv):
    cmd = argv[1] if len(argv) > 1 else "check"
    cur = compute()
    uncalled = cur.pop("uncalled")
    waived = waivers()
    if cmd == "update":
        try:
            old = json.loads(read(BASELINE))
        except (OSError, ValueError):
            old = {"metrics": {}, "diags": {}}
        problems = compare(old, cur, waived) + uncalled
        if problems:
            print("ratchet update refused: these would lower the baseline without a RATCHET-WAIVER line in %s:" % REGISTER)
            for p in problems:
                print("  " + p)
            return 1
        with open(os.path.join(ROOT, BASELINE), "w") as f:
            json.dump(cur, f, indent=1, sort_keys=True)
            f.write("\n")
        print("ratchet: wrote %s (%d metrics, %d expected-output files)" % (BASELINE, len(cur["metrics"]), len(cur["diags"])))
        return 0
    versions, problems = history()
    problems = list(problems) + uncalled
    for (a, sa, _), (b, sb, later) in zip(versions, versions[1:]):
        # the waivers and the entry statuses are those of the register AS COMMITTED at the later commit of the pair (the commit that made the
        # decrease): closing an entry afterwards must not turn an old, waived lowering into a permanent failure (Linux review of batch A2, finding 7)
        r = git("show", "%s:%s" % (later, REGISTER))
        for p in compare(sa, sb, waivers(r.stdout if r.returncode == 0 else "")):
            problems.append("%s -> %s: %s" % (a, b, p))
    try:
        committed = json.loads(read(BASELINE))
        committed["metrics"], committed["diags"]
    except (OSError, ValueError, KeyError, TypeError):
        committed = None
        problems.append("%s is missing or unreadable in the working tree" % BASELINE)
    if committed is not None:
        if versions and versions[-1][1] != committed:
            for p in compare(versions[-1][1], committed, waived):
                problems.append("%s -> working tree: %s" % (versions[-1][0], p))
        for p in compare(committed, cur, waived):
            problems.append("%s -> the computed counts: %s" % (BASELINE, p))
        if committed != cur and not any(p.startswith(BASELINE) for p in problems):
            problems.append("%s is not the computed counts (a test was added or changed): run `make ratchet-update` and commit it" % BASELINE)
    print("ratchet: replayed %d committed version(s) of %s" % (len(versions), BASELINE))
    if problems:
        print("ratchet: FAILED")
        for p in problems:
            print("  " + p)
        return 1
    print("ratchet: ok (%d metrics, %d expected-output files; no version went down)" % (len(cur["metrics"]), len(cur["diags"])))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
