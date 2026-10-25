#!/usr/bin/env python3
"""Uncovered lines of the compiler sources must each be justified (step 20i; CLAUDE.md "At every tag, list uncovered lines in lexer.c, parser.c and the typed-IR files
ir*.c ... each must be justified as untestable or get a test. No percentage targets"; register V6).

  tools/coverage_check.py [--list]

Run after `make coverage` (build/coverage/merged.profdata). For lexer.c, parser.c and ir*.c: every source line whose coverage count is 0 must have a line in
tests/coverage_justified.txt: `file<TAB>source text of the line (stripped)<TAB>reason`. Fails on an uncovered line without a reason, and on a reason whose line is now
covered or gone (stale). A line text that occurs on several uncovered lines is matched once per occurrence in order. --list prints the uncovered lines.
"""
import json, os, re, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COV = os.path.join(ROOT, "build", "coverage")
SRC = "tools/mannac/"
FILES = re.compile(r"^(lexer\.c|parser\.c|ir(?!_main|_test|_ndebug_test|_e0261)[a-z_]*\.c)$")   # the library files; *_test.c and *main.c are the harnesses
JUSTIFIED = os.path.join(ROOT, "tests", "coverage_justified.txt")
OBJECTS = ["mannac", "arena_test", "ast_test", "parser_state_test", "parser_fixes_test", "parser_dump", "ir_test", "ir_dump"]


def llvm_cov():
    if shutil.which("xcrun") and subprocess.run(["xcrun", "--find", "llvm-cov"], capture_output=True).returncode == 0:
        return ["xcrun", "llvm-cov"]
    for cand in ["llvm-cov"] + ["llvm-cov-%d" % v for v in range(25, 10, -1)]:
        if shutil.which(cand):
            return [cand]
    sys.exit("coverage_check: llvm-cov not found (xcrun or PATH): this check is not skipped")


def uncovered():
    prof = os.path.join(COV, "merged.profdata")
    if not os.path.exists(prof):
        sys.exit("coverage_check: %s missing: run `make coverage` first" % prof)
    objs = [os.path.join(COV, o) for o in OBJECTS]
    cmd = llvm_cov() + ["export", "-format=lcov", objs[0]] + sum([["-object", o] for o in objs[1:]], []) + ["-instr-profile=" + prof]
    lcov = subprocess.run(cmd, capture_output=True, text=True, check=True).stdout
    out, artefacts, cur, lines, counts = [], [], None, None, {}

    def flush():
        # clang opens a zero-count region right after a call that cannot return, and assert() is one on its failing side: the statement after an assert reads 0
        # although it runs whenever the assert line runs (experiment: tools/coverage_check_test.py compiles a 3-line function and shows it). Such a line is not
        # uncovered code: it is reported separately and needs no reason. Only when the assert line itself ran (count > 0).
        if lines is None:
            return
        for ln, c in sorted(counts.items()):
            if c != 0:
                continue
            k = ln - 1
            while k >= 1 and (not lines[k - 1].strip() or lines[k - 1].strip().startswith(("//", "/*", "*"))):
                k -= 1
            if k >= 1 and lines[k - 1].strip().startswith("assert(") and counts.get(k, 0) > 0:
                artefacts.append((cur, ln, lines[ln - 1].strip()))
            else:
                out.append((cur, ln, lines[ln - 1].strip()))

    for row in lcov.split("\n"):
        if row.startswith("SF:"):
            flush()
            cur = os.path.basename(row[3:])
            counts = {}
            lines = None
            if FILES.match(cur):
                lines = open(os.path.join(ROOT, SRC, cur), encoding="utf-8", errors="replace").read().split("\n")
        elif row.startswith("DA:") and lines is not None:
            ln, count = row[3:].split(",")[:2]
            counts[int(ln)] = int(count)
    flush()
    return sorted(set(out)), sorted(set(artefacts))


def load_justified():
    out = {}
    if os.path.exists(JUSTIFIED):
        for l in open(JUSTIFIED, encoding="utf-8").read().split("\n"):
            if l.strip() and not l.startswith("#"):
                f, text, why = l.split("\t", 2)
                out.setdefault((f, text), []).append(why)
    return out


def main(argv):
    unc, art = uncovered()
    if "--list" in argv:
        for f, ln, t in unc:
            print("%s:%d\t%s" % (f, ln, t))
        print("(%d more lines are the zero-count region after an assert, not listed)" % len(art))
        return 0
    just = load_justified()
    left = {k: len(v) for k, v in just.items()}
    missing = []
    for f, ln, t in unc:
        if left.get((f, t), 0) > 0:
            left[(f, t)] -= 1
        else:
            missing.append((f, ln, t))
    stale = [(k, n) for k, n in left.items() if n > 0]
    print("coverage_check: %d uncovered lines in lexer.c, parser.c and ir*.c; %d justified in tests/coverage_justified.txt; %d without a reason; %d stale reasons; "
          "%d further lines are clang's zero-count region after an assert that ran (not code that was missed)" %
          (len(unc), len(unc) - len(missing), len(missing), sum(n for _, n in stale), len(art)))
    for f, ln, t in missing:
        print("NO REASON %s:%d  %s" % (f, ln, t))
    for (f, t), n in stale:
        print("STALE REASON (the line is covered now or gone) %s  %s" % (f, t))
    return 1 if (missing or stale) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
