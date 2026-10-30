#!/usr/bin/env python3
"""The consumer a no-cascade golden is named for is really reached (typed-IR step 22k; round-4 finding 9). Run from the repo root:
  python3 tools/check_nocascade_reach.py [IR_DUMP [DIR]]       (make check-nocascade-reach; IR_DUMP defaults to build/ir_dump/ir_dump, DIR to tests/nocascade)

A golden of tests/nocascade says: this value, poisoned by this one mistake, reaches this consumer, and the builder reports the mistake and nothing else. That claim is empty if the builder
never gets as far as the consumer: a declaration error stops the build before the bodies are checked (docs/M2_PLAN.md "Naming conventions"), so 46 goldens whose unknown type sat in a
parameter, a return type or a struct field never reached the `x + 1` they were named for (the auditor's dead-consumer test, round 4).

For every golden of INDEX.tsv this adds ONE unrelated mistake to the body of `f` (`let zz_unrelated = unrelated_name;`, an E0230, as the first statement) and runs the builder:
  reach `body`  the output must be the golden's diagnostics plus that one E0230 (a body that is never checked reports nothing for it: the consumer is dead);
  reach `decl`  the output must be the golden's diagnostics exactly (the declaration error stops the build, which is what the golden tests; if the bodies become reachable the golden
                must be reclassified `body` and given a body consumer).
A builder that dies, or prints something else, is a failure naming the golden. Hard failure, never a skip: a missing INDEX or builder is a problem, not a pass."""
import os, re, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIAG = re.compile(r"^(\d+):(\d+) (E\d{4}) (.*)$", re.M)
UNRELATED = "    let zz_unrelated = unrelated_name;"
UNRELATED_DIAG = "E0230 unknown name 'unrelated_name'"


def rows(d):
    out = []
    for ln in open(os.path.join(d, "INDEX.tsv")).read().split("\n"):
        if ln.strip() and not ln.startswith("#"):
            f = ln.split("\t")
            out.append({"name": f[0], "reach": f[6] if len(f) > 6 else "body"})   # a row that does not say claims a body consumer: the name of the golden is the claim
    return out


def with_unrelated_mistake(src):
    """the program with the unrelated mistake as the first statement of `f`, and the line it is on; None if there is no `fn f(`"""
    lines = src.split("\n")
    for i, l in enumerate(lines):
        if l.startswith("fn f(") and l.rstrip().endswith("{"):
            return "\n".join(lines[:i + 1] + [UNRELATED] + lines[i + 1:]), i + 2
    return None


def run(binary, path):
    r = subprocess.run([binary, path], capture_output=True, text=True, timeout=30)
    return r.returncode, r.stdout, r.stderr


def diags(text):
    return [(int(a), int(b), c, m) for a, b, c, m in DIAG.findall(text)]


def check(binary, d):
    problems = []
    n_body = n_decl = 0
    with tempfile.TemporaryDirectory(prefix="reach_") as tmp:
        for r in rows(d):
            src = open(os.path.join(d, r["name"] + ".mn")).read()
            golden = open(os.path.join(d, r["name"] + ".ir")).read()
            made = with_unrelated_mistake(src)
            if made is None:
                problems.append("%s: no `fn f(` line to put the unrelated mistake in" % r["name"])
                continue
            prog, at = made
            p = os.path.join(tmp, r["name"] + ".mn")
            open(p, "w").write(prog)
            rc, out, err = run(binary, p)
            if rc != 0 or err.strip():
                problems.append("%s: the builder died or complained on the program with the unrelated mistake (exit %d): %s" % (r["name"], rc, (err.strip().split("\n") or [""])[0][:80]))
                continue
            if r["reach"] == "decl":
                n_decl += 1
                if out != golden:
                    problems.append("%s (decl): its body is now checked (output changed with a mistake added to f): reclassify it `body` and give it a body consumer, or the declaration error no longer stops the build" % r["name"])
                continue
            n_body += 1
            want = {(l, c, code, m) for l, c, code, m in diags(golden)}
            got = diags(out)
            new = [x for x in got if x[2] + " " + x[3] == UNRELATED_DIAG]
            if len(new) != 1 or new[0][0] != at:
                problems.append("%s (body): the unrelated mistake in f was NOT reported (%d such diagnostics): the body this golden names its consumer in is never checked, so the golden proves nothing about it" % (r["name"], len(new)))
                continue
            rest = [x for x in got if x is not new[0]]
            shifted = {(l + 1 if l >= at else l, c, code, m) for l, c, code, m in want}
            if set(rest) != shifted or len(rest) != len(want):
                problems.append("%s (body): the unrelated mistake changed the golden's own diagnostics (a mistake that depends on another one is a cascade, or a mistake that only shows without it)" % r["name"])
    return problems, n_body, n_decl


def main(argv):
    binary = argv[0] if argv else os.path.join(ROOT, "build", "ir_dump", "ir_dump")
    d = argv[1] if len(argv) > 1 else os.path.join(ROOT, "tests", "nocascade")
    if not os.path.exists(binary):
        print("check_nocascade_reach: the builder %s does not exist (make check-nocascade-reach builds it)" % binary)
        return 1
    if not os.path.exists(os.path.join(d, "INDEX.tsv")):
        print("check_nocascade_reach: %s/INDEX.tsv is missing" % d)
        return 1
    problems, nb, nd = check(binary, d)
    if problems:
        print("check_nocascade_reach: FAILED (%d golden(s))" % len(problems))
        for p in problems[:60]:
            print("  " + p)
        if len(problems) > 60:
            print("  ... and %d more" % (len(problems) - 60))
        return 1
    print("check_nocascade_reach: ok (%d body goldens: an added body mistake is reported beside the producer's; %d decl goldens: a declaration error stops the build before the bodies, pinned)" % (nb, nd))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
