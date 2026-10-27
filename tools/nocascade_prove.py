#!/usr/bin/env python3
"""The ablation proof of the no-cascade claim (typed-IR step 21h; the step 21 plan, "CI rules"). Run from the repo root:
  python3 tools/nocascade_prove.py [--check] [--write] [--only SINK-ID] [-j N]

For every sink tools/poison_paths.py finds (a decision that reads poison and stays silent), this builds a MUTANT of the builder in which that sink cannot see poison (each of
its poison tests is replaced by the constant that says "not poison", the byte ranges the static tool recorded) and runs every candidate golden on it. A candidate is a golden
whose output, on the real builder, is exactly its declared mistakes (tests/ir, tests/ir_fix originals, tests/nocascade; `NAME.mistakes`, default 1). The candidate PROVES the
sink when the mutant reports a different number of diagnostics (more: a cascade; fewer: a swallowed mistake), crashes, fails its own self-check or hangs (a hang or a crash is a KILL: the guard was holding something up). Only a mutant that
does not COMPILE is a failure of this tool (exit 2), never a skip.
  --write     write tests/nocascade/PATHS.lock: for every sink its first two proving goldens (sorted), so the lock only moves when the proof does
  --check     recompute and compare with the lock, then apply the rules:
                F1/F7  every sink has a proving golden; a sink without one is listed in tests/nocascade/UNPROVEN.txt with an OPEN register entry that owns it
                F2     the lock equals what is computed now
Prints, per sink, how many goldens prove it and the first two.

The real builder and every mutant are built the SAME way on every OS: -O1, ASan + UBSan (no recovery) and -ftrivial-auto-var-init=zero, so a read of an unset local is a zero, an
integer division by it is a UBSan report and no mutant's fate depends on what the stack happened to hold (the layout_struct sink whose mutant read an uninitialised size crashed on macOS
and not on Linux clang 18 before this). A mutant is run only on the candidates that REACH its sink (one instrumented build of the real builder records, per candidate, which sinks it evaluates): a candidate that never evaluates
the sink cannot differ under the mutant, so this is exact, and it is what keeps the sanitizers affordable. Every kill by a DEATH records why it died: a signal, an assert, a sanitizer report, the fix self-check (exit 4), or a hang."""
import glob, json, os, re, shutil, subprocess, sys, tempfile, time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRCDIR = "tools/mannac"
SRCS = "arena ast diag parser_state parser lexer ir ir_dump ir_build ir_decls ir_expr ir_body ir_calls ir_names ir_rename ir_selfcheck ir_main".split()
LOCK = "tests/nocascade/PATHS.lock"
UNPROVEN = "tests/nocascade/UNPROVEN.txt"
TIMEOUT = 10  # seconds per candidate run; a timeout is a kill
DIAG = re.compile(r"^\d+:\d+ E\d{4} ", re.M)


class Fail(Exception):
    pass


def candidates(root):
    """[(path, declared mistakes)]: the goldens whose output may prove a sink; the originals of the fix cases, not their .fixed.mn"""
    out = []
    pats = ["tests/ir/*.mn", "tests/nocascade/*.mn", "tests/ir_fix/*.mn"]
    for pat in pats:
        for p in sorted(glob.glob(os.path.join(root, pat))):
            if re.search(r"\.fixed2?\.mn$", p):
                continue
            side = p[:-3] + ".mistakes"
            out.append((os.path.relpath(p, root), int(open(side).read().strip()) if os.path.exists(side) else 1))
    return out


_SAN = None


def sanitize_flags():
    """the flags every build of this tool uses; probed once. -ftrivial-auto-var-init=zero is spelled differently before clang 16: a compiler that accepts neither is a hard failure"""
    global _SAN
    if _SAN is not None:
        return _SAN
    base = ["-fsanitize=address,undefined", "-fno-sanitize-recover=undefined", "-fno-omit-frame-pointer"]
    probe = tempfile.mkdtemp(prefix="san_probe_")
    try:
        src = os.path.join(probe, "p.c")
        open(src, "w").write("int main(void) { int x; return x; }\n")
        for zero in (["-ftrivial-auto-var-init=zero"],
                     ["-ftrivial-auto-var-init=zero", "-Xclang", "-enable-trivial-auto-var-init-zero-knowing-it-will-be-removed-from-clang"]):
            r = subprocess.run(["clang", "-c", "-o", os.path.join(probe, "p.o")] + base + zero + [src], capture_output=True, text=True)
            if r.returncode == 0:
                _SAN = base + zero
                return _SAN
        raise Fail("this clang accepts neither spelling of -ftrivial-auto-var-init=zero, or has no ASan/UBSan: the proof is not run without them")
    finally:
        shutil.rmtree(probe, ignore_errors=True)


