#!/usr/bin/env python3
"""Tests of tools/nocascade_prove.py (typed-IR step 21h): the ablation, the kill rule (a hang or a crash is a kill), the F1/F2/F7 rules. make test-nocascade-prove-script"""
import os, shutil, stat, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import nocascade_prove as np_

fails = []


def check(name, cond):
    print(("PASS: " if cond else "FAIL: ") + name)
    if not cond:
        fails.append(name)


# ---- the ablation text ----
text = "if (has_error(l) || has_error(r)) { x(); }\n"
atoms = [{"start": 4, "end": 16, "replace": "0"}, {"start": 20, "end": 32, "replace": "0"}]
check("patch replaces each atom's byte range by the parenthesised constant, last first", np_.patch(text, atoms) == "if ((0) || (0)) { x(); }\n")
check("patch keeps multi-byte text before an atom intact (offsets are bytes)", np_.patch("é x==1;", [{"start": 3, "end": 7, "replace": "0"}]) == "é (0);")

# ---- what counts as a kill ----
R = "ERRORS:\n1:1 E0230 unknown name x\n"
check("the same output is not a kill", not np_.killed(R, "ok", R))
check("more diagnostics is a kill (a cascade the blind sink lets through)", np_.killed(R, "ok", R + "2:1 E0223 type mismatch\n"))
check("fewer diagnostics is a kill (a mistake the blind sink swallows)", np_.killed(R + "2:1 E0223 type mismatch\n", "ok", R))
check("the SAME NUMBER of diagnostics with different content is a kill (one mistake swapped for another)", np_.killed(R, "ok", "ERRORS:\n1:1 E0260 operator\n"))
check("a different fixed source is a kill (a fix the blind sink offers or withdraws)", np_.killed("let r = a + b;\n", "ok", "let r = u32(a) + b;\n"))
check("a hang is a kill", np_.killed(R, "timeout", ""))
check("a crash or a non-zero exit is a kill", np_.killed(R, "died", ""))
check("kill_kind names how the mutant differs: more, fewer, a swap, a fixed source, a hang, a crash, or nothing",
      [np_.kill_kind(R, "ok", R + "2:1 E0223 x\n"), np_.kill_kind(R + "2:1 E0223 x\n", "ok", R), np_.kill_kind(R, "ok", "ERRORS:\n1:1 E0260 y\n"),
       np_.kill_kind("a\n", "ok", "b\n", True), np_.kill_kind(R, "timeout", ""), np_.kill_kind(R, "died", ""), np_.kill_kind(R, "ok", R)] ==
      ["more", "fewer", "same-count-different", "fixed-source-differs", "hang", "died", None])
check("only fix cases are also run with --fixed", np_.modes("tests/ir_fix/x.mn") == [False, True] and np_.modes("tests/ir/x.mn") == [False] and np_.modes("tests/nocascade/x.mn") == [False])

# ---- running candidates with a fake builder ----
d = tempfile.mkdtemp(prefix="np_test_")


def fake(name, body):
    p = os.path.join(d, name)
    open(p, "w").write("#!/bin/sh\n" + body + "\n")
    os.chmod(p, os.stat(p).st_mode | stat.S_IEXEC)
    return p


