#!/usr/bin/env python3
"""Tests of tools/poison_paths.py (typed-IR step 21a) on a scratch builder and on the real tree. make test-poison-paths-script

The scratch builder uses names the old pattern list of tools/audit_ir_poison.py has never seen (`zorp`, `lookup`), which is the shape of re-audit 3
finding 9: a silent-consumption line the regex list missed. The tool must find it without anyone adding the name anywhere."""
import json, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import poison_paths as pp
import audit_ir_poison as audit

fails = []


def check(name, cond):
    print(("PASS: " if cond else "FAIL: ") + name)
    if not cond:
        fails.append(name)


HEADER = """#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
typedef enum { T_INT, IR_T_ERROR } Kind;
enum { P_INT, P_ERROR };
typedef struct { Kind kind; } Type;
typedef struct { Type *node; bool group; unsigned pending; } Synth;
typedef struct { Type prim[2]; int n; } State;
typedef struct { bool poisoned; int size; bool ok; } Info;
void ir_error(State *st, int d);
int diag_ir_x(int v);
"""
BODY = """
static void prims(State *st) { st->prim[P_ERROR].kind = IR_T_ERROR; }
static Type *type_error(State *st) { return &st->prim[P_ERROR]; }
static bool zorp(Synth s) { return s.node->kind == IR_T_ERROR; }
int use_pred(Synth a) { if (zorp(a)) return 0; return 1; }
int use_direct(Type *t) { return t->kind == IR_T_ERROR ? 0 : 1; }
int use_assert(Type *t) { assert(t->kind != IR_T_ERROR); return 1; }
void mark(Info *i, Type *t) { if (t->kind == IR_T_ERROR) i->poisoned = true; }
int use_carrier(Info *i) { if (i->poisoned) return 0; return 1; }
static Type *lookup(State *st, int k) { if (k < 0) { ir_error(st, diag_ir_x(k)); return NULL; } return &st->prim[0]; }
int use_null(State *st) { Type *t = lookup(st, 1); if (!t) return 0; return 1; }
int plain(Synth s) { unsigned p = s.pending; return (int)p; }
static Type *fold(State *st, int v) { if (v > 5) { ir_error(st, diag_ir_x(v)); return &st->prim[0]; } return NULL; }
int use_fold(State *st) { Type *f = fold(st, 1); if (f) return 1; return 0; }
void set_ok(Info *i, Type *t) { if (t->kind == IR_T_ERROR) return; i->ok = true; }
int use_ok(Info *i) { if (!i->ok) return 0; return 1; }
int use_flag(State *st) { bool bad = false; Type *t = lookup(st, 1); if (!t) { bad = true; } if (bad) return 0; return 1; }
Type *make(State *st) { ir_error(st, diag_ir_x(1)); return type_error(st); }
"""


def scratch(body=BODY, header=HEADER):
    root = tempfile.mkdtemp(prefix="pp_test_")
    os.makedirs(os.path.join(root, "tools", "mannac"))
    open(os.path.join(root, "tools", "mannac", "ir_fx.c"), "w").write(header + body)
    return root