RUN_ENV = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=0")


def why_died(returncode, stderr):
    """a short reason for a death: an assert, a sanitizer report (with its kind), the fix self-check (exit 4), else the signal or the exit status. The text comes first because a
    sanitizer or an assert ends in abort(): the signal alone would say SIGABRT for all of them."""
    import re as _re, signal as _sg
    err = stderr or ""
    if "Assertion failed" in err or ("assertion" in err.lower() and "failed" in err.lower()):
        return "assert"
    m = _re.search(r"runtime error: ([^\n]+)", err)
    if m:
        return "sanitizer (UBSan: %s)" % m.group(1)[:60]
    m = _re.search(r"ERROR: AddressSanitizer: ([^\s]+)", err)
    if m:
        return "sanitizer (ASan: %s)" % m.group(1)
    if returncode == 4 and "did not re-check" in err:
        return "self-check (exit 4: a fix-it did not re-check)"
    if returncode < 0:
        try:
            return "signal %d (%s)" % (-returncode, _sg.Signals(-returncode).name)
        except ValueError:
            return "signal %d" % -returncode
    return "exit status %d%s" % (returncode, (": " + err.strip().split("\n")[0][:60]) if err.strip() else "")


def run_one(binary, path, root, timeout=TIMEOUT, fixed=False):
    """(status, stdout, why): status is 'ok', 'timeout' or 'died' (a signal, a non-zero exit including the self-check's 4, or anything on stderr); `why` explains a death (why_died).
    `fixed` runs `ir_dump --fixed`."""
    try:
        r = subprocess.run([binary] + (["--fixed"] if fixed else []) + [path], cwd=root, capture_output=True, text=True, timeout=timeout, env=RUN_ENV)
    except subprocess.TimeoutExpired:
        return "timeout", "", "hang (no answer in %d s)" % timeout
    if r.returncode != 0 or r.stderr.strip():
        return "died", r.stdout, why_died(r.returncode, r.stderr)
    return "ok", r.stdout, ""


CHUNK = 50          # candidates per batch process
CHUNK_TIMEOUT = 40  # seconds a batch may take; a hang costs one of these, then the culprit is found by its frame


def parse_frames(out):
    """[(path, rc, text)] for every complete frame of a --batch output, and the path of a frame that began and did not end (or None)"""
    frames, i, culprit = [], 0, None
    while True:
        b = out.find("@@@ BEGIN ", i)
        if b < 0:
            break
        nl = out.index("\n", b)
        path = out[b + 10:nl]
        e = out.find("\n@@@ END ", nl + 1)
        if e < 0:
            culprit = path
            break
        end_nl = out.index("\n", e + 1)
        rc = int(out[e + 9:end_nl])
        frames.append((path, rc, out[nl + 1:e]))
        i = end_nl + 1
    return frames, culprit


def run_batch(binary, paths, root, fixed=False):
    """{path: (status, stdout, why)} for many candidates in as few processes as possible. A candidate whose frame does not end, or ends with a non-zero status, is run ALONE
    to get its exact status and the reason it died; the batch resumes after it."""
    results = {}
    remaining = list(paths)
    while remaining:
        chunk, rest = remaining[:CHUNK], remaining[CHUNK:]
        cmd = [binary, "--batch"] + (["--fixed"] if fixed else []) + chunk
        try:
            r = subprocess.run(cmd, cwd=root, capture_output=True, text=True, timeout=CHUNK_TIMEOUT, env=RUN_ENV)
            out = r.stdout
        except subprocess.TimeoutExpired as e:
            out = (e.stdout.decode("utf-8", "replace") if isinstance(e.stdout, bytes) else (e.stdout or ""))
        frames, culprit = parse_frames(out)
        done = 0
        for path, rc, text in frames:
            done += 1
            if rc == 0:
                results[path] = ("ok", text, "")
            else:
                results[path] = run_one(binary, path, root, fixed=fixed)
        if culprit is not None:
            results[culprit] = run_one(binary, culprit, root, fixed=fixed)
            done += 1
        elif done < len(chunk):
            # the process ended early without naming a file (it died before the first frame, or a frame ended with a failure): run the next one alone
            nxt = chunk[done]
            results[nxt] = run_one(binary, nxt, root, fixed=fixed)
            done += 1
        remaining = chunk[done:] + rest
    return results


