#!/usr/bin/env python3
"""Checks docs/DEGRADATION.md, the register of weakened promises (step 20b). Run from the repo root.

  tools/check_register.py [--at ID] [--fix-lines]

  1. Every citation `file:line` "quote" (or `file@rev:line` for a historical revision) is checked against the repository: the quote must be
     on that line of that file (of that git revision). --fix-lines rewrites a drifted line number when the quote is on exactly one line
     of the file (the quote is the anchor; line numbers drift).
  2. Every entry (### X - title, sections 1-6) has Status, Reason, Promise, Where it stands, Owner, Trigger and Trigger-id; the Status is
     OPEN, PARTLY CLOSED, CLOSED (...) or NOT A DEGRADATION; the Trigger-id is in the ordered list under "## Trigger ids"; the entry id is unique.
  3. No OPEN / PARTLY CLOSED entry's trigger has passed: its id must not come before the register's **Current point**. With --at ID (run
     when a tag or milestone closes) it must also not be at or before ID ("no tag closes while an entry's trigger has passed"). `each-tag`
     counts as due at `m2-ir-v0` and at every later tag.
  4. Sections 7a / 7b: the codes in the two tables are exactly the codes whose Fix column in docs/error_codes.md starts with "None", and every
     quoted exception is on its line.
  5. Every `RATCHET-WAIVER <name> <from> -> <to> <entry>` line names an existing entry.
  6. CLAUDE.md and docs/STATUS.md point to the register.
Exit 1 on any problem; every problem is printed.
"""
import os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REG = "docs/DEGRADATION.md"
FIELDS = ["Status", "Reason", "Promise", "Where it stands", "Owner", "Trigger", "Trigger-id"]
STATUS = re.compile(r"^(OPEN|PARTLY CLOSED|CLOSED( \(.*\))?|NOT A DEGRADATION)$")
CITE = re.compile(r"`(?:([^`\s@]+)\s?@([0-9a-f]{7,40})|([^`\s@]+)):(\d+)`\s+(?:\|\s+)?\"(.*?)\"(?=\.?\s*(?:;|\||$))")
BARE = re.compile(r"`[\w./-]+:\d+`")


def read(path, root=None):
    with open(os.path.join(root or ROOT, path), encoding="utf-8", errors="replace") as f:
        return f.read()


def file_lines(path, rev, root, cache):
    key = (path, rev)
    if key not in cache:
        if rev:
            r = subprocess.run(["git", "-C", root, "show", "%s:%s" % (rev, path)], capture_output=True, text=True)
            cache[key] = r.stdout.split("\n") if r.returncode == 0 else None
        else:
            try:
                cache[key] = read(path, root).split("\n")
            except OSError:
                cache[key] = None
    return cache[key]


def check_cites(text, root, fix):
    """(problems, new text): every citation checked; with fix, a drifted line number is rewritten when the quote is unique in the file"""
    problems, cache, out, pos = [], {}, [], 0
    for lineno, line in enumerate(text.split("\n"), 1):
        new = line
        found = list(CITE.finditer(line))
        covered = [(m.start(), m.end()) for m in found]
        for b in BARE.finditer(line):
            if not any(s <= b.start() < e for s, e in covered) and re.search(r":\d+`", b.group()) and "RATCHET" not in line:
                problems.append("%s:%d: a citation without a quote: %s" % (REG, lineno, b.group()))
        for m in reversed(found):
            path, rev, quote = m.group(1) or m.group(3), m.group(2), m.group(5).replace("\\|", "|")
            n = int(m.group(4))
            ls = file_lines(path, rev, root, cache)
            where = "%s%s:%d" % (path, "@" + rev if rev else "", n)
            if ls is None:
                problems.append("%s:%d: %s: no such file%s" % (REG, lineno, where, " at that revision" if rev else ""))
            elif 1 <= n <= len(ls) and quote in ls[n - 1]:
                continue
            else:
                hits = [i + 1 for i, l in enumerate(ls) if quote in l]
                if fix and len(hits) == 1 and not rev:
                    new = new[:m.start(4)] + str(hits[0]) + new[m.end(4):]
                    continue
                problems.append("%s:%d: %s: the quote %r is %s" % (REG, lineno, where, quote[:60],
                                "not in the file" if not hits else "on line %s, not line %d" % (", ".join(map(str, hits[:4])), n)))
        out.append(new)
    return problems, "\n".join(out)


def parse(text):
    """(entries by id -> {field: value, line}, trigger ids in order, current point)"""
    entries, cur = {}, None
    ids, current = [], None
    for lineno, line in enumerate(text.split("\n"), 1):
        m = re.match(r"- `([a-z0-9-]+)` - ", line)
        if m:
            ids.append(m.group(1))
        m = re.search(r"\*\*Current point:\*\* `([a-z0-9-]+)`", line)
        if m:
            current = m.group(1)
        m = re.match(r"### (\S+) - ", line)
        if m and not m.group(1).startswith("7"):
            if m.group(1) in entries:
                entries[m.group(1)]["dup"] = True
            cur = entries.setdefault(m.group(1), {"line": lineno})
            continue
        if line.startswith("## "):
            cur = None
        if cur is not None:
            f = re.match(r"- \*\*([A-Za-z -]+):\*\* ?(.*)$", line)
            if f and f.group(1) in FIELDS:
                cur[f.group(1)] = f.group(2).strip()
    return entries, ids, current


