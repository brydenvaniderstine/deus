#!/usr/bin/env python3
"""The CI ratchet (step 20a): counts of tests only go up.

CLAUDE.md "No degradation": a test, golden, fix case, repro or diagnostic count never decreases unless the register lists why. This script
computes the counts and compares them with the baseline.

  tools/ratchet.py check [--base REF]   compute the counts; fail on a decrease against the baseline, or if tests/ratchet.json is not
                                         exactly the computed counts (a raise must be committed with `update`)
  tools/ratchet.py update               write tests/ratchet.json from the computed counts (refuses to lower anything; a waived
                                         decrease is lowered only when its RATCHET-WAIVER line is in docs/DEGRADATION.md)

The baseline is tests/ratchet.json as committed at REF (CI passes the push's `before` commit in $RATCHET_BASE; locally the default is
origin/main), so lowering the committed file in the same push does not help. With no usable REF (a new branch, an all-zero `before`, a
commit without the file) it falls back to the committed file at HEAD and SAYS SO.

Counted (all "never decrease"): the golden / case / repro inputs per directory; the test functions of each unit-test file; the documented
error codes and those with a fix-it; the cases of the memory stress test; the numbers in the two expected-count files (programs, fixes offered, fixes changed); and, per file, the
number of diagnostics in every expected output under tests/ (the da9f01c rule: a golden that loses a diagnostic is a regression, not a
re-baseline). One metric goes the other way: the written reasons in tools/audit_ir_poison_reasons.txt may not grow ("max" metrics).

A decrease is allowed only with a line in docs/DEGRADATION.md of the form
    RATCHET-WAIVER <metric or path> <from> -> <to> <register entry id>
where the entry exists in the register. A test file that moved is not a decrease: its content hash must still be present under another path.
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
    for f in files("tools/mannac/*_test.c"):
        metrics["unit_test_functions." + os.path.basename(f)] = len(re.findall(r"^static void test_\w+\(", read(f), re.M))
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
    metrics["memory_stress_cases"] = len(re.findall(r"^case\(", read("tools/ir_memory_stress.py"), re.M))
    reasons = [l for l in read("tools/audit_ir_poison_reasons.txt").split("\n") if l.strip() and not l.startswith("#")]
    metrics["audit_poison_reasons"] = len(reasons)
    for pat in ("tests/ir/*.ir", "tests/ir_fix/*.remaining", "tests/parser/*.ast", "tests/audit_repros/*.out"):
        for f in files(pat):
            text = read(f)
            diags[f] = [count_diags(text), hashlib.sha256(text.encode("utf-8", "replace")).hexdigest()[:16]]
    return {"metrics": metrics, "diags": diags}


def waivers():
    """{(name, from, to): entry id} from the register, only for entries that exist"""
    text = read(REGISTER)
    ids = set(re.findall(r"^### (\S+) -", text, re.M)) | {"7a", "7b"}
    out = {}
    for m in re.finditer(r"^RATCHET-WAIVER (\S+) (\d+) -> (\d+) (\S+)\s*$", text, re.M):
        if m.group(4) in ids:
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
    cur_hashes = {v[1] for v in cur["diags"].values()}
    for path, (n, h) in sorted(base["diags"].items()):
        now = cur["diags"].get(path)
        if now is None:
            if h in cur_hashes:   # the file moved
                continue
            if (path, n, 0) not in waived:
                problems.append("%s was removed (it held %d diagnostics)" % (path, n))
        elif now[0] < n and (path, n, now[0]) not in waived:
            problems.append("%s lost diagnostics: %d -> %d" % (path, n, now[0]))
    return problems


def git(*args):
    return subprocess.run(["git", "-C", ROOT] + list(args), capture_output=True, text=True)


def load_base(ref):
    """(baseline dict, description of where it came from)"""
    if ref and not re.fullmatch(r"0+", ref):
        r = git("show", "%s:%s" % (ref, BASELINE))
        if r.returncode == 0:
            return json.loads(r.stdout), "%s at %s" % (BASELINE, ref[:12])
    why = "no base ref" if not ref else ("an all-zero base ref (a new branch)" if re.fullmatch(r"0+", ref) else "%s has no %s" % (ref[:12], BASELINE))
    return json.loads(read(BASELINE)), "FALLBACK: the committed %s at HEAD (%s); a decrease relative to the previous push is NOT checked" % (BASELINE, why)


def main(argv):
    cmd = argv[1] if len(argv) > 1 else "check"
    cur = compute()
    if cmd == "update":
        waived = waivers()
        try:
            old = json.loads(read(BASELINE))
        except OSError:
            old = {"metrics": {}, "diags": {}}
        problems = compare(old, cur, waived)
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
    ref = os.environ.get("RATCHET_BASE") or (argv[argv.index("--base") + 1] if "--base" in argv else "origin/main")
    base, where = load_base(ref)
    print("ratchet: baseline = " + where)
    waived = waivers()
    problems = compare(base, cur, waived)
    committed = json.loads(read(BASELINE))
    if committed != cur and not problems:
        problems.append("%s is not the computed counts (a test was added or changed): run `make ratchet-update` and commit it" % BASELINE)
    if problems:
        print("ratchet: FAILED")
        for p in problems:
            print("  " + p)
        return 1
    print("ratchet: ok (%d metrics, %d expected-output files; none went down)" % (len(cur["metrics"]), len(cur["diags"])))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
