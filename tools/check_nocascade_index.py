#!/usr/bin/env python3
"""Rule F4/F5 of the no-cascade proof (typed-IR step 21g): tests/nocascade/INDEX.tsv, the goldens and the holes agree. Run from the repo root: python3 tools/check_nocascade_index.py [DIR]

INDEX.tsv is written by tools/gen_ir_nocascade.py: golden, hole, variant, producer, mistakes, the intended diagnostics, reach (body or decl: step 22k; tools/check_nocascade_reach.py runs it). This checks, without running the builder (make test-ir
and test-ir-asan run every tests/nocascade/*.mn against its .ir):
  - the .mn and .ir files of DIR are exactly the INDEX rows' (an unlisted golden or a missing one fails; sinks.json, PATHS.lock and the like are not goldens);
  - each .ir is `ERRORS:` and exactly the intended diagnostics: the golden says what the generator meant, so a golden edited to match the builder is caught here;
  - the number of diagnostics equals the declared number of mistakes (decision D5: one input, as many diagnostics as mistakes);
  - every hole of ast.h appears in the INDEX, or is an excluded hole with its reason in EXCLUDED.tsv; no row names a hole ast.h does not have (F5)."""
import os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def check(d, holes=None):
    problems = []
    idx = os.path.join(d, "INDEX.tsv")
    if not os.path.exists(idx):
        return ["%s is missing" % idx]
    rows = []
    for ln in open(idx).read().split("\n"):
        if ln.strip() and not ln.startswith("#"):
            f = ln.split("\t")
            if len(f) != 7:
                problems.append("INDEX row with %d fields (want 7): %s" % (len(f), ln[:80]))
                continue
            if f[6] not in ("body", "decl"):
                problems.append("%s: reach is %r (want body or decl)" % (f[0], f[6]))
            rows.append(f)
    names = [r[0] for r in rows]
    if len(names) != len(set(names)):
        problems.append("INDEX lists a golden twice")
    files = set(os.listdir(d))
    expected_files = {n + e for n in names for e in (".mn", ".ir")} | {"INDEX.tsv", "EXCLUDED.tsv"}
    for f in sorted(expected_files - files):
        problems.append("%s is listed (or required) but missing" % f)
    for f in sorted(f for f in files - expected_files if f.endswith((".mn", ".ir"))):   # the other files of the directory (sinks.json, PATHS.lock, ...) are not goldens
        problems.append("%s is in %s but not in the INDEX" % (f, d))
    for name, hole, variant, producer, mistakes, intended, reach in rows:
        ir = os.path.join(d, name + ".ir")
        if not os.path.exists(ir):
            continue
        diags = intended.split(" | ") if intended else []
        want = "\n".join(["ERRORS:"] + diags) + "\n"
        if open(ir).read() != want:
            problems.append("%s.ir differs from the intended diagnostics in the INDEX" % name)
        if len(diags) != int(mistakes):
            problems.append("%s: %d diagnostics intended for %s mistakes (D5: they must be equal)" % (name, len(diags), mistakes))
    if holes is not None:
        used = {r[1] for r in rows}
        excl = set()
        ex = os.path.join(d, "EXCLUDED.tsv")
        if os.path.exists(ex):
            excl = {ln.split("\t")[0] for ln in open(ex).read().split("\n") if ln.strip() and not ln.startswith("#") and ln.split("\t")[1] == "*"}
        for h in sorted(set(holes) - used - excl):
            problems.append("F5: hole %s has no golden in the INDEX and is not an excluded hole" % h)
        for h in sorted(used - set(holes)):
            problems.append("F5: the INDEX names hole %s, which ast.h does not have" % h)
    return problems


def main(argv):
    d = argv[0] if argv else os.path.join(ROOT, "tests", "nocascade")
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import nocascade_holes
    problems = check(d, nocascade_holes.derive())
    for p in problems:
        print("check_nocascade_index: " + p)
    if problems:
        return 1
    n = len([r for r in open(os.path.join(d, "INDEX.tsv")).read().split("\n") if r.strip() and not r.startswith("#")])
    print("check_nocascade_index: ok (%d goldens, every hole covered)" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