def check_entries(text, at):
    problems = []
    entries, ids, current = parse(text)
    if not ids:
        problems.append("%s: no '## Trigger ids' list" % REG)
    if current not in ids:
        problems.append("%s: the Current point %r is not a trigger id" % (REG, current))
    order = {t: i for i, t in enumerate(ids)}
    if at and at not in order:
        problems.append("--at %r is not a trigger id" % at)
    for eid, e in sorted(entries.items(), key=lambda kv: kv[1]["line"]):
        where = "%s:%d (%s)" % (REG, e["line"], eid)
        if e.get("dup"):
            problems.append("%s: the entry id is used twice" % where)
        for f in FIELDS:
            if not e.get(f):
                problems.append("%s: missing field %s" % (where, f))
        st = e.get("Status", "")
        if st and not STATUS.match(st):
            problems.append("%s: Status %r is not OPEN, PARTLY CLOSED, CLOSED (...) or NOT A DEGRADATION" % (where, st))
        tid = (e.get("Trigger-id") or "").strip("`")
        if tid and tid not in order:
            problems.append("%s: Trigger-id %r is not in the list" % (where, tid))
            continue
        if tid and (st.startswith("OPEN") or st.startswith("PARTLY")):
            eff = "m2-ir-v0" if tid == "each-tag" and at else tid
            if current in order and order[tid] < order[current]:
                problems.append("%s: OPEN but its trigger %s has passed (the current point is %s)" % (where, tid, current))
            elif at in order and order[eff] <= order[at]:
                problems.append("%s: OPEN but its trigger %s is at or before %s, which is closing" % (where, tid, at))
    return problems


def check_tables(text, root):
    problems = []
    none_codes = set()
    for line in read("docs/error_codes.md", root).split("\n"):
        m = re.match(r"\| `(E\d{4})` \|", line)
        if m:
            cells = line.strip().strip("|").split("|")
            if cells[-1].strip().startswith("None"):
                none_codes.add(m.group(1))
    a = text.index("### 7a."), text.index("### 7b.")
    seven_a, seven_b = text[a[0]:a[1]], text[a[1]:]
    ca = set(re.findall(r"^\| (E\d{4}) \|", seven_a, re.M))
    cb = set(re.findall(r"^\| (E\d{4}) \|", seven_b, re.M))
    if ca & cb:
        problems.append("7a and 7b both list %s" % sorted(ca & cb))
    if ca | cb != none_codes:
        problems.append("7a + 7b list %d codes; docs/error_codes.md has %d with Fix 'None...': missing %s, extra %s" %
                        (len(ca | cb), len(none_codes), sorted(none_codes - (ca | cb)), sorted((ca | cb) - none_codes)))
    m = re.search(r"7b\. Fix-it exceptions to close \((\d+) codes\)", text)
    if m and int(m.group(1)) != len(cb):
        problems.append("the 7b heading says %s codes, the table has %d" % (m.group(1), len(cb)))
    return problems


def check_waivers_and_pointers(text, root):
    problems = []
    ids = set(re.findall(r"^### (\S+) - ", text, re.M)) | {"7a", "7b"}
    for lineno, line in enumerate(text.split("\n"), 1):
        if line.startswith("RATCHET-WAIVER"):
            m = re.fullmatch(r"RATCHET-WAIVER (\S+) (\d+) -> (\d+) (\S+)", line.strip())
            if not m:
                problems.append("%s:%d: a malformed RATCHET-WAIVER line" % (REG, lineno))
            elif m.group(4) not in ids:
                problems.append("%s:%d: RATCHET-WAIVER names the unknown entry %s" % (REG, lineno, m.group(4)))
    for f in ("CLAUDE.md", "docs/STATUS.md"):
        if "docs/DEGRADATION.md" not in read(f, root):
            problems.append("%s does not point to docs/DEGRADATION.md" % f)
    return problems


def run(root, at=None, fix=False):
    text = read(REG, root)
    problems, new = check_cites(text, root, fix)
    if fix and new != text:
        with open(os.path.join(root, REG), "w") as f:
            f.write(new)
        print("check_register: rewrote drifted line numbers in %s" % REG)
        text = new
        problems = check_cites(text, root, False)[0]
    problems += check_entries(text, at) + check_tables(text, root) + check_waivers_and_pointers(text, root)
    return problems


def main(argv):
    at = argv[argv.index("--at") + 1] if "--at" in argv else (os.environ.get("AT") or None)
    problems = run(ROOT, at, "--fix-lines" in argv)
    if problems:
        print("check_register: FAILED (%d)" % len(problems))
        for p in problems:
            print("  " + p)
        return 1
    text = read(REG)
    entries, ids, current = parse(text)
    print("check_register: ok (%d entries, %d citations checked, current point %s%s)" %
          (len(entries), sum(len(CITE.findall(l)) for l in text.split("\n")), current, ", closing " + at if at else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