def ndiags(text):
    return len(DIAG.findall(text))


def modes(path):
    """the ways a candidate is run: the diagnostics always, and for a fix case also the source with every fix applied (a guard on a FIX changes no diagnostic)"""
    return [False, True] if path.startswith("tests/ir_fix/") else [False]


def baseline(binary, cands, root):
    """{(golden, fixed): output} for the single-mistake goldens of the real builder (diagnostics == declared mistakes, at least one); `fixed` runs also need a clean exit"""
    declared = dict(cands)
    plain = run_batch(binary, [p for p, _ in cands], root)
    out = {}
    for path, (st, text, _) in plain.items():
        if st == "ok" and ndiags(text) == declared[path] and declared[path] >= 1:
            out[(path, False)] = text
    fixed_paths = [p for p in sorted({k[0] for k in out}) if True in modes(p)]
    for path, (st2, text2, _) in run_batch(binary, fixed_paths, root, fixed=True).items():
        if st2 == "ok":
            out[(path, True)] = text2
    return out


def kill_kind(base_text, status, text, fixed=False):
    """how the mutant differs from the real builder on one candidate, or None if it does not: 'hang', 'died', 'more' / 'fewer' diagnostics, 'same-count-different' (one diagnostic
    swapped for another), 'fixed-source-differs' (a fix the blind sink offers or withdraws). The golden pins its output exactly (its declared mistakes, and for a fix case the fixed
    source), so MORE is a cascade the blind sink would let through, FEWER a mistake it would swallow, a swap a mistake it would mis-state."""
    if status == "timeout":
        return "hang"
    if status == "died":
        return "died"
    if text == base_text:
        return None
    if fixed:
        return "fixed-source-differs"
    nb, n = ndiags(base_text), ndiags(text)
    return "more" if n > nb else "fewer" if n < nb else "same-count-different"


def killed(base_text, status, text):
    return kill_kind(base_text, status, text) is not None


def patch(text, atoms):
    """`text` with each atom's byte range replaced by `(constant)`; atoms are disjoint, applied from the last to the first"""
    b = text.encode("utf-8")
    for a in sorted(atoms, key=lambda a: -a["start"]):
        b = b[:a["start"]] + ("(%s)" % a["replace"]).encode() + b[a["end"]:]
    return b.decode("utf-8")


def build_mutant(root, sink, outdir):
    """compile the builder with `sink` ablated; returns the binary. A mutant that does not compile is a hard failure."""
    work = os.path.join(outdir, "src")
    shutil.copytree(os.path.join(root, SRCDIR), os.path.join(work, SRCDIR))
    fpath = os.path.join(work, SRCDIR, sink["file"])
    text = open(fpath, encoding="utf-8").read()
    open(fpath, "w", encoding="utf-8").write(patch(text, sink["atoms"]))
    exe = os.path.join(outdir, "ir_dump")
    cmd = ["clang", "-std=c11", "-O1", "-w"] + sanitize_flags() + ["-I", os.path.join(work, SRCDIR), "-o", exe] + [os.path.join(work, SRCDIR, s + ".c") for s in SRCS]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise Fail("the mutant of %s does not compile (a failure of this tool, never a skip):\n%s" % (sink["id"], r.stderr[:1500]))
    return exe


