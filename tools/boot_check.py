#!/usr/bin/env python3
"""Checks a QEMU serial log against what the kernel promises (step 20h; register K4, K5).

  tools/boot_check.py smoke SERIAL [QEMU_LOG]   the Milestone 1 boot: the ordered regexes of tests/boot_expected.txt, A and B interleaved after the
                                                preemption proof starts, no exception dump, no triple fault in the QEMU log
  tools/boot_check.py wx N SERIAL [QEMU_LOG]    test-wx subtest N (1: write to .text, 2: execute .data, 3: execute .text through the identity alias): the
                                                kernel must have taken exactly the page fault the protection promises, at the address it printed

ANSI escapes and CRs (firmware console noise) are stripped first. Exit 1 with every problem on any mismatch; a missing or empty log is a failure.
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
EXPECTED = os.path.join(os.path.dirname(HERE), "tests", "boot_expected.txt")
ANSI = re.compile(r"\x1b\[[0-9;?=]*[A-Za-z]")


def clean(text):
    return ANSI.sub("", text).replace("\r", "")


def read(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError as e:
        return None


def triple_fault(qemu_log):
    return qemu_log is not None and "Triple fault" in qemu_log


def smoke(serial, qemu_log, expected_path=EXPECTED):
    problems = []
    text = clean(serial)
    pos = 0
    for line in open(expected_path, encoding="utf-8").read().split("\n"):
        if not line.strip() or line.startswith("#"):
            continue
        m = re.compile(line).search(text, pos)
        if not m:
            problems.append("missing (in this order, after %d bytes): /%s/" % (pos, line))
            break    # later lines would only report the same break
        pos = m.end()
    marker = "starting A/B preemption proof"
    i = text.find(marker)
    if i >= 0:
        ab = re.sub(r"[^AB]", "", text[i + len(marker):])
        transitions = sum(1 for a, b in zip(ab, ab[1:]) if a != b)
        if len(ab) < 40 or ab.count("A") < 10 or ab.count("B") < 10 or transitions < 10:
            problems.append("no preemption proof: after the marker the A/B output is %d chars (%d A, %d B, %d switches); need >= 40, 10 and 10, 10" %
                            (len(ab), ab.count("A"), ab.count("B"), transitions))
    for bad in ("[exception]", "FATAL", "panic"):
        if bad in text:
            problems.append("the log contains %r" % bad)
    if triple_fault(qemu_log):
        problems.append("QEMU reports a triple fault")
    return problems


WX = {
    1: (r"\[test-wx\] writing to \.text at (0x[0-9a-f]+)", "0x3", False),
    2: (r"\[test-wx\] executing \.data at (0x[0-9a-f]+)", "0x11", True),
    3: (r"\[test-wx\] executing \.text via identity alias at (0x[0-9a-f]+)", "0x11", True),
}


def wx(n, serial, qemu_log):
    problems = []
    text = clean(serial)
    pat, err, rip_is_target = WX[n]
    for need in (r"\[test-wx\] WX_TEST=%d\b" % n, r"\[paging\] CR0\.WP enabled", r"\[test-wx\] protections live; attempting the deliberate violation"):
        if not re.search(need, text):
            problems.append("missing /%s/" % need)
    m = re.search(pat, text)
    if not m:
        problems.append("missing /%s/" % pat)
        return problems
    target = m.group(1)
    e = re.search(r"\[exception\] vector=(\d+) error_code=(0x[0-9a-f]+) rip=(0x[0-9a-f]+) .* cr2=(0x[0-9a-f]+)", text[m.end():])
    if not e:
        problems.append("no [exception] line after the violation: the protection did not fault (or the kernel hung)")
    else:
        vector, code, rip, cr2 = e.groups()
        if vector != "14":
            problems.append("vector %s, want 14 (page fault)" % vector)
        if code != err:
            problems.append("error_code %s, want %s" % (code, err))
        if cr2 != target:
            problems.append("cr2 %s, want the address the test printed, %s" % (cr2, target))
        if rip_is_target and rip != target:
            problems.append("rip %s, want %s (the jump into the page)" % (rip, target))
        if "[exception] halting" not in text[m.end():]:
            problems.append("the exception handler did not halt")
    if "FAIL: no fault occurred" in text:
        problems.append("the kernel reports that no fault occurred")
    if triple_fault(qemu_log):
        problems.append("QEMU reports a triple fault")
    return problems


def main(argv):
    if len(argv) >= 3 and argv[1] == "smoke":
        serial_path, qpath, kind = argv[2], argv[3] if len(argv) > 3 else None, "smoke"
    elif len(argv) >= 4 and argv[1] == "wx" and argv[2] in ("1", "2", "3"):
        serial_path, qpath, kind = argv[3], argv[4] if len(argv) > 4 else None, "wx %s" % argv[2]
    else:
        print(__doc__)
        return 2
    serial = read(serial_path)
    if not serial or not serial.strip():
        print("boot_check %s: FAILED: %s is missing or empty (the kernel printed nothing)" % (kind, serial_path))
        return 1
    qemu_log = read(qpath) if qpath else None
    if qpath and qemu_log is None:
        print("boot_check %s: FAILED: %s is unreadable" % (kind, qpath))
        return 1
    problems = smoke(serial, qemu_log) if argv[1] == "smoke" else wx(int(argv[2]), serial, qemu_log)
    if problems:
        print("boot_check %s: FAILED" % kind)
        for p in problems:
            print("  " + p)
        return 1
    print("boot_check %s: ok" % kind)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
