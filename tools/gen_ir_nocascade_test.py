#!/usr/bin/env python3
"""Tests of tools/nocascade_holes.py and tools/gen_ir_nocascade.py (typed-IR step 21f). make test-gen-nocascade-script"""
import os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import nocascade_holes as nh
import gen_ir_nocascade as gen

fails = []


def check(name, cond):
    print(("PASS: " if cond else "FAIL: ") + name)
    if not cond:
        fails.append(name)


# ---- the holes are derived from the header, with no list of names in the tool ----
HEADER = """#include <stdint.h>
typedef struct { void **items; unsigned long len, cap; } NodeList;
typedef struct Type Type;
typedef struct Expr Expr;
typedef struct Stmt Stmt;
struct Expr {
    int kind;
    union {
        struct { int op; Expr *operand; } unary;
        struct { Expr *base; Expr *brand_new_slot; } widget;      /* a slot added after the generator was written */
        struct { NodeList items; } tuple;                          /* items: Expr* */
        struct { Type *ty; Stmt *body; } holder;
    } as;
};
"""


def scratch(header):
    root = tempfile.mkdtemp(prefix="nh_test_")
    os.makedirs(os.path.join(root, "tools", "mannac"))
    open(os.path.join(root, "tools", "mannac", "ast.h"), "w").write(header)
    return root


root = scratch(HEADER)
try:
    holes = nh.derive(root)
    check("every Expr * member is a hole, derived from the header", "Expr.unary.operand" in holes and "Expr.widget.base" in holes)
    check("a slot nobody has seen before is a hole with no edit to the tool", "Expr.widget.brand_new_slot" in holes)
    check("a list of expressions is a hole (the element type is read from the comment)", "Expr.tuple.items[]" in holes)
    check("a Type * member is a hole and a Stmt * member is not", "Expr.holder.ty" in holes and "Expr.holder.body" not in holes)
finally:
    shutil.rmtree(root, ignore_errors=True)
root = scratch(HEADER.replace("/* items: Expr* */", ""))
try:
    try:
        nh.derive(root)
        check("a NodeList whose element type the header does not say is a hard failure", False)
    except nh.Fail as e:
        check("a NodeList whose element type the header does not say is a hard failure", "element type" in str(e))
finally:
    shutil.rmtree(root, ignore_errors=True)

# ---- the real header against the generator (rule F5) ----
real = nh.derive()
r = subprocess.run([sys.executable, os.path.join(HERE, "nocascade_holes.py"), "--check"], capture_output=True, text=True, cwd=REPO)
check("every hole of ast.h has a skeleton or an excluded reason (F5 passes on the real tree)", r.returncode == 0 and "ok" in r.stdout)
saved = dict(gen.SKELETONS)
del gen.SKELETONS["Expr.binary.lhs"]
r = subprocess.run([sys.executable, "-c", "import sys; sys.path.insert(0, %r); import gen_ir_nocascade as g; del g.SKELETONS['Expr.binary.lhs']; import nocascade_holes as n; sys.exit(n.main(['--check']))" % HERE],
                   capture_output=True, text=True, cwd=REPO)
check("a hole without a skeleton fails F5 and names it", r.returncode == 1 and "F5: hole Expr.binary.lhs has no skeleton" in r.stdout)
gen.SKELETONS.update(saved)
r = subprocess.run([sys.executable, "-c", "import sys; sys.path.insert(0, %r); import gen_ir_nocascade as g; g.SKELETONS['Expr.gone.slot'] = []; import nocascade_holes as n; sys.exit(n.main(['--check']))" % HERE],
                   capture_output=True, text=True, cwd=REPO)
check("a skeleton for a hole that no longer exists fails F5", r.returncode == 1 and "Expr.gone.slot" in r.stdout)
check("the one excluded hole carries its reason", set(gen.EXCLUDED_HOLES) == {"Expr.named_arg.name"} and all(len(v) > 20 for v in gen.EXCLUDED_HOLES.values()))
check("the real header has %d holes" % len(real), len(real) >= 28)