NC_HIT_H = """#include <stdio.h>
#include <stdlib.h>
static inline int nc_hit(int id) {
    const char *p = getenv("NC_HITS");
    if (p) { FILE *f = fopen(p, "a"); if (f) { fprintf(f, "%d\\n", id); fclose(f); } }
    return 0;
}
"""


def patch_reach(text, atoms_with_ids):
    """`text` with each atom `A` replaced by `(nc_hit(i), nc_hit(j), (A))`, i, j... the sinks that own that exact range; ranges are disjoint or identical (anything else is a Fail)"""
    b = text.encode("utf-8")
    by_range = {}
    for a, sid in atoms_with_ids:
        by_range.setdefault((a["start"], a["end"]), []).append(sid)
    keys = sorted(by_range)
    for (s0, e0), (s1, e1) in zip(keys, keys[1:]):
        if s1 < e0:
            raise Fail("two sinks' atoms overlap without being identical (%d-%d and %d-%d): the reach build cannot tell them apart" % (s0, e0, s1, e1))
    for (s0, e0) in sorted(keys, reverse=True):
        hits = ", ".join("nc_hit(%d)" % i for i in sorted(set(by_range[(s0, e0)])))
        b = b[:s0] + ("(%s, (" % hits).encode() + b[s0:e0] + b"))" + b[e0:]
    return b.decode("utf-8")


def reach_map(root, sinks, keys):
    """{sink id: set of (golden, fixed)}: which single-mistake runs evaluate which sink, from ONE build of the real builder in which every atom first records the sink it belongs to"""
    tmp = tempfile.mkdtemp(prefix="reach_")
    try:
        work = os.path.join(tmp, "src")
        shutil.copytree(os.path.join(root, SRCDIR), os.path.join(work, SRCDIR))
        open(os.path.join(work, SRCDIR, "nc_hit.h"), "w").write(NC_HIT_H)
        per_file = {}
        for i, sk in enumerate(sinks):
            for a in sk["atoms"]:
                per_file.setdefault(sk["file"], []).append((a, i))
        for fname, atoms in per_file.items():
            path = os.path.join(work, SRCDIR, fname)
            text = patch_reach(open(path, encoding="utf-8").read(), atoms)
            open(path, "w", encoding="utf-8").write('#include "nc_hit.h"\n' + text)
        exe = os.path.join(tmp, "ir_dump")
        r = subprocess.run(["clang", "-std=c11", "-O1", "-w"] + sanitize_flags() + ["-I", os.path.join(work, SRCDIR), "-o", exe] + [os.path.join(work, SRCDIR, x + ".c") for x in SRCS], capture_output=True, text=True)
        if r.returncode != 0:
            raise Fail("the reach build does not compile:\n" + r.stderr[:1500])
        reached = {sk["id"]: set() for sk in sinks}

        def one(item):
            n, (path, fixed) = item
            hitfile = os.path.join(tmp, "hits_%d.txt" % n)
            env = dict(RUN_ENV, NC_HITS=hitfile)
            try:
                subprocess.run([exe] + (["--fixed"] if fixed else []) + [path], cwd=root, capture_output=True, text=True, timeout=TIMEOUT, env=env)
            except subprocess.TimeoutExpired:
                pass
            ids = set()
            if os.path.exists(hitfile):
                ids = {int(ln) for ln in open(hitfile).read().split("\n") if ln.strip()}
            return (path, fixed), ids

        with ThreadPoolExecutor(os.cpu_count() or 4) as ex:
            for key, ids in ex.map(one, enumerate(keys)):
                for i in ids:
                    reached[sinks[i]["id"]].add(key)
        return reached
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def prove_sink(root, sink, cands, base, binary_env=None, reach=None):
    tmp = tempfile.mkdtemp(prefix="prove_")
    try:
        exe = build_mutant(root, sink, tmp)
        proofs = {}
        for fixed in (False, True):
            keys = [k for k in sorted(base) if k[1] == fixed and (reach is None or k in reach)]   # a candidate that never evaluates the sink cannot differ
            for path, (st, text, why) in run_batch(exe, [k[0] for k in keys], root, fixed=fixed).items():
                k = kill_kind(base[(path, fixed)], st, text, fixed)
                if k:
                    proofs.setdefault(path, set()).add(k if k not in ("died", "hang") else "%s: %s" % (k, why))
        return {p: sorted(k) for p, k in sorted(proofs.items())}
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def lock_of(sinks, proofs):
    return {s["id"]: sorted(os.path.splitext(os.path.basename(p))[0] for p in proofs[s["id"]])[:2] for s in sinks}


