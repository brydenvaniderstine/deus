#!/usr/bin/env python3
"""Cross-check of the static sinks against the dynamic ones (typed-IR step 21c; the step 21 plan, section 3c). Run from the repo root:
  python3 tools/nocascade_sinks.py [--reuse]            report (exit 0 unless the tool itself fails); --reuse keeps the profiles of the last sweep

The dynamic side needs no names. Two coverage profiles of the same binary (tools/mannac/ir_inject.c, -fprofile-instr-generate):
  B  every program of the corpus that builds CLEAN, built once with no injection;
  I  every injection point of every such program (tools/nocascade_inject.py --cov DIR, nothing sampled).
A region or a branch direction that I executes and B never does is code that only runs when a value is poison: the poison-only code. A static sink (tools/poison_paths.py)
is CONFIRMED when poison-only code lies inside its extent (its if / while / for / ?: or its statement). The report lists
  - the static sinks no clean-corpus injection moved (they need a golden with a real mistake: the E0261 `pending` state, for one, cannot be injected);
  - the poison-only code outside every static sink, by function: either a sink the static closure missed or propagation code that only a poisoned value reaches.
Both lists are what step 21h turns into CI rules F3/F7; this step only measures them. Profiles are read with the llvm tools (tools/llvm_tool.sh)."""
import glob, json, os, subprocess, sys

ROOT = os.getcwd()
COV = "build/nocascade/cov"
EXE = "build/nocascade/ir_inject_cov"


def tool(name):
    r = subprocess.run(["sh", "tools/llvm_tool.sh", name], capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit("nocascade_sinks: " + r.stderr.strip())
    return r.stdout.split()


def export(exe, sub, wanted):
    raws = [r for r in glob.glob(os.path.join(COV, sub, "*.profraw")) if os.path.getsize(r) > 0]  # a program that did not build clean left an empty file here
    if not raws:
        raise SystemExit("nocascade_sinks: no profiles in %s/%s (run the sweep with --cov first)" % (COV, sub))
    prof = os.path.join(COV, sub + ".profdata")
    subprocess.run(tool("llvm-profdata") + ["merge", "-sparse", "-o", prof] + raws, check=True)
    j = json.loads(subprocess.run(tool("llvm-cov") + ["export", exe, "-instr-profile=" + prof], capture_output=True, text=True, check=True).stdout)
    out = {}
    for fl in j["data"][0]["files"]:
        b = os.path.basename(fl["filename"])
        if b in wanted:
            out[b] = {"segments": fl["segments"], "branches": {tuple(x[:4]): (x[4], x[5]) for x in fl.get("branches", [])}}
    return out, len(raws)


def count_at(segs, line, col):
    """the execution count of the region that covers (line, col); the two profiles have different segment lists (llvm-cov merges neighbours with equal counts)"""
    lo, hi = 0, len(segs)
    while lo < hi:
        mid = (lo + hi) // 2
        if (segs[mid][0], segs[mid][1]) <= (line, col): lo = mid + 1
        else: hi = mid
    i = lo - 1
    return segs[i][2] if i >= 0 and segs[i][3] else 0


def poison_only(B, I):
    """[(file, line, col, kind)]: regions and branch directions executed in I and never in B (B and I are {file: {segments, branches}} of one binary)"""
    out = []
    for f, ci in I.items():
        cb = B.get(f)
        if not cb:
            raise SystemExit("nocascade_sinks: the clean-build profile has no %s" % f)
        for si in ci["segments"]:
            # [line, col, count, hasCount, isRegionEntry, isGap]
            if si[3] and si[4] and not si[5] and si[2] > 0 and count_at(cb["segments"], si[0], si[1]) == 0:
                out.append((f, si[0], si[1], "region"))
        for key, (ti, fi) in ci["branches"].items():
            tb, fb = cb["branches"].get(key, (0, 0))
            if ti > 0 and tb == 0:
                out.append((f, key[0], key[1], "branch-true"))
            if fi > 0 and fb == 0:
                out.append((f, key[0], key[1], "branch-false"))
    return sorted(set(out))


def attribute(items, man):
    """-> (confirmed: {sink id: [items]}, outside: {(file, function): [items]})  the innermost sink extent wins"""
    confirmed, outside = {}, {}
    sinks = man["sinks"]
    for it in items:
        f, line = it[0], it[1]
        best = None
        for s in sinks:
            if s["file"] == f and s["extent"][0] <= line <= s["extent"][1]:
                if best is None or (s["extent"][1] - s["extent"][0]) < (best["extent"][1] - best["extent"][0]):
                    best = s
        if best:
            confirmed.setdefault(best["id"], []).append(it)
            continue
        fn = next((x["function"] for x in man["functions"] if x["file"] == f and x["start"] <= line <= x["end"]), "?")
        outside.setdefault((f, fn), []).append(it)
    return confirmed, outside


def main(argv):
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import poison_paths
    man = poison_paths.load().manifest()  # live, with the line extents; the committed tests/nocascade/sinks.json holds the identity only
    wanted = {s["file"] for s in man["sinks"]} | {x["file"] for x in man["functions"]}
    if "--reuse" not in argv:
        r = subprocess.run([sys.executable, "tools/nocascade_inject.py", "--cov", COV], capture_output=True, text=True)
        sys.stdout.write("\n".join(l for l in r.stdout.split("\n") if l.startswith("nocascade sweep")) + "\n")
        if r.returncode != 0:
            raise SystemExit("nocascade_sinks: the coverage sweep failed:\n" + r.stdout[-3000:] + r.stderr[-1500:])
    B, nb = export(EXE, "base", wanted)
    I, ni = export(EXE, "inj", wanted)
    items = poison_only(B, I)
    confirmed, outside = attribute(items, man)
    sinks = man["sinks"]
    unconfirmed = [s for s in sinks if s["id"] not in confirmed]
    print("profiles: %d clean-build runs (B), %d injected shards (I); %d poison-only regions/branch directions in the builder files" % (nb, ni, len(items)))
    print("static sinks %d: %d confirmed by poison-only code in their extent, %d not" % (len(sinks), len(sinks) - len(unconfirmed), len(unconfirmed)))
    print("\nSTATIC SINKS NO CLEAN-CORPUS INJECTION MOVED (%d):" % len(unconfirmed))
    for s in unconfirmed:
        print("  %-11s %-26s %5d  %s" % (s["file"], s["function"], s["line"], s["text"][:72]))
    n_out = sum(len(v) for v in outside.values())
    print("\nPOISON-ONLY CODE OUTSIDE EVERY STATIC SINK (%d items in %d functions):" % (n_out, len(outside)))
    produ, report, preds = set(man["producers"]), set(man.get("reporters", [])), set(man["predicates"])
    for (f, fn), its in sorted(outside.items()):
        tag = "hook" if fn == "?" and f == "ir.c" else "producer" if fn in produ else "predicate" if fn in preds else "reporter" if fn in report else "other"
        lines = sorted({i[1] for i in its})
        print("  %-11s %-26s %-8s lines %s" % (f, fn, tag, " ".join(str(l) for l in lines[:12]) + (" ..." if len(lines) > 12 else "")))
    os.makedirs(os.path.dirname(COV), exist_ok=True)
    json.dump({"unconfirmed": [s["id"] for s in unconfirmed], "outside": {"%s::%s" % k: sorted({i[1] for i in v}) for k, v in outside.items()},
               "confirmed": {k: len(v) for k, v in confirmed.items()}}, open(os.path.join("build/nocascade", "sinks_report.json"), "w"), indent=1, sort_keys=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
