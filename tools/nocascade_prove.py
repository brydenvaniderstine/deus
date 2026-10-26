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
Prints, per sink, how many goldens prove it and the first two."""
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


def run_one(binary, path, root, timeout=TIMEOUT, fixed=False):
    """(status, stdout): status is 'ok', 'timeout' or 'died' (a signal, a non-zero exit including the self-check's 4, or anything on stderr). `fixed` runs `ir_dump --fixed`."""
    try:
        r = subprocess.run([binary] + (["--fixed"] if fixed else []) + [path], cwd=root, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return "timeout", ""
    if r.returncode != 0 or r.stderr.strip():
        return "died", r.stdout
    return "ok", r.stdout


def ndiags(text):
    return len(DIAG.findall(text))


def modes(path):
    """the ways a candidate is run: the diagnostics always, and for a fix case also the source with every fix applied (a guard on a FIX changes no diagnostic)"""
    return [False, True] if path.startswith("tests/ir_fix/") else [False]


def baseline(binary, cands, root):
    """{(golden, fixed): output} for the single-mistake goldens of the real builder (diagnostics == declared mistakes, at least one); `fixed` runs also need a clean exit"""
    out = {}
    for path, declared in cands:
        st, text = run_one(binary, path, root)
        if not (st == "ok" and ndiags(text) == declared and declared >= 1):
            continue
        out[(path, False)] = text
        if True in modes(path):
            st2, text2 = run_one(binary, path, root, fixed=True)
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
    cmd = ["clang", "-std=c11", "-O1", "-w", "-I", os.path.join(work, SRCDIR), "-o", exe] + [os.path.join(work, SRCDIR, s + ".c") for s in SRCS]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise Fail("the mutant of %s does not compile (a failure of this tool, never a skip):\n%s" % (sink["id"], r.stderr[:1500]))
    return exe


def prove_sink(root, sink, cands, base, binary_env=None):
    tmp = tempfile.mkdtemp(prefix="prove_")
    try:
        exe = build_mutant(root, sink, tmp)
        proofs = {}
        for (path, fixed), real in sorted(base.items()):
            st, text = run_one(exe, path, root, fixed=fixed)
            k = kill_kind(real, st, text, fixed)
            if k:
                proofs.setdefault(path, set()).add(k)
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


def report(sinks, kinds):
    """the proven sinks split by HOW the blind mutant differs (decision for Bryden: whether 'fewer' and the other kinds count as proof): written to build/nocascade/prove_report.json"""
    groups = {"more": [], "fewer": [], "same-count-different": [], "fixed-source-differs": [], "hang-or-died": []}
    rep = {}
    for s in sinks:
        per = {}
        for g, ks in kinds[s["id"]].items():
            for k in ks:
                per.setdefault("hang-or-died" if k in ("hang", "died") else k, []).append(g)
        rep[s["id"]] = {k: {"goldens": len(v), "first": sorted(os.path.splitext(os.path.basename(x))[0] for x in v)[:2]} for k, v in per.items()}
        for k in per:
            groups[k].append(s["id"])
    os.makedirs("build/nocascade", exist_ok=True)
    json.dump(rep, open("build/nocascade/prove_report.json", "w"), indent=1, sort_keys=True)
    only = {k: [i for i in ids if set(rep[i]) <= {k}] for k, ids in groups.items()}
    print("\nHOW THE BLIND MUTANT DIFFERS (a sink may appear in several rows):")
    for k, ids in groups.items():
        print("  %-22s %2d sinks (%d of them only this way)" % (k, len(ids), len(only[k])))


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
    sinks = [s for s in man["sinks"] if only is None or s["id"] == only]
    if only and not sinks:
        raise Fail("no sink %s" % only)
    t0 = time.time()
    tmp = tempfile.mkdtemp(prefix="prove_base_")
    try:
        # the real builder, built once
        exe = os.path.join(tmp, "ir_dump")
        r = subprocess.run(["clang", "-std=c11", "-O1", "-w", "-I", os.path.join(root, SRCDIR), "-o", exe] + [os.path.join(root, SRCDIR, s + ".c") for s in SRCS], capture_output=True, text=True)
        if r.returncode != 0:
            raise Fail("the real builder does not compile:\n" + r.stderr[:1500])
        cands = candidates(root)
        base = baseline(exe, cands, root)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    with ThreadPoolExecutor(jobs) as ex:
        results = list(ex.map(lambda s: prove_sink(root, s, cands, base), sinks))
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
