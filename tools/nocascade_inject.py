#!/usr/bin/env python3
"""The single-fault injection sweep over a corpus (typed-IR step 21b; the step 21 plan, section 3b). Run from the repo root:
  python3 tools/nocascade_inject.py [--corpus DIR ...] [--bin PATH] [-j N]

Builds tools/mannac/ir_inject.c with every IR source, -DMANNA_POISON_INJECT, ASan and UBSan (unless --bin is given), runs it on every program of the corpus
(default: every .mn under tests/ that parses and builds clean) and sums the result. The oracle and the output format are in ir_inject.c: a program is
SKIPPED if it does not parse clean or build clean (an erroneous program has nothing to inject into), otherwise every value the builder creates is made ERROR,
silently, one at a time, and the build must report nothing.
Exit status: 0 no finding; 1 findings or a crash (a crash is a finding: the process died under a sanitizer); 2 the tool itself failed (build, I/O).
It never samples: every point of every program is run. Prints the real size (programs, points) and the wall time, which the plan asks the report to carry."""
import glob, os, re, shutil, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

SRCS = "arena ast diag parser_state parser lexer ir ir_dump ir_build ir_decls ir_expr ir_body ir_calls ir_names ir_rename ir_selfcheck ir_inject".split()
OUT = "build/nocascade"


def build(asan=True):
    os.makedirs(OUT, exist_ok=True)
    exe = os.path.join(OUT, "ir_inject_cov" if COV else "ir_inject")
    cmd = ["clang", "-std=c11", "-Wall", "-Wextra"] + ([os.environ["MANNAC_WERROR"]] if os.environ.get("MANNAC_WERROR") else []) + ["-g", "-DMANNA_POISON_INJECT", "-Itools/mannac", "-o", exe]
    cmd += ["-fprofile-instr-generate", "-fcoverage-mapping", "-DNC_COVERAGE", "-O0"] if COV else \
           ["-fsanitize=address,undefined", "-fno-sanitize-recover=undefined"] if asan else ["-O2"]
    cmd += ["tools/mannac/%s.c" % s for s in SRCS]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print("nocascade_inject: the build failed:\n" + r.stderr[:3000])
        sys.exit(2)
    return exe


def default_corpus():
    return sorted(glob.glob("tests/**/*.mn", recursive=True))


COV = None  # --cov DIR: build without sanitizers but with source-based coverage; profiles go to DIR/{base,inj,discard} (tools/nocascade_sinks.py reads them)