root = scratch()
try:
    prog = pp.load(root)
    man = prog.manifest()
    fns = sorted({s["function"] for s in man["sinks"]})
    check("the poison alias enumerator is derived from the code (P_ERROR), not named", man["aliases"] == ["P_ERROR"])
    check("a predicate with a name no list has ever held is found (zorp)", man["predicates"] == ["zorp"])
    check("the producers include the type constructor and its caller", "type_error" in man["producers"] and "make" in man["producers"])
    check("a field assigned under a poison test is a carrier (poisoned)", "poisoned" in man["carriers"])
    check("a function that returns NULL after a reported error is null-poison (lookup)", man["nullpoison"] == ["lookup"])
    check("a NULL that means 'nothing to fold', after a branch that reported and returned, is NOT poison (fold): a sink on it would be a false sink", "fold" not in man["nullpoison"] and "use_fold" not in {x["function"] for x in man["sinks"]})
    check("sinks are exactly the decisions", fns == ["mark", "set_ok", "use_carrier", "use_direct", "use_flag", "use_null", "use_ok", "use_pred"])
    ok_sink = [x for x in man["sinks"] if x["function"] == "use_ok"][0]
    check("a flag set to true only AFTER a poison early return is poison by omission: 'not poison' for it is 1, not 0 (the ablation must not make everything look poisoned)", ok_sink["atoms"][0]["replace"] == "1" and "ok" in man["carriers"])
    fl = [x for x in man["sinks"] if x["function"] == "use_flag" and x["text"] == "bad"]
    check("a LOCAL flag set to true under a test of a null-poison value is poison-carrying: a decision on it is a sink whose ablation says 'false'", len(fl) == 1 and fl[0]["atoms"][0]["replace"] == "0")
    check("a flag assigned under a poison test says 'not poison' with 0", [a["replace"] for a in [x for x in man["sinks"] if x["function"] == "use_carrier"][0]["atoms"]] == ["0"])
    check("an assert on poison is not a sink", "use_assert" not in fns)
    check("a plain read of a carrier is flow, not a sink", "plain" not in fns)
    src_line = [l for l in open(os.path.join(root, "tools/mannac/ir_fx.c")).read().split("\n") if l.startswith("int use_pred")][0]
    check("the old pattern list does NOT see the zorp line (this is finding 9's shape)", not audit.PAT.search(src_line))
    s = [x for x in man["sinks"] if x["function"] == "use_pred"][0]
    check("a sink carries the range of its atom and the constant that ablates it", len(s["atoms"]) == 1 and s["atoms"][0]["replace"] == "0" and s["atoms"][0]["text"] == "zorp(a)")
    src = open(os.path.join(root, "tools/mannac/ir_fx.c"), "rb").read()
    check("the atom range is the atom's bytes in the source", src[s["atoms"][0]["start"]:s["atoms"][0]["end"]] == b"zorp(a)")
    check("emitters name their diag constructor", [e["constructor"] for e in man["emitters"]] == ["diag_ir_x"] * 3)
    check("the creation sites include the call to the type constructor", any(p["function"] == "make" for p in man["producer_sites"]))
    ids = [x["id"] for x in man["sinks"]]
    check("sink ids are unique", len(ids) == len(set(ids)))
    # --check: stale and current
    mp = os.path.join(root, "m.json")
    r = subprocess.run([sys.executable, os.path.join(HERE, "poison_paths.py"), "--json", mp], cwd=root, capture_output=True, text=True)
    check("--json writes the manifest", r.returncode == 0 and os.path.exists(mp))
    r = subprocess.run([sys.executable, os.path.join(HERE, "poison_paths.py"), "--check", mp], cwd=root, capture_output=True, text=True)
    check("--check passes on a current manifest", r.returncode == 0)
    p = os.path.join(root, "tools/mannac/ir_fx.c")
    open(p, "a").write("int more(Type *t) { if (t->kind == IR_T_ERROR) return 0; return 1; }\n")
    r = subprocess.run([sys.executable, os.path.join(HERE, "poison_paths.py"), "--check", mp], cwd=root, capture_output=True, text=True)
    check("--check FAILS when a new sink appears and the manifest was not regenerated", r.returncode == 1 and "stale" in r.stdout)
finally:
    shutil.rmtree(root, ignore_errors=True)

# a builder file nobody listed is analysed; the harness files are not
root = scratch()
try:
    open(os.path.join(root, "tools/mannac/ir_brand_new.c"), "w").write(HEADER + "int g(Type *t) { return t->kind == IR_T_ERROR ? 1 : 0; }\nstatic void p(State *s) { s->prim[P_ERROR].kind = IR_T_ERROR; }\n")
    open(os.path.join(root, "tools/mannac/ir_test.c"), "w").write("this is not C at all\n")
    check("a new ir_*.c is analysed with no edit to the tool; the test file is excluded", "ir_brand_new.c" in pp.builder_files(root) and "ir_test.c" not in pp.builder_files(root))