try:
    one = fake("one", "echo ERRORS:; echo '3:1 E0230 unknown name x'")
    two = fake("two", "echo ERRORS:; echo '3:1 E0230 unknown name x'; echo '4:1 E0223 type mismatch'")
    hang = fake("hang", "sleep 30")
    crash = fake("crash", "kill -SEGV $$")
    noisy = fake("noisy", "echo ERRORS:; echo '3:1 E0230 unknown name x'; echo 'internal error' 1>&2")
    check("a clean single-diagnostic run is ok with 1 diagnostic", np_.run_one(one, "x.mn", d)[0] == "ok" and np_.ndiags(np_.run_one(one, "x.mn", d)[1]) == 1)
    asrt = fake("asrt", "echo 'Assertion failed: (x), function f, file ir.c, line 9.' 1>&2; exit 134")
    ubsan = fake("ubsan", "echo 'ir_decls.c:249:30: runtime error: division by zero' 1>&2; exit 1")
    asan = fake("asan", "echo '==1==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x1' 1>&2; exit 1")
    selfchk = fake("selfchk", "echo 'ir_dump: internal error: a fix-it did not re-check (please report this): x' 1>&2; exit 4")
    other = fake("other", "echo 'boom' 1>&2; exit 3")
    check("why a mutant died: a signal is named", np_.run_one(crash, "x.mn", d)[2] == "signal 11 (SIGSEGV)")
    check("why a mutant died: an assert", np_.run_one(asrt, "x.mn", d)[2] == "assert")
    check("why a mutant died: a UBSan report keeps its message", np_.run_one(ubsan, "x.mn", d)[2] == "sanitizer (UBSan: division by zero)")
    check("why a mutant died: an ASan report keeps its kind", np_.run_one(asan, "x.mn", d)[2] == "sanitizer (ASan: heap-buffer-overflow)")
    check("why a mutant died: the fix self-check (exit 4)", np_.run_one(selfchk, "x.mn", d)[2].startswith("self-check (exit 4"))
    check("why a mutant died: any other exit says its status and first stderr line", np_.run_one(other, "x.mn", d)[2] == "exit status 3: boom")
    check("a hang says so", np_.run_one(hang, "x.mn", d, timeout=1)[2].startswith("hang"))
    check("two diagnostics are counted", np_.ndiags(np_.run_one(two, "x.mn", d)[1]) == 2)
    check("a program that never returns is a timeout (and the run does not wait for it)", np_.run_one(hang, "x.mn", d, timeout=1)[0] == "timeout")
    check("a crash is 'died'", np_.run_one(crash, "x.mn", d)[0] == "died")
    check("the base kind of a recorded death or hang is 'died' / 'hang'", np_.base_kind("died: signal 8 (SIGFPE)") == "died" and np_.base_kind("hang: hang (no answer in 10 s)") == "hang" and np_.base_kind("more") == "more")
    check("anything on stderr (a failed fix self-check, a sanitizer) is 'died'", np_.run_one(noisy, "x.mn", d)[0] == "died")
    os.makedirs(os.path.join(d, "tests", "ir"))
    for n, declared in (("a", None), ("b", 2)):
        open(os.path.join(d, "tests/ir", n + ".mn"), "w").write("x\n")
        if declared:
            open(os.path.join(d, "tests/ir", n + ".mistakes"), "w").write("%d\n" % declared)
    open(os.path.join(d, "tests/ir", "a.fixed.mn"), "w").write("x\n")
    cands = np_.candidates(d)
    check("candidates: the goldens and their declared mistakes (default 1), never a .fixed.mn", cands == [("tests/ir/a.mn", 1), ("tests/ir/b.mn", 2)])
    check("baseline keeps a candidate whose output is exactly its declared mistakes", list(np_.baseline(one, cands, d)) == [("tests/ir/a.mn", False)])
    check("... and a golden with two declared mistakes needs two diagnostics", list(np_.baseline(two, cands, d)) == [("tests/ir/b.mn", False)])
finally:
    shutil.rmtree(d, ignore_errors=True)

# ---- a mutant that does not compile is a failure of the tool, naming the sink ----
bad = {"id": "ir_expr.c::fake::0::0", "file": "ir_expr.c", "atoms": [{"start": 0, "end": 1, "replace": "@@@"}]}
tmp = tempfile.mkdtemp(prefix="np_bad_")
try:
    try:
        np_.build_mutant(REPO, bad, tmp)
        check("a mutant that does not compile is a hard failure naming the sink", False)
    except np_.Fail as e:
        check("a mutant that does not compile is a hard failure naming the sink", "does not compile" in str(e) and "ir_expr.c::fake::0::0" in str(e))
finally:
    shutil.rmtree(tmp, ignore_errors=True)

# ---- every build is the same on every OS: unset locals are zero, ASan and UBSan are on ----
flags = np_.sanitize_flags()
check("the build flags zero unset locals and turn on ASan and UBSan without recovery", "-ftrivial-auto-var-init=zero" in flags and "-fsanitize=address,undefined" in flags and "-fno-sanitize-recover=undefined" in flags)
tmp2 = tempfile.mkdtemp(prefix="np_zero_")
try:
    src = os.path.join(tmp2, "u.c")
    open(src, "w").write("#include <stdio.h>\nint f(int *out) { int d; *out = 100 / d; return *out; }\nint main(int argc, char **argv) { int o; (void)argv; return f(&o) + argc; }\n")
    import subprocess as sp
    r = sp.run(["clang", "-O1", "-o", os.path.join(tmp2, "u")] + flags + [src], capture_output=True, text=True)
    check("an unset local is zero in a build with these flags", r.returncode == 0)
    st, text, why = np_.run_one(os.path.join(tmp2, "u"), "x", tmp2)
    check("so dividing by one is a UBSan death with the same reason on every OS (here: %s)" % why, st == "died" and "division by zero" in why)
finally:
    shutil.rmtree(tmp2, ignore_errors=True)