def unproven_owners(root):
    """{sink id: register entry} from tests/nocascade/UNPROVEN.txt (`sink id<TAB>entry id`); the entry must be OPEN or PARTLY CLOSED in docs/DEGRADATION.md"""
    path = os.path.join(root, UNPROVEN)
    out = {}
    if os.path.exists(path):
        for ln in open(path).read().split("\n"):
            if ln.strip() and not ln.startswith("#"):
                sid, entry = ln.split("\t")[:2]
                out[sid] = entry
    return out


def open_entries(root):
    text = open(os.path.join(root, "docs", "DEGRADATION.md"), encoding="utf-8").read()
    return {m.group(1) for m in re.finditer(r"^### (\S+) - .*?^- \*\*Status:\*\* ([^\n]*)", text, re.M | re.S) if m.group(2).startswith(("OPEN", "PARTLY CLOSED"))}


DEATH = ("died", "hang")


def base_kind(k):
    """'died: signal 8 (SIGFPE)' -> 'died'; 'more' -> 'more'"""
    return k.split(":")[0]


def report(sinks, kinds):
    """the proven sinks split by HOW the blind mutant differs, written to build/nocascade/prove_report.json. A sink proven ONLY by a death is a CRASH GUARD: blinding it crashes the
    builder (or hangs it), which shows the guard holds the builder up, not that it keeps a wrong diagnostic away; it gets its own row, with the reasons the mutant died."""
    groups = {"more": [], "fewer": [], "same-count-different": [], "fixed-source-differs": [], "death": []}
    rep, whys = {}, {}
    for s in sinks:
        per, death_whys = {}, {}
        for g, ks in kinds[s["id"]].items():
            for k in ks:
                bk = base_kind(k)
                if bk in DEATH:
                    per.setdefault("death", []).append(g)
                    why = k.split(": ", 1)[1] if ": " in k else k
                    death_whys[why] = death_whys.get(why, 0) + 1
                else:
                    per.setdefault(bk, []).append(g)
        rep[s["id"]] = {k: {"goldens": len(v), "first": sorted(os.path.splitext(os.path.basename(x))[0] for x in set(v))[:2]} for k, v in per.items()}
        if death_whys:
            rep[s["id"]]["death"]["why"] = death_whys
            whys[s["id"]] = death_whys
        for k in per:
            groups[k].append(s["id"])
    os.makedirs("build/nocascade", exist_ok=True)
    json.dump(rep, open("build/nocascade/prove_report.json", "w"), indent=1, sort_keys=True)
    only = {k: [i for i in ids if set(rep[i]) <= {k}] for k, ids in groups.items()}
    print("\nHOW THE BLIND MUTANT DIFFERS (a sink may appear in several rows):")
    for k, ids in groups.items():
        print("  %-22s %2d sinks (%d of them only this way)" % (k, len(ids), len(only[k])))
    if only["death"]:
        print("\nPROVEN ONLY BY A DEATH (%d sinks: crash guards, not shown to keep a wrong diagnostic away):" % len(only["death"]))
        for sid in only["death"]:
            print("  %-52s %s" % (sid[:52], "; ".join("%s x%d" % (w, n) for w, n in sorted(whys[sid].items(), key=lambda x: -x[1])[:3])))


