#!/usr/bin/env python3
"""No-cascade audit (typed-IR step 15). Run from the repo root: python3 tools/audit_ir_poison.py [--check]

A POISONING PATH is a source line in the builder where a mistake becomes a diagnostic or an ERROR-typed node, or
where an ERROR operand is consumed silently (the places a cascade would start): every `ir_error(` call, every
`is_error(` / `error_node(` / `ir_type_error(` use, and the declaration-poisoning flag `poisoned`.
For each such line this finds the goldens (tests/ir/*.mn) that execute it, using one instrumented ir_dump run
per golden, and reports the ones whose expected output holds EXACTLY ONE diagnostic: a test in which that path
runs and one mistake costs one error. A line with no such golden is listed as UNPROVEN (exit 1 with --check);
the unit tests (ir_test.c) cannot be split per test, so a line only they reach must be argued by hand in the report.
"""
import os, re, subprocess, sys, glob, shutil, json, bisect
from concurrent.futures import ThreadPoolExecutor

ROOT = os.getcwd()
OUT = os.path.join(ROOT, "build", "audit_poison")
SRC = "tools/mannac/"
FILES = ["ir_decls.c", "ir_expr.c", "ir_body.c", "ir_calls.c", "ir_build.c"]
BIN_SRCS = ["arena", "ast", "diag", "parser_state", "parser", "lexer", "ir", "ir_dump", "ir_build", "ir_decls",
            "ir_expr", "ir_body", "ir_calls", "ir_names", "ir_rename", "ir_main"]
PAT = re.compile(r"\b(ir_error|is_error|error_node|ir_type_error)\(|\bpoisoned\b")
DEF = re.compile(r"^\s*(static\s+)?[\w\s\*]+\b(is_error|error_node|ir_type_error)\([^;]*\)\s*\{?\s*$|^\s*(static\s+)?[\w\s\*]+\b(is_error|error_node)\([^;]*\)\s*\{.*\}\s*$")

def position(code, nxt):
    """(line offset, column) of the code that runs only when the poisoning path is taken: the last match, or, when that match
    is inside an if-condition, the statement the condition guards (so a condition that is always evaluated proves nothing)."""
    m = list(PAT.finditer(code))[-1]
    depth = 0; cond_end = None
    for m_if in re.finditer(r"\bif\s*\(", code):
        i = m_if.end() - 1; d = 0
        for j in range(i, len(code)):
            d += code[j] == "("; d -= code[j] == ")"
            if d == 0: cond_end = j; break
        if m_if.start() < m.start() <= (cond_end if cond_end else len(code)): break
        cond_end = None
    if cond_end is None: return 0, m.start() + 1
    rest = code[cond_end + 1:]
    if rest.strip() and rest.strip() != "{": return 0, cond_end + 1 + (len(rest) - len(rest.lstrip())) + 1
    return 1, len(nxt) - len(nxt.lstrip()) + 1

def candidates():
    out = []
    for f in FILES:
        in_block = False
        raw = open(SRC + f).read().split("\n")
        for n, line in enumerate(raw, 1):
            code = line
            if in_block:
                if "*/" in code: code = code.split("*/", 1)[1]; in_block = False
                else: continue
            code = re.sub(r"/\*.*?\*/", "", code)
            if "/*" in code: code = code.split("/*", 1)[0]; in_block = True
            code = code.split("//", 1)[0]
            if not PAT.search(code) or DEF.match(code): continue
            dl, col = position(code, raw[n] if n < len(raw) else "")
            out.append((f, n, line.strip(), n + dl, col))
    return out

def build():
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT)
    cmd = ["clang", "-std=c11", "-g", "-fprofile-instr-generate", "-fcoverage-mapping", "-o", OUT + "/ir_dump"] + \
          [SRC + s + ".c" for s in BIN_SRCS]
    subprocess.run(cmd, check=True)

def lines_hit(mn):
    """the set of (file, line, col) positions covered with a nonzero count, as segment starts: returns a checker"""
    name = os.path.basename(mn)[:-3]
    raw = "%s/%s.profraw" % (OUT, name); prof = "%s/%s.profdata" % (OUT, name)
    env = dict(os.environ, LLVM_PROFILE_FILE=raw)
    subprocess.run([OUT + "/ir_dump", mn], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["xcrun", "llvm-profdata", "merge", "-sparse", raw, "-o", prof], check=True)
    j = json.loads(subprocess.run(["xcrun", "llvm-cov", "export", OUT + "/ir_dump", "-instr-profile=" + prof],
                                  capture_output=True, text=True, check=True).stdout)
    segs = {}
    for fl in j["data"][0]["files"]:
        b = os.path.basename(fl["filename"])
        if b in FILES: segs[b] = [(s[0], s[1], s[2] if (s[3] and not s[5]) else None) for s in fl["segments"]]
    os.remove(raw); os.remove(prof)
    return name, segs

def ran(segs, f, line, col):
    """the count of the region that covers (line, col) is nonzero"""
    ss = segs.get(f, [])
    i = bisect.bisect_right([(s[0], s[1]) for s in ss], (line, col)) - 1
    return i >= 0 and ss[i][2] is not None and ss[i][2] > 0

def n_diags(name):
    return sum(1 for l in open("tests/ir/%s.ir" % name).read().split("\n") if re.match(r"\d+:\d+ E\d{4} ", l))

if __name__ == "__main__":
    check = "--check" in sys.argv
    cands = candidates()
    build()
    goldens = sorted(glob.glob("tests/ir/*.mn"))
    with ThreadPoolExecutor(8) as ex: results = list(ex.map(lines_hit, goldens))
    one = {name: hit for name, hit in results if n_diags(name) == 1}
    anyhit = {name: hit for name, hit in results if n_diags(name) >= 1}
    unproven = []
    print("%-14s %-6s %-58s %s" % ("file:line", "ones", "source", "proving goldens (exactly one diagnostic; first 2)"))
    for f, n, src, ln, col in cands:
        proof = sorted(nm for nm, hit in one.items() if ran(hit, f, ln, col))
        err = sorted(nm for nm, hit in anyhit.items() if ran(hit, f, ln, col))
        print("%-14s %-6d %-58s %s" % ("%s:%d" % (f, n), len(proof), src[:58], ", ".join(proof[:2]) if proof else ("UNPROVEN (reached by %d golden(s) with several diagnostics)" % len(err) if err else "UNPROVEN (no golden reaches it)")))
        if not proof: unproven.append((f, n))
    print("\n%d poisoning lines, %d proven by a one-diagnostic golden, %d unproven" % (len(cands), len(cands) - len(unproven), len(unproven)))
    for f, n in unproven: print("UNPROVEN %s:%d" % (f, n))
    shutil.rmtree(OUT, ignore_errors=True)
    sys.exit(1 if (check and unproven) else 0)
