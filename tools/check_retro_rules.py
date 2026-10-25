#!/usr/bin/env python3
"""Every rule has a check, or sits in the register as unenforced (step 20i; CLAUDE.md "No degradation": "Every retro rule lives in a check, or sits in the register as
'unenforced' with a trigger"). Run from the repo root: python3 tools/check_retro_rules.py  (make check-retro-rules)

The inventory is the table under "## Retro-rule inventory" in docs/REFINEMENT.md: | Source | Anchor | Enforced by |. The checker enumerates the rules itself:
  - CLAUDE.md "## Lessons": every bullet;
  - docs/REFINEMENT.md: every line holding "**The rule.**";
  - CLAUDE.md "## No degradation": every bullet (before the fix-it policy paragraph);
  - CLAUDE.md "## Working conventions": every bullet and sub-bullet;
and fails when a rule has no row (a row's Anchor is a quote of at least 25 characters found on that rule's line), when an Anchor is on no rule, when an "Enforced by" item names
a make target that does not exist or that no workflow runs, or a register entry that does not exist or is CLOSED (a CLOSED entry enforces nothing). Items are separated by ';':
`make <target>` or `register <ID>`. A row with only register items is an UNENFORCED rule; a row with a make item AND a register item is PARTLY enforced (the check covers part of the rule, the register entry the rest; Linux review of batch C, finding 4). Three counts are printed: enforced (make only), partly, unenforced.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))


def read(root, path):
    try:
        with open(os.path.join(root, path), encoding="utf-8") as f:
            return f.read()
    except OSError:
        return ""


def section(text, heading, stop=r"^## "):
    m = re.search(r"^## %s.*$" % re.escape(heading), text, re.M)
    if not m:
        return []
    rest = text[m.end():]
    n = re.search(stop, rest, re.M)
    return rest[:n.start()].split("\n") if n else rest.split("\n")


def rules(root):
    """[(source file, line text)] for every rule"""
    out = []
    claude = read(root, "CLAUDE.md")
    ref = read(root, "docs/REFINEMENT.md")
    out += [("CLAUDE.md", l) for l in section(claude, "Lessons") if l.startswith("- ")]
    out += [("docs/REFINEMENT.md", l) for l in ref.split("\n") if "**The rule.**" in l]
    nd = []
    for l in section(claude, "No degradation"):
        if l.startswith("**Fix-it policy"):
            break
        nd.append(l)
    out += [("CLAUDE.md", l) for l in nd if l.startswith("- ")]
    out += [("CLAUDE.md", l) for l in section(claude, "Working conventions") if re.match(r"^\s*- ", l)]
    return out


def inventory(root):
    rows = []
    for l in section(read(root, "docs/REFINEMENT.md"), "Retro-rule inventory"):
        m = re.match(r"^\| (CLAUDE\.md|docs/REFINEMENT\.md) \| (.+?) \| (.+?) \|\s*$", l)
        if m:
            rows.append((m.group(1), m.group(2).strip().strip('"'), m.group(3).strip()))
    return rows


def register_status(root):
    out = {}
    text = read(root, "docs/DEGRADATION.md")
    for m in re.finditer(r"^### (\S+) - .*?^- \*\*Status:\*\* ([^\n]*)", text, re.M | re.S):
        out[m.group(1)] = m.group(2)
    return out


def check(root):
    problems = []
    rs = rules(root)
    rows = inventory(root)
    if not rows:
        return ["docs/REFINEMENT.md has no '## Retro-rule inventory' table"], 0, 0, 0
    import check_ci_targets as ct
    ct_problems, checks, run = ct.check(root)
    mk = read(root, "Makefile")
    defined = set(re.findall(r"^([A-Za-z0-9_.-]+):(?!=)", mk, re.M))
    reg = register_status(root)
    covered = set()
    unenforced = partly = 0
    for src, anchor, enforced in rows:
        if len(anchor) < 25:
            problems.append("anchor %r is shorter than 25 characters" % anchor)
        hits = [i for i, (s, l) in enumerate(rs) if s == src and anchor in l]
        if not hits:
            problems.append("the anchor %r is on no rule of %s" % (anchor[:50], src))
        covered.update(hits)
        items = [x.strip() for x in enforced.split(";") if x.strip()]
        if not items:
            problems.append("no 'Enforced by' for %r" % anchor[:50])
        only_register = bool(items)
        has_register = False
        for it in items:
            m = re.fullmatch(r"make ([A-Za-z0-9_.-]+)", it)
            r = re.fullmatch(r"register (\S+)", it)
            if m:
                only_register = False
                t = m.group(1)
                if t not in defined:
                    problems.append("%r: `make %s` is not a Makefile target" % (anchor[:40], t))
                elif t not in run:
                    problems.append("%r: `make %s` is run by no workflow" % (anchor[:40], t))
            elif r:
                has_register = True
                st = reg.get(r.group(1))
                if st is None:
                    problems.append("%r: register entry %s does not exist" % (anchor[:40], r.group(1)))
                elif st.startswith("CLOSED"):
                    problems.append("%r: register entry %s is CLOSED and enforces nothing" % (anchor[:40], r.group(1)))
            else:
                problems.append("%r: cannot read the item %r (use `make <target>` or `register <ID>`)" % (anchor[:40], it))
        unenforced += only_register
        partly += (not only_register) and has_register
    for i, (s, l) in enumerate(rs):
        if i not in covered:
            problems.append("a rule has no row in the inventory (%s): %s" % (s, l.strip()[:90]))
    return problems, len(rs), unenforced, partly


def main(argv):
    root = argv[1] if len(argv) > 1 else ROOT
    problems, n, unenforced, partly = check(root)
    if problems:
        print("check_retro_rules: FAILED (%d)" % len(problems))
        for p in problems:
            print("  " + p)
        return 1
    print("check_retro_rules: ok (%d rules: %d enforced by a check, %d PARTLY enforced (a check and a register entry), %d unenforced (register entry only))" %
          (n, n - partly - unenforced, partly, unenforced))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
