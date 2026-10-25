#!/usr/bin/env python3
"""Every test target the Makefile defines is run by CI (step 20c; CLAUDE.md "Verify CI actually runs the tests you think it does").

A target is a *check* if its name starts with test-, audit- or check-. Each must be run by a `run:` step (outside YAML comments, as a command that
starts with `make`, not inside an echo) of .github/workflows/ci.yml or docs.yml, and that step must be able to fail the job: a step with
`continue-on-error: true`, `if: false`, or a command followed by `|| true` does not count (Linux review of batch A, finding 4). Unless it is in SKIPPED below with a register entry that is still open (the entry says why and when it is wired; a closed entry
makes the skip stale and the check fails). A `make <target>` step in a workflow whose target the Makefile does not define fails too.
Run from the repo root: python3 tools/check_ci_targets.py [root]  (make check-ci-targets)"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIPPED = {"test-wx": "K4"}   # the QEMU W^X test: wired by step 20h
PREFIXES = ("test-", "audit-", "check-")
WORKFLOWS = (".github/workflows/ci.yml", ".github/workflows/docs.yml", ".github/workflows/fuzz.yml")


def read(root, path):
    try:
        with open(os.path.join(root, path)) as f:
            return f.read()
    except OSError:
        return ""


def strip_comment(line):
    """a YAML comment: a # at the start of the line's content or after whitespace, outside quotes"""
    q = None
    for i, c in enumerate(line):
        if q:
            if c == q:
                q = None
        elif c in "\"'":
            q = c
        elif c == "#" and (i == 0 or line[i - 1] in " \t"):
            return line[:i].rstrip()
    return line.rstrip()


def steps(text):
    """[(step lines)] of every list item of a workflow: an item ends at the next non-blank line indented at most as much as its dash"""
    lines = [strip_comment(l) for l in text.split("\n")]
    out, i = [], 0
    while i < len(lines):
        m = re.match(r"^(\s*)- ", lines[i])
        if not m:
            i += 1
            continue
        ind = len(m.group(1))
        j = i + 1
        while j < len(lines) and (not lines[j].strip() or len(lines[j]) - len(lines[j].lstrip()) > ind):
            j += 1
        out.append(lines[i:j])
        i += 1   # nested items are steps of their own only if they are themselves list items; the outer item stays whole
    return out


def run_text(step):
    """the shell text of the step's `run:` key (single line or block scalar), or None"""
    for k, line in enumerate(step):
        body = re.sub(r"^\s*- ", "  ", line) if k == 0 else line
        m = re.match(r"^(\s*)run:\s*(.*)$", body)
        if not m:
            continue
        key_ind, val = len(m.group(1)), m.group(2).strip()
        if val and val[0] not in "|>":
            return val
        block = []
        for l in step[k + 1:]:
            if l.strip() and len(l) - len(l.lstrip()) <= key_ind:
                break
            block.append(l.strip())
        return "\n".join(block)
    return None


def make_targets(shell):
    """({target}, [reasons the step cannot fail the job]) for the commands of a run text that start with `make`"""
    found, why = set(), []
    shell = shell.replace("\\\n", " ")
    if re.search(r"\|\|\s*(true|:|exit\s+0)\b", shell):
        why.append("`|| true`")
    for cmd in re.split(r"&&|\|\||;|\||\n", shell):
        toks = cmd.split()
        while toks and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*=\S*", toks[0]):
            toks = toks[1:]
        if toks and toks[0] == "make":
            found |= {t for t in toks[1:] if not t.startswith("-") and "=" not in t}
    return found, why


def check(root):
    problems = []
    mk = read(root, "Makefile")
    phony = set()
    for m in re.finditer(r"^\.PHONY:(.*(?:\\\n.*)*)", mk, re.M):
        phony |= set(m.group(1).replace("\\\n", " ").split())
    defined = set(re.findall(r"^([A-Za-z0-9_.-]+):(?!=)", mk, re.M))
    checks = sorted(t for t in phony if t.startswith(PREFIXES) and t in defined)
    for t in sorted(t for t in phony if t.startswith(PREFIXES) and t not in defined):
        problems.append("Makefile: %s is in .PHONY but has no rule" % t)
    run, unreliable = set(), {}
    for w in WORKFLOWS:
        for st in steps(read(root, w)):
            shell = run_text(st)
            if shell is None:
                continue
            targets, why = make_targets(shell)
            body = "\n".join(st)
            if re.search(r"^\s*(- )?continue-on-error:\s*true\b", body, re.M):
                why.append("continue-on-error: true")
            if re.search(r"^\s*(- )?if:\s*false\s*$", body, re.M):
                why.append("if: false")
            for t in targets:
                (unreliable.setdefault(t, why) if why else run.add(t))
    reg = read(root, "docs/DEGRADATION.md")
    for t in checks:
        if t in unreliable and t not in run:
            problems.append("`make %s` runs in a step that cannot fail the job (%s)" % (t, ", ".join(unreliable[t])))
            continue
        if t in run:
            if t in SKIPPED:
                problems.append("%s is run by CI but is still in SKIPPED (remove it)" % t)
            continue
        if t not in SKIPPED:
            problems.append("`make %s` is a check target that no workflow runs (add a step, or a SKIPPED entry tied to an open register entry)" % t)
            continue
        m = re.search(r"^### %s - .*?^- \*\*Status:\*\* (\S+)" % re.escape(SKIPPED[t]), reg, re.M | re.S)
        if not m or m.group(1).startswith("CLOSED"):
            problems.append("%s is skipped because of register entry %s, which is %s" % (t, SKIPPED[t], "missing" if not m else "CLOSED"))
    for t in sorted((run | set(unreliable)) - defined - {"all"}):
        if t not in phony and t not in defined:
            problems.append("a workflow runs `make %s`, which the Makefile does not define" % t)
    # -Werror (step 20g-2): every host-side compile recipe takes $(MANNAC_WERROR), and each CI job sets MANNAC_WERROR: -Werror, so no warning returns
    for n, line in enumerate(mk.split("\n"), 1):
        if line.startswith("\t$(MANNAC_CC) -std=c11") and "$(MANNAC_WERROR)" not in line:
            problems.append("Makefile:%d: a host-side compile without $(MANNAC_WERROR)" % n)
    ci = read(root, ".github/workflows/ci.yml")
    jobs = re.split(r"^  (?=[a-z][a-z0-9_-]*:\s*$)", ci, flags=re.M)
    for j in jobs:
        m = re.match(r"([a-z][a-z0-9_-]*):", j)
        if m and "runs-on:" in j and not re.search(r"^\s+MANNAC_WERROR:\s*-Werror\s*$", j, re.M):
            problems.append("ci.yml: job '%s' does not set MANNAC_WERROR: -Werror" % m.group(1))
    return problems, checks, run


def main(argv):
    root = argv[1] if len(argv) > 1 else ROOT
    problems, checks, run = check(root)
    if problems:
        print("check_ci_targets: FAILED")
        for p in problems:
            print("  " + p)
        return 1
    print("check_ci_targets: ok (%d check targets; %d run by CI, %d skipped with an open register entry: %s)" %
          (len(checks), sum(t in run for t in checks), sum(t not in run for t in checks), ", ".join("%s (%s)" % (t, SKIPPED[t]) for t in sorted(SKIPPED) if t not in run) or "none"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
