#!/usr/bin/env python3
"""Every generated golden is what its generator writes TODAY (typed-IR step 21g, the batch B review's finding). Run from the repo root: python3 tools/check_generated.py [ROOT]

Runs every tools/gen_ir_*.py in a temporary tree (no arguments, `tests/ir`, `tests/ir_fix` and `tests/nocascade` empty there, the generators importing each other from
the real tools/), then compares every file they wrote with the committed copy byte for byte. Fails on
  - a generator that exits non-zero;
  - a generated file that is missing in the repository or differs (a golden edited by hand, or by a script, without its generator: the 21d edits of five expectations
    would have been caught here);
  - a `.mistakes` file (decision D5: the mistake count of a golden, written by the generator that wrote the golden) that no generator writes.
Goldens that no generator writes are hand-written and are not checked here (tests/ir has 82 of them). Nothing is skipped: a missing generator output is a failure."""
import glob, os, shutil, subprocess, sys, tempfile


def run(root):
    problems, generated = [], {}
    tmp = tempfile.mkdtemp(prefix="check_generated_")
    try:
        for d in ("tests/ir", "tests/ir_fix", "tests/nocascade"):
            os.makedirs(os.path.join(tmp, d))
        gens = sorted(glob.glob(os.path.join(root, "tools", "gen_ir_*.py")))
        env = dict(os.environ, PYTHONPATH=os.path.join(root, "tools"))
        for g in gens:
            r = subprocess.run([sys.executable, g], cwd=tmp, env=env, capture_output=True, text=True)
            if r.returncode != 0:
                problems.append("%s exited %d: %s" % (os.path.basename(g), r.returncode, (r.stderr or r.stdout).strip()[-300:]))
        for dirpath, _, names in os.walk(os.path.join(tmp, "tests")):
            for n in names:
                p = os.path.join(dirpath, n)
                generated[os.path.relpath(p, tmp)] = open(p, "rb").read()
        for rel, data in sorted(generated.items()):
            committed = os.path.join(root, rel)
            if not os.path.exists(committed):
                problems.append("%s is written by a generator but is not in the repository" % rel)
            elif open(committed, "rb").read() != data:
                problems.append("%s differs from what its generator writes (a golden changed without its generator)" % rel)
        for p in glob.glob(os.path.join(root, "tests", "**", "*.mistakes"), recursive=True):
            rel = os.path.relpath(p, root)
            if rel not in generated:
                problems.append("%s is a mistake count that no generator writes" % rel)
        return problems, len(gens), len(generated)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main(argv):
    root = os.path.abspath(argv[0]) if argv else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    problems, ngen, nfiles = run(root)
    for p in problems:
        print("check_generated: " + p)
    if problems:
        return 1
    print("check_generated: ok (%d generators, %d generated files all equal to the committed ones, every .mistakes file written by a generator)" % (ngen, nfiles))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