def env(phase="inj"):
    leaks = "1" if sys.platform.startswith("linux") else "0"  # LeakSanitizer does not exist on macOS
    e = dict(os.environ, ASAN_OPTIONS="abort_on_error=1:detect_leaks=" + leaks, UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    if COV:
        e["LLVM_PROFILE_FILE"] = os.path.join(COV, phase, "%p.profraw")
        e["NC_DISCARD_PROFILE"] = os.path.join(COV, "discard", "%p.profraw")
        e["NC_NO_COLLAPSE"] = "1"
    return e


SHARD = 1000  # a program with more points than this is split into ranges of this size; every point is still run


def count_one(args):
    exe, path = args
    r = subprocess.run([exe, "--count", path], capture_output=True, text=True, env=env("base"), timeout=1800)
    return path, r.returncode, r.stdout, r.stderr


def run_one(args):
    exe, path, rng = args
    t0 = time.time()
    cmd = [exe] + (["--range", str(rng[0]), str(rng[1])] if rng else []) + [path]
    r = subprocess.run(cmd, capture_output=True, text=True, env=env(), timeout=3600)
    dt = time.time() - t0
    crashed = r.returncode not in (0, 1)
    last = ""
    if crashed:
        t = subprocess.run([exe, "--trace"] + (["--range", str(rng[0]), str(rng[1])] if rng else []) + [path], capture_output=True, text=True, env=env(), timeout=3600)
        ks = re.findall(r"k=(\d+)", t.stderr)
        last = ks[-1] if ks else "?"
    return path, r.returncode, r.stdout, r.stderr, crashed, last, dt


def main(argv):
    corpus_dirs, exe, jobs = [], None, os.cpu_count() or 4
    i = 0
    while i < len(argv):
        if argv[i] == "--corpus": corpus_dirs.append(argv[i + 1]); i += 1
        elif argv[i] == "--bin": exe = argv[i + 1]; i += 1
        elif argv[i] == "-j": jobs = int(argv[i + 1]); i += 1
        elif argv[i] == "--cov":
            global COV
            COV = argv[i + 1]; i += 1
            for sub in ("base", "inj", "discard"):
                shutil.rmtree(os.path.join(COV, sub), ignore_errors=True)
                os.makedirs(os.path.join(COV, sub))
        else: print(__doc__); return 2
        i += 1
    t0 = time.time()
    exe = exe or build()
    t_build = time.time() - t0
    files = sorted(f for d in corpus_dirs for f in [x for x in glob.glob(os.path.join(d, "*")) if os.path.isfile(x)]) if corpus_dirs else default_corpus()
    t1 = time.time()
    with ThreadPoolExecutor(jobs) as ex:
        counted = list(ex.map(count_one, [(exe, f) for f in files]))
    jobs_list, counts, skipped = [], {}, {"parse": 0, "not-clean": 0}
    crashes = 0
    for path, rc, out, err in counted:
        m = re.match(r"COUNT \S+ (\d+)", out)
        sk = re.match(r"SKIP \S+ (\S+)", out)
        if sk:
            skipped[sk.group(1)] += 1
        elif m:
            n = int(m.group(1)); counts[path] = n
            if n <= SHARD: jobs_list.append((exe, path, None))
            else: jobs_list += [(exe, path, (a, min(a + SHARD, n))) for a in range(0, n, SHARD)]
        else:
            crashes += 1
            print("CRASH %s while counting (exit %d)\n%s" % (path, rc, err[:1500]))
    with ThreadPoolExecutor(jobs) as ex:
        results = list(ex.map(run_one, jobs_list))
    t_run = time.time() - t1
    programs = len(counts); points = findings = 0
    collapses = {}
    biggest = max(((n, p) for p, n in counts.items()), default=(0, ""))
    slow = sorted(((r[6], r[0]) for r in results), reverse=True)[:3]
    for path, rc, out, err, crashed, last, dt in results:
        for line in out.split("\n"):
            mc = re.match(r"COLLAPSE (\d+) (.*)", line)
            if mc: collapses[mc.group(2)] = collapses.get(mc.group(2), 0) + int(mc.group(1))
            m = re.match(r"RESULT \S+ points=(\d+) findings=(\d+)", line)
            if m:
                points += int(m.group(1)); findings += int(m.group(2))
        if "FINDING" in out:
            print(out.rstrip())
        if crashed:
            crashes += 1
            print("CRASH %s (exit %d) at k=%s\n%s" % (path, rc, last, err[:1500]))
    print("\nnocascade sweep: %d files; %d skipped (%d do not parse clean, %d do not build clean); %d programs swept, %d injection points, "
          "largest %d points (%s)" % (len(files), sum(skipped.values()), skipped["parse"], skipped["not-clean"], programs, points, biggest[0], biggest[1]))
    if collapses:
        # group by parent kind + operator + where the poison sat; the baseline type is summarised as a class (bool / int / array or slice / struct)
        groups = {}
        for key, n in collapses.items():
            m = re.match(r"(\S+) (\S+) -> (.*?)  \(poison in (\S+): (\S+)\)", key)
            if not m: continue
            kind, op, ty, site, slot = m.groups()
            cls = "bool" if ty == "bool" else "int" if re.fullmatch(r"[ui](8|16|32|64)", ty) else "array/slice" if ty.startswith("[") else "struct"
            g = groups.setdefault((site, kind, op, slot), {})
            g[cls] = g.get(cls, 0) + n
        print("\nWhere an injected poison made a PARENT give up although the baseline run knew its type (the list decision D1 is made from).")
        print("The poison is silent, so these are not cascades: they are diagnostics a user would be shown only after fixing the first mistake.")
        print("  %-9s %-11s %-3s %-8s %8s  baseline type of the parent" % ("poison in", "parent", "op", "slot", "times"))
        for (site, kind, op, slot), g in sorted(groups.items(), key=lambda kv: (kv[0][0] != "expr", kv[0])):
            if site != "expr":
                continue
            print("  %-9s %-11s %-3s %-8s %8d  %s" % (site, kind, op, slot, sum(g.values()), ", ".join("%s x%d" % kv for kv in sorted(g.items()))))
        others = {}
        for (site, kind, op, slot), g in groups.items():
            if site != "expr":
                others[site] = others.get(site, 0) + sum(g.values())
        for site, n in sorted(others.items()):
            print("  (%d parent collapses when the poison was a %s, listed in the raw rows)" % (n, site))
    print("slowest programs (s): " + ", ".join("%.1f %s" % (t, os.path.basename(p)) for t, p in slow))
    print("nocascade sweep: %d finding(s), %d crash(es); build %.1f s, sweep %.1f s on %d workers (%s, every point, no sampling)" %
          (findings, crashes, t_build, t_run, jobs, "coverage build" if COV else "ASan+UBSan"))
    return 1 if findings or crashes else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