finally:
    shutil.rmtree(root, ignore_errors=True)

# a sink inside a header's inline function (reviewer finding 1: the Mac silently dropped functions of ir_internal.h while Ubuntu clang analysed them)
root = tempfile.mkdtemp(prefix="pp_test_")
try:
    os.makedirs(os.path.join(root, "tools", "mannac"))
    open(os.path.join(root, "tools/mannac/ir_fx.h"), "w").write(HEADER + "static inline int hdr_sink(Type *t) { if (t->kind == IR_T_ERROR) return 0; return 1; }\n")
    open(os.path.join(root, "tools/mannac/ir_fx.c"), "w").write('#include "ir_fx.h"\nstatic void prims(State *st) { st->prim[P_ERROR].kind = IR_T_ERROR; }\n')
    man = pp.load(root).manifest()
    hs = [x for x in man["sinks"] if x["function"] == "hdr_sink"]
    check("a sink inside a header's inline function is found, attributed to the header (repo-relative)", len(hs) == 1 and hs[0]["file"] == "ir_fx.h" and "tools/mannac/ir_fx.h" in man["files"])
    check("the builder headers are listed in the manifest's files", pp.builder_headers(root) == ["ir_fx.h"])
    # printing differences: the same file as an absolute path, a relative path and through a symlink all normalise to one name
    link = root + "_link"
    os.symlink(root, link)
    forms = [os.path.join(root, "tools/mannac/ir_fx.h"), "tools/mannac/ir_fx.h", os.path.join(link, "tools/mannac/ir_fx.h"), "./tools/mannac/../mannac/ir_fx.h"]
    check("paths as Apple clang and Ubuntu clang print them normalise to one repo-relative name", {pp.norm(f, root) for f in forms} == {"tools/mannac/ir_fx.h"})
    check("a file outside the repo keeps an absolute path and is not a builder file", pp.norm("/usr/include/stdio.h", root) == os.path.realpath("/usr/include/stdio.h") and not pp.is_builder(pp.norm("/usr/include/stdio.h", root)))
    # a path the normaliser does not recognise must be a hard failure, not a skip
    real_norm = pp.norm
    pp.norm = lambda path, r: ("/elsewhere/" + os.path.basename(path)) if path and path.endswith(".h") and "ir_fx" in path else real_norm(path, r)
    try:
        pp.load(root)
        check("a builder function whose path does not normalise is a hard failure naming it", False)
    except pp.Fail as e:
        check("a builder function whose path does not normalise is a hard failure naming it", "NOT analysed" in str(e) and "hdr_sink" in str(e))
    finally:
        pp.norm = real_norm
    os.unlink(link)
finally:
    shutil.rmtree(root, ignore_errors=True)

# hard failures, never a skip
root = scratch(body=BODY.replace("static void prims(State *st) { st->prim[P_ERROR].kind = IR_T_ERROR; }", ""))
try:
    try:
        pp.load(root)
        check("no derivable alias is a hard failure", False)
    except pp.Fail as e:
        check("no derivable alias is a hard failure", "alias" in str(e))
finally:
    shutil.rmtree(root, ignore_errors=True)
root = scratch(body="int broken( {\n")
try:
    try:
        pp.load(root)
        check("a file that does not parse is a hard failure", False)
    except pp.Fail as e:
        check("a file that does not parse is a hard failure", "clang failed" in str(e))
finally:
    shutil.rmtree(root, ignore_errors=True)

