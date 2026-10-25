#!/usr/bin/env python3
"""Tests of the pure parts of tools/nocascade_sinks.py (typed-IR step 21c): the profile diff and the attribution to sinks. make test-nocascade-sinks-script"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import nocascade_sinks as ns

fails = []


def check(name, cond):
    print(("PASS: " if cond else "FAIL: ") + name)
    if not cond:
        fails.append(name)


# [line, col, count, hasCount, isRegionEntry, isGap]. llvm-cov merges neighbours with equal counts, so the two profiles have DIFFERENT segment lists.
B = {"f.c": {"segments": [[1, 1, 5, True, True, False], [10, 1, 0, True, True, False], [20, 1, 5, True, False, False], [30, 1, 0, True, True, False]],
             "branches": {(5, 3, 5, 20): (4, 0), (7, 3, 7, 20): (0, 3)}}}
I = {"f.c": {"segments": [[1, 1, 9, True, True, False], [10, 1, 2, True, True, False], [30, 1, 0, True, True, False]],
             "branches": {(5, 3, 5, 20): (4, 0), (7, 3, 7, 20): (2, 3), (8, 3, 8, 9): (0, 1)}}}
items = ns.poison_only(B, I)
check("a region the clean builds never ran but an injected run did is poison-only (line 10)", ("f.c", 10, 1, "region") in items)
check("a region both ran is not (line 1)", ("f.c", 1, 1, "region") not in items)
check("a region nobody ran is not (line 30)", ("f.c", 30, 1, "region") not in items)
check("a branch direction only the injected runs took is poison-only (7:3 true)", ("f.c", 7, 3, "branch-true") in items)
check("a direction both took is not (5:3 true, 7:3 false)", ("f.c", 5, 3, "branch-true") not in items and ("f.c", 7, 3, "branch-false") not in items)
check("a branch the clean profile has no record of counts as never taken (8:3 false)", ("f.c", 8, 3, "branch-false") in items)
check("count_at reads the region that covers a position, not the nearest entry", ns.count_at(B["f.c"]["segments"], 25, 4) == 5 and ns.count_at(B["f.c"]["segments"], 12, 1) == 0)
try:
    ns.poison_only({}, I)
    check("a file missing from the clean profile is a hard failure", False)
except SystemExit:
    check("a file missing from the clean profile is a hard failure", True)

man = {"sinks": [{"id": "outer", "file": "f.c", "extent": [5, 40]}, {"id": "inner", "file": "f.c", "extent": [8, 12]}, {"id": "other", "file": "g.c", "extent": [1, 99]}],
       "functions": [{"file": "f.c", "function": "work", "start": 50, "end": 60}]}
conf, out = ns.attribute([("f.c", 10, 1, "region"), ("f.c", 7, 3, "branch-true"), ("f.c", 55, 1, "region"), ("f.c", 99, 1, "region")], man)
check("the innermost sink extent wins", [i[1] for i in conf["inner"]] == [10] and [i[1] for i in conf["outer"]] == [7])
check("code outside every sink is grouped by its function", [i[1] for i in out[("f.c", "work")]] == [55])
check("code outside every function is reported too, never dropped", [i[1] for i in out[("f.c", "?")]] == [99])
check("a sink of another file never takes it", "other" not in conf)

print("\n%d failure(s)" % len(fails) if fails else "\nall passed")
sys.exit(1 if fails else 0)