# ---- batch mode: the same output as one process per file, with a death or a hang pinned to its file ----
out = "@@@ BEGIN a.mn\nERRORS:\n1:1 E0230 x\n\n@@@ END 0\n@@@ BEGIN b.mn\n\n@@@ END 0\n@@@ BEGIN c.mn\nERR"
frames, culprit = np_.parse_frames(out)
check("parse_frames returns every complete frame with its exact text and names the one that began and did not end", frames == [("a.mn", 0, "ERRORS:\n1:1 E0230 x\n"), ("b.mn", 0, "")] and culprit == "c.mn")
check("a frame whose status is not 0 is returned with that status", np_.parse_frames("@@@ BEGIN a\nx\n@@@ END 4\n")[0] == [("a", 4, "x")])
check("a --fixed output that does not end in a newline survives the frame byte for byte", np_.parse_frames("@@@ BEGIN a\nno newline\n@@@ END 0\n")[0] == [("a", 0, "no newline")])
d2 = tempfile.mkdtemp(prefix="np_batch_")
# the real builder: a batch gives the same bytes as one process per file, for the diagnostics and for the fixed source
real = tempfile.mkdtemp(prefix="np_real_")
try:
    exe = os.path.join(real, "ir_dump")
    import subprocess as sp2
    r = sp2.run(["clang", "-std=c11", "-O1", "-w", "-I", os.path.join(REPO, "tools/mannac"), "-o", exe] + [os.path.join(REPO, "tools/mannac", x + ".c") for x in np_.SRCS], capture_output=True, text=True)
    check("the real builder compiles for the batch test", r.returncode == 0)
    gold = sorted(f for f in ["tests/ir/" + n for n in os.listdir(os.path.join(REPO, "tests/ir")) if n.endswith(".mn")])[:60] + \
           ["tests/ir_fix/" + n for n in sorted(os.listdir(os.path.join(REPO, "tests/ir_fix"))) if n.endswith(".mn") and ".fixed" not in n][:30]
    got = np_.run_batch(exe, gold, REPO)
    same = all(got[g] == (lambda t: (t[0], t[1], t[2]))(np_.run_one(exe, g, REPO)) for g in gold)
    check("a batch of %d goldens gives, for every one, the status, the bytes and the reason of a single run" % len(gold), same and len(got) == len(gold))
    fx = [g for g in gold if g.startswith("tests/ir_fix/")]
    gotf = np_.run_batch(exe, fx, REPO, fixed=True)
    check("... and so does a --fixed batch (%d fix cases)" % len(fx), all(gotf[g] == np_.run_one(exe, g, REPO, fixed=True) for g in fx))
    # a death inside a batch: the culprit is found and the rest still runs
    bad = os.path.join(real, "zz_missing.mn")   # read_file fails: the process exits 1 with a message on stderr
    mixed = gold[:3] + [bad] + gold[3:6]
    gm = np_.run_batch(exe, mixed, REPO)
    check("a file that kills the batch is reported dead with its reason, and the files after it still run", gm[bad][0] == "died" and all(gm[g][0] == "ok" for g in gold[:6]))
finally:
    shutil.rmtree(real, ignore_errors=True)
    shutil.rmtree(d2, ignore_errors=True)

# ---- the rules ----
S = [{"id": "s1"}, {"id": "s2"}]
P = {"s1": ["tests/ir/a.mn"], "s2": []}
lock = {"s1": ["a"], "s2": []}
check("F1/F7: a sink with no proving golden and no owner fails", any("sink s2 has no proving golden and no owner" in p for p in np_.check_rules(S, P, {}, set(), lock, lock)))
check("... it passes when an OPEN register entry owns it", np_.check_rules(S, P, {"s2": "C1"}, {"C1"}, lock, lock) == [])
check("... and fails when the owning entry is not open", any("not an OPEN register entry" in p for p in np_.check_rules(S, P, {"s2": "C1"}, set(), lock, lock)))
check("F7: an owner line for a sink that is proven now must go", any("proven now" in p for p in np_.check_rules(S, {"s1": ["x"], "s2": ["y"]}, {"s2": "C1"}, {"C1"}, lock, lock)))
check("F7: an owner line for a sink that no longer exists must go", any("not a sink any more" in p for p in np_.check_rules(S, P, {"s2": "C1", "gone": "C1"}, {"C1"}, lock, lock)))
check("F2: a lock that differs from what was computed fails and names a sink", any("F2: tests/nocascade/PATHS.lock is stale for 1 sink(s), e.g. s1" in p for p in np_.check_rules(S, P, {"s2": "C1"}, {"C1"}, {"s1": ["zzz"], "s2": []}, lock)))
check("F2: a missing lock fails", any("is missing" in p for p in np_.check_rules(S, P, {"s2": "C1"}, {"C1"}, None, lock)))
check("lock_of keeps the first two proving goldens, sorted by name", np_.lock_of([{"id": "s"}], {"s": ["tests/ir/z.mn", "tests/ir/b.mn", "tests/nocascade/a.mn"]}) == {"s": ["a", "b"]})

print("\n%d failure(s)" % len(fails) if fails else "\nall passed")
sys.exit(1 if fails else 0)
