#!/usr/bin/env python3
"""Tests of tools/check_register.py (step 20b) on a small scratch repository: a good register passes; a drifted quote, a quote that is not
there, a historical revision, a missing field, a bad status, an unknown or passed trigger, --at, the 7a/7b tables, a bad waiver and a missing
pointer each fail (or are repaired by --fix-lines). Run from the repo root: python3 tools/check_register_test.py (make test-register-script)"""
import os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_register as cr

fails = []


def check(name, cond):
    print(("PASS: " if cond else "FAIL: ") + name)
    if not cond:
        fails.append(name)


ENTRY = """### X1 - an entry

- **Status:** OPEN
- **Reason:** because
- **Promise:** `a.txt:2` "second line"
- **Where it stands:** `a.txt@REV:1` "first line"
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** step 21
- **Trigger-id:** `step-21`
"""
HEAD = """# Degradation register

## Trigger ids

**Current point:** `step-20`

- `step-20` - this step
- `step-21` - next
- `m2-ir-v0` - the tag
- `each-tag` - every tag
- `post-m2-ir-v0` - after
- `v1` - v1

## 1. Section

"""
TAIL = """
## 7. Fix-it exceptions

### 7a. Approved exceptions: the six safety limits

| Code | Location | Quote |
|---|---|---|
| E0104 | `docs/error_codes.md:3` | "None - documented exception: a" |

### 7b. Fix-it exceptions to close (1 codes)

| Code | Location | The exception, quoted |
|---|---|---|
| E0101 | `docs/error_codes.md:4` | "None - documented exception: b" |
"""


STATUS_OK = "see docs/DEGRADATION.md\n\n**Current point:** `step-20`\n"


def make(entry=ENTRY, head=HEAD, tail=TAIL, errcodes=None, claude="see docs/DEGRADATION.md\n", status=STATUS_OK, extra=""):
    root = tempfile.mkdtemp()
    os.makedirs(os.path.join(root, "docs"))
    open(os.path.join(root, "a.txt"), "w").write("first line\nsecond line\nthird line\n")
    open(os.path.join(root, "CLAUDE.md"), "w").write(claude)
    open(os.path.join(root, "docs", "STATUS.md"), "w").write(status)
    open(os.path.join(root, "docs", "error_codes.md"), "w").write(errcodes or
        "| Code | Message | Meaning | Fix |\n|---|---|---|---|\n| `E0104` | m | x | None - documented exception: a |\n| `E0101` | m | x | None - documented exception: b |\n| `E0103` | m | x | Rename |\n")
    git = lambda *a: subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", "-C", root] + list(a), capture_output=True, text=True)
    git("init", "-q")
    git("add", "-A")
    git("commit", "-qm", "one")
    rev = git("rev-parse", "--short", "HEAD").stdout.strip()
    open(os.path.join(root, "docs", "DEGRADATION.md"), "w").write(head + entry.replace("REV", rev) + extra + tail)
    return root, rev


def run(root, **kw):
    return cr.run(root, **kw)


root, rev = make()
check("a good register passes", run(root) == [])
check("--at before the trigger passes", run(root, at="step-20") == [])
check("--at the trigger fails (the step is closing with the entry open)", any("closing" in p for p in run(root, at="step-21")))
check("--at a later id fails too", any("closing" in p for p in run(root, at="m2-ir-v0")))
shutil.rmtree(root)

root, rev = make()
open(os.path.join(root, "a.txt"), "w").write("new top line\nfirst line\nsecond line\nthird line\n")   # the cited lines moved down by one
p = run(root)
check("a drifted line fails and says where the quote is", any("on line 3, not line 2" in x for x in p))
p = run(root, fix=True)
check("--fix-lines repairs the live citation (the historical one is left alone)", p == [] and "`a.txt:3`" in open(os.path.join(root, "docs", "DEGRADATION.md")).read()
      and run(root) == [])
shutil.rmtree(root)

root, rev = make(entry=ENTRY.replace('"second line"', '"not in the file"'))
check("a quote that is nowhere fails", any("not in the file" in x for x in run(root)))
check("--fix-lines does not invent a line for it", any("not in the file" in x for x in run(root, fix=True)))
shutil.rmtree(root)

root, rev = make(entry=ENTRY.replace("@REV:1", "@0000000:1"))
check("an unknown revision fails", any("no such file at that revision" in x for x in run(root)))
shutil.rmtree(root)

root, rev = make(entry=ENTRY.replace("- **Owner:** Claude Code (work); Bryden (approval)\n", ""))
check("a missing field fails", any("missing field Owner" in x for x in run(root)))
shutil.rmtree(root)

root, rev = make(entry=ENTRY.replace("OPEN", "ACCEPTED"))
check("a status outside the list fails", any("Status 'ACCEPTED'" in x for x in run(root)))
shutil.rmtree(root)

root, rev = make(entry=ENTRY.replace("`step-21`", "`someday`"))
check("an unknown trigger id fails", any("'someday' is not in the list" in x for x in run(root)))
shutil.rmtree(root)

root, rev = make(head=HEAD.replace("**Current point:** `step-20`", "**Current point:** `m2-ir-v0`"))
check("an OPEN entry whose trigger has passed fails", any("has passed" in x for x in run(root)))
shutil.rmtree(root)

