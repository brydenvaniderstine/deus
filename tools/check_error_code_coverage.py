#!/usr/bin/env python3
"""Per-code coverage for the typed-IR codes (typed-IR step 15). Run from the repo root: python3 tools/check_error_code_coverage.py
For every E02xx code in docs/error_codes.md, lists the goldens (tests/ir/*.ir, tests/ir_fix/*.remaining) and the unit
tests (tools/mannac/ir_test.c, by test function) that emit it. Exits 1 if a code the doc says is emitted has no test, if
a code is emitted by tools/mannac/diag.c but not documented, or if a documented-as-emitted code has no constructor in diag.c.
Codes the doc does not call emitted (unassigned / retired) are listed separately."""
import glob, re, sys

doc = open("docs/error_codes.md").read()
rows = {}  # code -> emitted?
for line in doc.split("\n"):
    m = re.match(r"\| `(E02\d\d)` \|", line)
    if m: rows[m.group(1)] = "Emitted since" in line
for m in re.finditer(r"`(E02\d\d)` is deliberately unassigned", doc): rows.setdefault(m.group(1), False)

golden = {c: [] for c in rows}
for f in sorted(glob.glob("tests/ir/*.ir") + glob.glob("tests/ir_fix/*.remaining")):
    for l in open(f).read().split("\n"):
        m = re.match(r"\d+:\d+ (E02\d\d) ", l)
        if m and m.group(1) in golden and f not in golden[m.group(1)]: golden[m.group(1)].append(f)

unit = {c: [] for c in rows}
fn = None
for l in open("tools/mannac/ir_test.c").read().split("\n"):
    m = re.match(r"static void (test_\w+)\(", l)
    if m: fn = m.group(1)
    for c in re.findall(r'"(E02\d\d)"', l):
        if c in unit and fn and fn not in unit[c]: unit[c].append(fn)

src = set(re.findall(r'"(E02\d\d)"', open("tools/mannac/diag.c").read()))
bad = []
print("%-6s %-5s %-4s %s" % ("code", "gold", "unit", "tests (first goldens, then unit tests)"))
for c in sorted(rows):
    if not rows[c]: continue
    g, u = golden[c], unit[c]
    names = [x.split("/")[-1].rsplit(".", 1)[0] for x in g[:2]] + u[:2]
    print("%-6s %-5d %-4d %s" % (c, len(g), len(u), ", ".join(names)))
    if not g and not u: bad.append("%s is documented as emitted and no test emits it" % c)
    if c not in src: bad.append("%s is documented as emitted but diag.c has no constructor for it" % c)
print("\nnot emitted (listed separately): " + (", ".join(sorted(c for c in rows if not rows[c])) or "none"))
for c in sorted(src - set(rows)): bad.append("%s is emitted by diag.c but not documented in docs/error_codes.md" % c)
print("%d documented as emitted, %d with a golden, %d with a unit test" % (sum(rows.values()), sum(1 for c in rows if rows[c] and golden[c]), sum(1 for c in rows if rows[c] and unit[c])))
for b in bad: print("FAIL " + b)
sys.exit(1 if bad else 0)
