#!/usr/bin/env python3
"""Tests of tools/check_generated.py and tools/check_nocascade_index.py (typed-IR step 21g) on scratch trees and on the real one. make test-check-generated-script"""
import os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import check_generated as cg
import check_nocascade_index as ci

fails = []


def check(name, cond):
    print(("PASS: " if cond else "FAIL: ") + name)
    if not cond:
        fails.append(name)


GEN = "import os\nopen('tests/ir/a.mn','w').write('A\\n')\nopen('tests/ir_fix/b.mistakes','w').write('2\\n')\nopen('tests/nocascade/c.ir','w').write('C\\n')\n"


def tree(gen=GEN):
    root = tempfile.mkdtemp(prefix="cg_test_")
    for d in ("tools", "tests/ir", "tests/ir_fix", "tests/nocascade"):
        os.makedirs(os.path.join(root, d))
    open(os.path.join(root, "tools", "gen_ir_x.py"), "w").write(gen)
    open(os.path.join(root, "tests/ir/a.mn"), "w").write("A\n")
    open(os.path.join(root, "tests/ir_fix/b.mistakes"), "w").write("2\n")
    open(os.path.join(root, "tests/nocascade/c.ir"), "w").write("C\n")
    open(os.path.join(root, "tests/ir/hand.mn"), "w").write("a golden nobody generates\n")
    return root


root = tree()
try:
    problems, ngen, nfiles = cg.run(root)
    check("a tree whose generated files equal the committed ones passes (and a hand-written golden is not flagged)", problems == [] and ngen == 1 and nfiles == 3)
    open(os.path.join(root, "tests/ir/a.mn"), "w").write("edited by hand\n")
    problems, _, _ = cg.run(root)
    check("a generated golden edited without its generator fails and is named", any("tests/ir/a.mn differs" in p for p in problems))
    open(os.path.join(root, "tests/ir/a.mn"), "w").write("A\n")
    os.remove(os.path.join(root, "tests/nocascade/c.ir"))
    problems, _, _ = cg.run(root)
    check("a generated file missing from the repository fails", any("tests/nocascade/c.ir is written by a generator but is not in the repository" in p for p in problems))
    open(os.path.join(root, "tests/nocascade/c.ir"), "w").write("C\n")
    open(os.path.join(root, "tests/ir/orphan.mistakes"), "w").write("2\n")
    problems, _, _ = cg.run(root)
    check("a .mistakes file no generator writes fails (the count must come from a generator, never from a hand)", any("tests/ir/orphan.mistakes is a mistake count that no generator writes" in p for p in problems))
    os.remove(os.path.join(root, "tests/ir/orphan.mistakes"))
    open(os.path.join(root, "tools", "gen_ir_y.py"), "w").write("import sys\nsys.exit(3)\n")
    problems, _, _ = cg.run(root)
    check("a generator that exits non-zero fails and is named", any("gen_ir_y.py exited 3" in p for p in problems))
    os.remove(os.path.join(root, "tools", "gen_ir_y.py"))
    r = subprocess.run([sys.executable, os.path.join(HERE, "check_generated.py"), root], capture_output=True, text=True)
    check("the command exits 0 on a good tree and prints the counts", r.returncode == 0 and "1 generators, 3 generated files" in r.stdout)
    open(os.path.join(root, "tests/ir/a.mn"), "w").write("edited\n")
    r = subprocess.run([sys.executable, os.path.join(HERE, "check_generated.py"), root], capture_output=True, text=True)
    check("the command exits 1 on a bad tree", r.returncode == 1)
finally:
    shutil.rmtree(root, ignore_errors=True)

# ---- the INDEX check ----
INDEX = "# header\n" + "\t".join(["g1", "Expr.unary.operand", "neg", "unknown_name", "1", "14:13 E0230 unknown name 'nope'", "body"]) + "\n" + \
        "\t".join(["g2", "Expr.binary.lhs", "add", "unknown_name", "2", "14:12 E0230 unknown name 'nope' | 14:20 E0230 unknown name 'x'", "body"]) + "\n"


def nc():
    d = tempfile.mkdtemp(prefix="nc_idx_")
    open(os.path.join(d, "INDEX.tsv"), "w").write(INDEX)
    open(os.path.join(d, "EXCLUDED.tsv"), "w").write("# x\nExpr.named_arg.name\t*\t*\twhy\n")
    for n, body in (("g1", "ERRORS:\n14:13 E0230 unknown name 'nope'\n"), ("g2", "ERRORS:\n14:12 E0230 unknown name 'nope'\n14:20 E0230 unknown name 'x'\n")):
        open(os.path.join(d, n + ".mn"), "w").write("manna v0;\n")
        open(os.path.join(d, n + ".ir"), "w").write(body)
    open(os.path.join(d, "PATHS.lock"), "w").write("{}\n")
    return d


HOLES = ["Expr.unary.operand", "Expr.binary.lhs", "Expr.named_arg.name"]
d = nc()
try:
    check("a consistent INDEX, goldens and holes pass (a non-golden file in the directory is ignored)", ci.check(d, HOLES) == [])
    open(os.path.join(d, "g1.ir"), "w").write("ERRORS:\n14:13 E0230 unknown name 'other'\n")
    check("a golden edited away from the intended diagnostics fails (F4)", any("g1.ir differs from the intended" in p for p in ci.check(d, HOLES)))
    open(os.path.join(d, "g1.ir"), "w").write("ERRORS:\n14:13 E0230 unknown name 'nope'\n")
    open(os.path.join(d, "stray.mn"), "w").write("x\n")
    check("a golden that is not in the INDEX fails", any("stray.mn is in" in p for p in ci.check(d, HOLES)))
    os.remove(os.path.join(d, "stray.mn"))
    os.remove(os.path.join(d, "g2.ir"))
    check("a listed golden with a missing file fails", any("g2.ir is listed" in p for p in ci.check(d, HOLES)))
    open(os.path.join(d, "g2.ir"), "w").write("ERRORS:\n14:12 E0230 unknown name 'nope'\n14:20 E0230 unknown name 'x'\n")
    open(os.path.join(d, "INDEX.tsv"), "w").write(INDEX.replace("\t2\t", "\t1\t"))
    check("diagnostics that differ from the declared mistake count fail (D5)", any("g2: 2 diagnostics intended for 1 mistakes" in p for p in ci.check(d, HOLES)))
    open(os.path.join(d, "INDEX.tsv"), "w").write(INDEX)
    check("a hole of ast.h that no golden covers and that is not excluded fails (F5)", any("F5: hole Expr.new.slot has no golden" in p for p in ci.check(d, HOLES + ["Expr.new.slot"])))
    check("a row naming a hole ast.h no longer has fails (F5)", any("names hole Expr.binary.lhs, which ast.h does not have" in p for p in ci.check(d, ["Expr.unary.operand", "Expr.named_arg.name"])))
finally:
    shutil.rmtree(d, ignore_errors=True)

# ---- the real tree ----
problems, ngen, nfiles = cg.run(REPO)
check("real tree: every generated file equals its generator's output (%d generators, %d files)" % (ngen, nfiles), problems == [] and ngen >= 25 and nfiles >= 1800)
check("real tree: the INDEX, the goldens and the holes of ast.h agree", ci.check(os.path.join(REPO, "tests", "nocascade"), __import__("nocascade_holes").derive()) == [])

print("\n%d failure(s)" % len(fails) if fails else "\nall passed")
sys.exit(1 if fails else 0)