root, rev = make(entry=ENTRY.replace("OPEN", "CLOSED (2026-10-24, step 21)").replace("`step-21`", "`step-20`"))
check("a CLOSED entry past its trigger is fine", run(root, at="m2-ir-v0") == [])
shutil.rmtree(root)

root, rev = make(entry=ENTRY.replace("`step-21`", "`each-tag`") + "- **Checked at:** \n")
check("each-tag is due at the first tag (it needs a Checked at line naming it)", any("Checked at" in x for x in run(root, at="m2-ir-v0")))
shutil.rmtree(root)

# finding 8 (Linux review of batch A2): an each-tag entry is never "passed" by position; AT=<tag> needs "Checked at: <tag>"
EACH = ENTRY.replace("`step-21`", "`each-tag`") + "- **Checked at:** \n"
root, rev = make(entry=EACH, head=HEAD.replace("**Current point:** `step-20`", "**Current point:** `post-m2-ir-v0`"), status="see docs/DEGRADATION.md\n\n**Current point:** `post-m2-ir-v0`\n")
check("finding 8: with the Current point past the tag an OPEN each-tag entry does not fail", run(root) == [])
check("finding 8: AT=m2-ir-v0 without the tag in Checked at fails", any("Checked at" in x and "m2-ir-v0" in x for x in run(root, at="m2-ir-v0")))
shutil.rmtree(root)
root, rev = make(entry=EACH.replace("Checked at:** ", "Checked at:** m2-ir-v0, 2026-11-01"), head=HEAD.replace("**Current point:** `step-20`", "**Current point:** `post-m2-ir-v0`"), status="see docs/DEGRADATION.md\n\n**Current point:** `post-m2-ir-v0`\n")
check("finding 8: AT=m2-ir-v0 with the tag in Checked at passes", run(root, at="m2-ir-v0") == [])
check("finding 8: naming another tag does not satisfy it", any("Checked at" in x and "v1" in x for x in run(root, at="v1")))
shutil.rmtree(root)
root, rev = make(entry=ENTRY.replace("`step-21`", "`each-tag`"))
check("finding 8: an each-tag entry must carry the Checked at field (even empty)", any("Checked at" in x for x in run(root)))
shutil.rmtree(root)

root, rev = make(errcodes="| Code | Message | Meaning | Fix |\n|---|---|---|---|\n| `E0104` | m | x | None - documented exception: a |\n| `E0101` | m | x | None - documented exception: b |\n| `E0105` | m | x | None - documented exception: c |\n")
check("a 'None' code missing from 7a/7b fails", any("E0105" in x for x in run(root)))
shutil.rmtree(root)

root, rev = make(extra="\nRATCHET-WAIVER ir_goldens 10 -> 9 Q7\n")
check("a waiver naming an unknown entry fails", any("unknown entry Q7" in x for x in run(root)))
shutil.rmtree(root)
root, rev = make(extra="\nRATCHET-WAIVER ir_goldens 10 -> 9 X1\n")
check("a waiver naming a real entry passes", run(root) == [])
shutil.rmtree(root)

root, rev = make(claude="nothing\n")
check("CLAUDE.md without the pointer fails", any("CLAUDE.md does not point" in x for x in run(root)))
shutil.rmtree(root)

# finding 5: STATUS.md carries the Current point, and it must equal the register's
root, rev = make(status="see docs/DEGRADATION.md\n")
check("finding 5: STATUS.md without a Current point line fails", any("STATUS.md" in x and "Current point" in x for x in run(root)))
shutil.rmtree(root)
root, rev = make(status="see docs/DEGRADATION.md\n\n**Current point:** `step-21`\n")
check("finding 5: STATUS.md and the register disagreeing about the Current point fails", any("step-21" in x and "step-20" in x for x in run(root)))
shutil.rmtree(root)
root, rev = make()
subprocess.run(["git", "-C", root, "tag", "m2-ir-v0"], capture_output=True)
check("finding 5: a git tag for a trigger id later than the Current point fails", any("tag m2-ir-v0 exists" in x for x in run(root)))
shutil.rmtree(root)
root, rev = make()
subprocess.run(["git", "-C", root, "tag", "step-20"], capture_output=True)
check("finding 5: a tag at the Current point is fine", run(root) == [])
shutil.rmtree(root)

# finding 6: short quotes and waivers
root, rev = make(entry=ENTRY.replace('"second line"', '"se"'))
check("finding 6: a quote shorter than 8 characters fails", any("too short" in x for x in run(root)))
shutil.rmtree(root)
root, rev = make(entry=ENTRY.replace('"second line"', '""'))
check("finding 6: an empty quote fails", any("too short" in x or "without a quote" in x for x in run(root)))
shutil.rmtree(root)
root, rev = make(entry=ENTRY.replace("OPEN", "CLOSED (x)").replace("`step-21`", "`step-20`"), extra="\nRATCHET-WAIVER ir_goldens 10 -> 9 X1\n")
check("finding 6: a waiver naming a CLOSED entry fails", any("not OPEN or PARTLY CLOSED" in x for x in run(root)))
shutil.rmtree(root)

# the real register must pass
check("the real register passes", cr.run(os.path.dirname(HERE)) == [])

print("register tests: %d failed" % len(fails) if fails else "register tests: all passed")
sys.exit(1 if fails else 0)
