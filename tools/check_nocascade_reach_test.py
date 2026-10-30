#!/usr/bin/env python3
"""Tests of tools/check_nocascade_reach.py (typed-IR step 22k, round-4 finding 9) with fake builders. make test-nocascade-reach-script"""
import os, shutil, stat, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_nocascade_reach as cr

fails = []


def check(name, cond):
    print(("PASS: " if cond else "FAIL: ") + name)
    if not cond:
        fails.append(name)


PROG = "manna v0;\nfn g(x: Nope) -> u8 {\n    return 0;\n}\nfn f(p: P) -> u8 {\n    let r = nope + 1;\n    return 0;\n}\n"
d = tempfile.mkdtemp(prefix="reach_test_")


def fake(name, body):
    p = os.path.join(d, name)
    open(p, "w").write("#!/bin/sh\n" + body + "\n")
    os.chmod(p, os.stat(p).st_mode | stat.S_IEXEC)
    return p


def tree(rows):
    """rows: [(name, reach, program, golden)]"""
    t = tempfile.mkdtemp(prefix="reach_tree_", dir=d)
    with open(os.path.join(t, "INDEX.tsv"), "w") as f:
        f.write("# header\n")
        for name, reach, prog, golden in rows:
            f.write("\t".join([name, "H", "v", "p", "1", "x", reach]) + "\n")
            open(os.path.join(t, name + ".mn"), "w").write(prog)
            open(os.path.join(t, name + ".ir"), "w").write(golden)
    return t


try:
    check("the unrelated mistake goes in as the first statement of f, on the line after its signature", cr.with_unrelated_mistake(PROG)[1] == 6 and cr.with_unrelated_mistake(PROG)[0].split("\n")[5] == cr.UNRELATED)
    check("a program with no `fn f(` has nowhere to put it", cr.with_unrelated_mistake("manna v0;\n") is None)
    # a live builder: the producer's own mistake (line 6 once the line was added: it was on 6, now on 7) and the unrelated one on line 6
    live = fake("live", "grep -q unrelated_name \"$1\" && { echo ERRORS:; echo \"6:21 E0230 unknown name 'unrelated_name'\"; echo \"7:13 E0230 unknown name 'nope'\"; }")
    golden_body = "ERRORS:\n6:13 E0230 unknown name 'nope'\n"
    t = tree([("b1", "body", PROG, golden_body)])
    check("a body golden whose body is checked passes: the added mistake is reported beside the producer's, and the producer's own line is shifted", cr.check(live, t)[0] == [])
    # a dead builder: the declaration error stops it, so the body mistake never shows (the base of round 4)
    dead = fake("dead", "echo ERRORS:; echo \"2:9 E0220 unknown type 'Nope'\"")
    t = tree([("b2", "body", PROG, "ERRORS:\n2:9 E0220 unknown type 'Nope'\n")])
    pr = cr.check(dead, t)[0]
    check("a body golden whose body is never checked FAILS and says so (the 46 dead consumers of the base)", len(pr) == 1 and "NOT reported" in pr[0] and "b2" in pr[0])
    t = tree([("d1", "decl", PROG, "ERRORS:\n2:9 E0220 unknown type 'Nope'\n")])
    check("a decl golden passes when the declaration error stops the build (output unchanged)", cr.check(dead, t)[0] == [])
    check("a decl golden FAILS when its body becomes live", any("its body is now checked" in p for p in cr.check(live, tree([("d2", "decl", PROG, "ERRORS:\n2:9 E0220 unknown type 'Nope'\n")]))[0]))
    # the producer's diagnostics must survive the added mistake
    swapped = fake("swapped", "grep -q unrelated_name \"$1\" && { echo ERRORS:; echo \"6:21 E0230 unknown name 'unrelated_name'\"; echo \"7:13 E0232 unknown function 'nope'\"; }")
    check("a body golden whose own diagnostic changes when a mistake is added fails", any("changed the golden's own diagnostics" in p for p in cr.check(swapped, tree([("b3", "body", PROG, golden_body)]))[0]))
    crash = fake("crash", "echo boom 1>&2; exit 3")
    check("a builder that dies is a failure naming the golden", any("b4" in p and "died" in p for p in cr.check(crash, tree([("b4", "body", PROG, golden_body)]))[0]))
    # a row with no reach column claims a body consumer
    t = tree([("b5", "body", PROG, golden_body)])
    lines = open(os.path.join(t, "INDEX.tsv")).read().split("\n")
    open(os.path.join(t, "INDEX.tsv"), "w").write("\n".join(l.rsplit("\t", 1)[0] if l.startswith("b5") else l for l in lines))
    check("a row that says nothing about reach claims a body consumer", cr.rows(t) == [{"name": "b5", "reach": "body"}])
    # the real tree: every golden is classified, the decl ones are exactly the three declaration holes
    real = os.path.join(os.path.dirname(HERE), "tests", "nocascade")
    rr = cr.rows(real)
    idx = [l.split("\t") for l in open(os.path.join(real, "INDEX.tsv")).read().split("\n") if l.strip() and not l.startswith("#")]
    decl = [r for r in idx if r[6] == "decl"]
    check("real tree: %d goldens, every one says body or decl" % len(rr), len(rr) >= 449 and all(r[6] in ("body", "decl") for r in idx))
    check("real tree: the decl goldens are the 46 of the three declaration holes, each named for what it tests", len(decl) == 46 and {r[1] for r in decl} == {"Field.type", "Decl.function_decl.return_type", "Type.slice.elem"} and all("declaration_stops_bodies_" in r[0] for r in decl))
    check("real tree: no body golden is on a declaration hole", not [r for r in idx if r[6] == "body" and r[1] in ("Field.type", "Decl.function_decl.return_type", "Type.slice.elem")])
finally:
    shutil.rmtree(d, ignore_errors=True)

print("\n%d failure(s)" % len(fails) if fails else "\nall passed")
sys.exit(1 if fails else 0)
