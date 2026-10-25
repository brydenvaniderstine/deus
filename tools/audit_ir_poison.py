#!/usr/bin/env python3
"""One-diagnostic-golden audit of the poisoning paths (typed-IR step 15, reworded at step 17). Run from the repo root: python3 tools/audit_ir_poison.py [--check]

A POISONING PATH is a source line in the builder where a mistake becomes a diagnostic or an ERROR-typed node, or
where an ERROR operand is consumed silently (the places a cascade would start): every `ir_error(` call, every
`is_error(` / `error_node(` / `ir_type_error(` use, and the declaration-poisoning flag `poisoned`.
For each such line this finds the goldens (tests/ir/*.mn) that execute it, using one instrumented ir_dump run
per golden, and reports the ones whose expected output holds EXACTLY ONE diagnostic: a test in which that path
runs and nothing else is reported. A line with no such golden is listed as NOT RUN BY A ONE-DIAGNOSTIC GOLDEN (exit 1 with --check).
THIS IS NOT A PROOF that the path cannot cascade: it shows that a test exists in which the path ran and the output was one
diagnostic, and says nothing about other inputs. The unit tests (ir_test.c) cannot be split per test, so a line only they reach
must be argued by hand in the report.
"""
import os, re, subprocess, sys, glob, shutil, json, bisect
from concurrent.futures import ThreadPoolExecutor

ROOT = os.getcwd()
OUT = os.path.join(ROOT, "build", "audit_poison")
SRC = "tools/mannac/"
FILES = ["ir_decls.c", "ir_expr.c", "ir_body.c", "ir_calls.c", "ir_build.c", "ir_rename.c"]
BIN_SRCS = ["arena", "ast", "diag", "parser_state", "parser", "lexer", "ir", "ir_dump", "ir_build", "ir_decls",
            "ir_expr", "ir_body", "ir_calls", "ir_names", "ir_rename", "ir_selfcheck", "ir_main"]
PAT = re.compile(r"\b(ir_error|is_error|error_node|ir_type_error|has_error|settle_pending)\(|\bpoisoned\b|IR_T_ERROR|\.pending")
DEF = re.compile(r"^\s*(static\s+)?[\w\s\*]+\b(is_error|error_node|ir_type_error|settle_pending)\([^;]*\)\s*\{?\s*$|^\s*(static\s+)?[\w\s\*]+\b(is_error|error_node)\([^;]*\)\s*\{.*\}\s*$")

def llvm_tool(name):
    """llvm-cov / llvm-profdata: `xcrun` on macOS, else the plain tool or a versioned one on PATH (Linux). A missing tool is a hard failure, never a skip."""
    if shutil.which("xcrun") and subprocess.run(["xcrun", "--find", name], capture_output=True).returncode == 0: return ["xcrun", name]
    for cand in [name] + [name + "-" + str(v) for v in range(25, 10, -1)]:
        if shutil.which(cand): return [cand]
    sys.exit("audit_ir_poison: %s not found (xcrun or PATH): install LLVM; this check is not skipped" % name)

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
    name = os.path.basename(os.path.dirname(mn)) + "/" + os.path.basename(mn)[:-3]
    tag = name.replace("/", "__"); raw = "%s/%s.profraw" % (OUT, tag); prof = "%s/%s.profdata" % (OUT, tag)
    env = dict(os.environ, LLVM_PROFILE_FILE=raw)
    run = subprocess.run([OUT + "/ir_dump", mn], env=env, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    ndiag = sum(1 for l in run.stdout.split("\n") if re.match(r"\d+:\d+ E\d{4} ", l))
    subprocess.run(llvm_tool("llvm-profdata") + ["merge", "-sparse", raw, "-o", prof], check=True)
    j = json.loads(subprocess.run(llvm_tool("llvm-cov") + ["export", OUT + "/ir_dump", "-instr-profile=" + prof],
                                  capture_output=True, text=True, check=True).stdout)
    segs = {}
    for fl in j["data"][0]["files"]:
        b = os.path.basename(fl["filename"])
        if b in FILES: segs[b] = [(s[0], s[1], s[2] if (s[3] and not s[5]) else None) for s in fl["segments"]]
    os.remove(raw); os.remove(prof)
    return name, segs, ndiag

def ran(segs, f, line, col):
    """the count of the region that covers (line, col) is nonzero"""
    ss = segs.get(f, [])
    i = bisect.bisect_right([(s[0], s[1]) for s in ss], (line, col)) - 1
    return i >= 0 and ss[i][2] is not None and ss[i][2] > 0

REASONS = "tools/audit_ir_poison_reasons.txt"

def load_reasons():
    """`file<TAB>source text<TAB>reason`: a poisoning line no one-diagnostic input runs, with the written reason it is not (yet) proven."""
    out = {}
    if os.path.exists(REASONS):
        for l in open(REASONS).read().split("\n"):
            if l.strip() and not l.startswith("#"):
                f, src, why = l.split("\t", 2)
                out[(f, src)] = why
    return out

def corpus():
    """every input that can show a path running: the goldens (tests/ir) and the originals of the fix cases (tests/ir_fix)"""
    ins = sorted(glob.glob("tests/ir/*.mn")) + [m for m in sorted(glob.glob("tests/ir_fix/*.mn")) if not re.search(r"\.fixed2?\.mn$", m)]
    return ins

if __name__ == "__main__":
    check = "--check" in sys.argv
    cands = candidates()
    build()
    with ThreadPoolExecutor(8) as ex: results = list(ex.map(lines_hit, corpus()))
    one = {name: hit for name, hit, nd in results if nd == 1}
    anyhit = {name: hit for name, hit, nd in results if nd >= 1}
    reasons = load_reasons()
    not_run, used = [], set()
    print("%-14s %-6s %-58s %s" % ("file:line", "ones", "source", "one-diagnostic inputs that run it (first 2)"))
    for f, n, src, ln, col in cands:
        proof = sorted(nm for nm, hit in one.items() if ran(hit, f, ln, col))
        err = sorted(nm for nm, hit in anyhit.items() if ran(hit, f, ln, col))
        print("%-14s %-6d %-58s %s" % ("%s:%d" % (f, n), len(proof), src[:58], ", ".join(proof[:2]) if proof else ("NOT RUN BY A ONE-DIAGNOSTIC INPUT (reached by %d input(s) with several diagnostics)" % len(err) if err else "NOT RUN BY A ONE-DIAGNOSTIC INPUT (no input reaches it)")))
        if proof: continue
        if (f, src) in reasons: used.add((f, src))
        else: not_run.append((f, n, src))
    stale = sorted(k for k in reasons if k not in used)
    print("\n%d poisoning lines, %d run by a one-diagnostic input, %d with a written reason (%s), %d with neither (this is not a no-cascade proof)" %
          (len(cands), len(cands) - len(used) - len(not_run), len(used), REASONS, len(not_run)))
    for f, n, src in not_run: print("NOT RUN, NO REASON %s:%d  %s" % (f, n, src))
    for f, src in stale: print("STALE REASON (the line is now proven or gone) %s  %s" % (f, src))
    shutil.rmtree(OUT, ignore_errors=True)
    sys.exit(1 if (check and (not_run or stale)) else 0)