# ---- the generator ----
progs, skipped = gen.programs()
names = [p["name"] for p in progs]
check("%d programs, all with distinct names" % len(progs), len(progs) >= 350 and len(names) == len(set(names)))
check("every hole with a skeleton is exercised", {p["hole"] for p in progs} == set(gen.skeleton_holes()))
check("every producer is used", {p["producer"] for p in progs} == {x[0] for x in gen.EXPR_PRODUCERS + gen.TYPE_PRODUCERS})
check("every skipped combination has a reason", all(len(w[3]) > 15 for w in skipped) and len(skipped) >= 5)
bad, muted = [], [p for p in progs if p["variant"].startswith("muted_")]
for p in progs:
    pos = p["diags"][0].split(" ")[0]
    ln, col = [int(x) for x in pos.split(":")]
    line = p["lines"][ln - 1]
    prod = next(x for x in gen.EXPR_PRODUCERS + gen.TYPE_PRODUCERS if x[0] == p["producer"])
    if p in muted:
        # a bad-assignment-target program (step 21i, C11): E0270 at the first token of the target (the line's first character after the indent) and, unless the combination is
        # listed as one-mistake, the producer's own diagnostic second, inside the target
        two = (p["variant"], p["producer"]) not in gen.ONE_MISTAKE
        e270 = p["diags"][0].split(" ")
        if e270[1] != "E0270" or int(e270[0].split(":")[1]) != len(line) - len(line.lstrip()) + 1 or line.find(prod[1]) + 1 <= int(e270[0].split(":")[1]) or len(p["diags"]) != (2 if two else 1):
            bad.append(p["name"])
        elif two:
            pl, pc = [int(x) for x in p["diags"][1].split(" ")[0].split(":")]
            want = line.find(prod[1]) + 1 + prod[2] - (1 if p["variant"] == "muted_paren" and p["producer"] in gen.PAREN_IS_THE_EXPRESSION else 0)
            if pl != ln or pc != want or p["diags"][1].split(" ")[1] != prod[3]:
                bad.append(p["name"])
        continue
    # the column the generator computed is where the producer's diagnostic starts INSIDE the producer's text, found independently here
    if line.find(prod[1]) < 0 or line.find(prod[1]) + 1 + prod[2] != col:
        bad.append(p["name"])
check("the expected column is the producer's own, computed from where its text went (%d programs checked)" % len(progs), not bad)
check("the muted-target programs (step 21i): every shape x every expression producer", len(muted) == len(gen.MUTED_TARGETS) * len(gen.EXPR_PRODUCERS))
check("no program holds two holes or an unreplaced marker", all("@E@" not in "\n".join(p["lines"]) for p in progs))
check("every program is a single-mistake program (INDEX says 1), except the bad-assignment-target ones, which hold E0270 and the mistake inside the target",
      all(p["mistakes"] == len(p["diags"]) and (p["mistakes"] == 1 or p in muted) for p in progs) and sum(p["mistakes"] == 2 for p in progs) == len(muted) - len(gen.ONE_MISTAKE))

# written files
out = tempfile.mkdtemp(prefix="nc_out_")
try:
    gen.write(out)
    files = sorted(os.listdir(out))
    check("write() produces a .mn and a .ir per program, INDEX.tsv and EXCLUDED.tsv", len([f for f in files if f.endswith(".mn")]) == len(progs) == len([f for f in files if f.endswith(".ir")]) and "INDEX.tsv" in files and "EXCLUDED.tsv" in files)
    first = open(os.path.join(out, progs[0]["name"] + ".ir")).read()
    check("the expected .ir is the ERRORS header and the intended diagnostics", first == "ERRORS:\n" + progs[0]["diags"][0] + "\n")
    check("the INDEX has one row per golden plus the header comment", len(open(os.path.join(out, "INDEX.tsv")).read().strip().split("\n")) == len(progs) + 1)
    side = sorted(f for f in files if f.endswith(".mistakes"))
    check("a .mistakes sidecar is written exactly for the programs with more than one mistake, holding that number (read by the ablation proof and check_generated)",
          side == sorted(p["name"] + ".mistakes" for p in progs if p["mistakes"] != 1) and len(side) > 0
          and all(open(os.path.join(out, f)).read() == "2\n" for f in side))
    out2 = tempfile.mkdtemp(prefix="nc_out2_")
    gen.write(out2)
    same = all(open(os.path.join(out, f), "rb").read() == open(os.path.join(out2, f), "rb").read() for f in files)
    shutil.rmtree(out2, ignore_errors=True)
    check("the generator is deterministic", same)
finally:
    shutil.rmtree(out, ignore_errors=True)

print("\n%d failure(s)" % len(fails) if fails else "\nall passed")
sys.exit(1 if fails else 0)