def check_rules(sinks, proofs, owners, live, old_lock, lock):
    """the problems F1/F7/F2 find: every sink proven or owned by an OPEN register entry; no stale owner line; the lock equals what was computed"""
    problems = []
    for s in sinks:
        if not proofs[s["id"]]:
            if s["id"] not in owners:
                problems.append("F1/F7: sink %s has no proving golden and no owner in %s (a golden whose mistakes make it report more when the sink is blind, or a register entry)" % (s["id"], UNPROVEN))
            elif owners[s["id"]] not in live:
                problems.append("F1/F7: sink %s is owned by %s, which is not an OPEN register entry" % (s["id"], owners[s["id"]]))
    for sid in owners:
        if sid not in proofs:
            problems.append("F7: %s lists %s, which is not a sink any more" % (UNPROVEN, sid))
        elif proofs[sid]:
            problems.append("F7: %s lists %s, which is proven now: remove the line" % (UNPROVEN, sid))
    if old_lock is None:
        problems.append("F2: %s is missing (run with --write)" % LOCK)
    elif old_lock != lock:
        diff = sorted(k for k in set(old_lock) | set(lock) if old_lock.get(k) != lock.get(k))
        problems.append("F2: %s is stale for %d sink(s), e.g. %s (run python3 tools/nocascade_prove.py --write and read the diff)" % (LOCK, len(diff), diff[0]))
    return problems


def main(argv):
    root = ROOT
    sys.path.insert(0, os.path.join(root, "tools"))
    import poison_paths
    only = argv[argv.index("--only") + 1] if "--only" in argv else None
    jobs = int(argv[argv.index("-j") + 1]) if "-j" in argv else (os.cpu_count() or 4)
    man = poison_paths.load(root).manifest()
    sinks = [s for s in man["sinks"] + man["muting"] if only is None or s["id"] == only]   # the poison sinks and the muting sites (step 21i): both are ablated and proven the same way
    if only and not sinks:
        raise Fail("no sink %s" % only)
    t0 = time.time()
    tmp = tempfile.mkdtemp(prefix="prove_base_")
    try:
        # the real builder, built once
        exe = os.path.join(tmp, "ir_dump")
        r = subprocess.run(["clang", "-std=c11", "-O1", "-w"] + sanitize_flags() + ["-I", os.path.join(root, SRCDIR), "-o", exe] + [os.path.join(root, SRCDIR, s + ".c") for s in SRCS], capture_output=True, text=True)
        if r.returncode != 0:
            raise Fail("the real builder does not compile:\n" + r.stderr[:1500])
        cands = candidates(root)
        base = baseline(exe, cands, root)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    reached = reach_map(root, sinks, sorted(base))
    print("reach: %d (sink, run) pairs; the busiest sink is evaluated by %d runs, %d sinks by none" %
          (sum(len(v) for v in reached.values()), max(len(v) for v in reached.values()), sum(1 for v in reached.values() if not v)))
    with ThreadPoolExecutor(jobs) as ex:
        results = list(ex.map(lambda s: prove_sink(root, s, cands, base, reach=reached[s["id"]]), sinks))
    kinds = {s["id"]: r for s, r in zip(sinks, results)}   # sink -> {golden: [kinds]}
    proofs = {sid: sorted(r) for sid, r in kinds.items()}
    elapsed = time.time() - t0
    print("%d candidate goldens (%d single-mistake on the real builder, %d runs counting the fixed-source runs), %d sinks, %.0f s on %d workers" % (len(cands), len({k[0] for k in base}), len(base), len(sinks), elapsed, jobs))
    for s in sinks:
        p = proofs[s["id"]]
        print("  %-52s %4d  %s" % (s["id"][:52], len(p), ", ".join(os.path.splitext(os.path.basename(x))[0] for x in p[:2]) if p else "UNPROVEN"))
    if "--report" in argv:
        report(sinks, kinds)
    lock = lock_of(sinks, proofs)
    lockfile = os.path.join(root, LOCK)
    if "--write" in argv:
        if only:
            raise Fail("--write needs every sink (no --only)")
        json.dump(lock, open(lockfile, "w"), indent=1, sort_keys=True)
        open(lockfile, "a").write("\n")
        print("wrote %s (%d sinks)" % (LOCK, len(lock)))
    if "--check" in argv:
        old = json.load(open(lockfile)) if os.path.exists(lockfile) else None
        problems = check_rules(sinks, proofs, unproven_owners(root), open_entries(root), old, lock)
        for p in problems:
            print(p)
        if problems:
            return 1
        print("nocascade_prove: ok (%d sinks, every one proven by a golden or owned by an open register entry; the lock is current)" % len(sinks))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except Fail as e:
        print("nocascade_prove: " + str(e), file=sys.stderr)
        sys.exit(2)