# ---- muting (step 21i): the mute flag is DERIVED (the member the recorder tests before diag_add), its writers are the muting sites ----
MUTE_HEADER = """#include <stddef.h>
typedef enum { T_INT, IR_T_ERROR } Kind;
enum { P_INT, P_ERROR };
typedef struct { Kind kind; } Type;
typedef struct { int hush; int count; Type prim[2]; } St;
void diag_add(St *st, int d);
int diag_ir_x(int v);
"""
MUTE_BODY = """
static void prims(St *st) { st->prim[P_ERROR].kind = IR_T_ERROR; }
void ir_error(St *st, int d) { if (st->hush) { return; } diag_add(st, d); st->count++; }
int dry(St *st, int v) { int h = st->count; st->hush++; ir_error(st, v); st->hush--; return st->count == h; }
void reset(St *st) { st->hush = 0; }
void other(St *st) { st->count += 1; }
"""
root = scratch(body=MUTE_BODY, header=MUTE_HEADER)
try:
    prog = pp.load(root)
    mu = prog.manifest()["muting"]
    check("muting: the recorder's test and the one site that raises the flag are found, and nothing else (a reset and an unrelated counter are not sites)",
          sorted((x["function"], x["role"]) for x in mu) == [("dry", "site"), ("ir_error", "recorder")])
    check("muting: the flag name is read from the recorder, not listed in the tool (here it is `hush`)", all("hush" in a["text"] for x in mu for a in x["atoms"]))
    site = [x for x in mu if x["role"] == "site"][0]
    check("muting: a site's atoms are the raise AND the lower, both made `0` (the flag can never go negative in the mutant)",
          [a["text"] for a in site["atoms"]] == ["st->hush++", "st->hush--"] and all(a["replace"] == "0" for a in site["atoms"]))
    check("muting: the committed (stable) form carries the muting sinks", len(pp.stable(prog.manifest())["muting"]) == 2)
finally:
    shutil.rmtree(root, ignore_errors=True)
root = scratch(body=MUTE_BODY + "void odd(St *st) { st->hush = 2; }\n", header=MUTE_HEADER)
try:
    try:
        pp.load(root).manifest()
        check("muting: assigning the flag something other than 0 is a hard failure", False)
    except pp.Fail as e:
        check("muting: assigning the flag something other than 0 is a hard failure", "unsupported way to mute" in str(e))
finally:
    shutil.rmtree(root, ignore_errors=True)
root = scratch(body=MUTE_BODY + "void half(St *st) { st->hush++; }\n", header=MUTE_HEADER)
try:
    try:
        pp.load(root).manifest()
        check("muting: a raise without its lower is a hard failure", False)
    except pp.Fail as e:
        check("muting: a raise without its lower is a hard failure", "without the other half" in str(e))
finally:
    shutil.rmtree(root, ignore_errors=True)

# the real tree
os.chdir(REPO)
pp.ROOT = REPO
prog = pp.load(REPO)
man = prog.manifest()
cands, missing = pp.audit_diff(man)
check("real tree: the tool classifies every one of the audit's %d lines" % len(cands), missing == [])
check("real tree: the muting sites are found: the recorder and the three that raise the flag", sorted((x["function"], x["role"]) for x in man["muting"]) == [("check_muted", "site"), ("group_fits", "site"), ("ir_assign_to_pending", "site"), ("ir_error", "recorder")])
check("real tree: the sinks are found (%d)" % len(man["sinks"]), len(man["sinks"]) >= 30)
check("real tree: every ir_error call site is an emitter with a diag_ir_ constructor",
      len(man["emitters"]) >= 45 and all(e["constructor"] for e in man["emitters"]))
check("real tree: has_error is derived as a predicate", "has_error" in man["predicates"] and man["aliases"])
check("real tree: the sinks include ones the audit's regex list does not name",
      any(not audit.PAT.search(s["text"]) for s in man["sinks"]))
check("real tree: the test and harness files are not analysed", not any(f in man["files"] for f in ("tools/mannac/ir_test.c", "tools/mannac/ir_main.c")))
check("real tree: the builder header ir_internal.h is analysed (its inline functions are in the manifest)", "tools/mannac/ir_internal.h" in man["files"] and "symtab_lookup" in prog.funcs)
check("real tree: every file in the manifest is repo-relative", all(not os.path.isabs(f) for f in man["files"]))

print("\n%d failure(s)" % len(fails) if fails else "\nall passed")
sys.exit(1 if fails else 0)
