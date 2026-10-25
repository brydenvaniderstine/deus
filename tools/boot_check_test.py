#!/usr/bin/env python3
"""Tests of tools/boot_check.py (step 20h) on the real serial logs recorded on 2026-10-25 (tests/boot/*.log) and on damaged copies of them: every promise
the checker makes has a mutation that must fail. Needs no QEMU. Run from the repo root: python3 tools/boot_check_test.py (make test-boot-check-script)"""
import os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import boot_check as bc

ROOT = os.path.dirname(HERE)
fails = []


def check(name, cond):
    print(("PASS: " if cond else "FAIL: ") + name)
    if not cond:
        fails.append(name)


def log(name):
    return open(os.path.join(ROOT, "tests", "boot", name), encoding="utf-8", errors="replace").read()


SMOKE = log("smoke_serial.serial")
check("the real smoke log passes", bc.smoke(SMOKE, None) == [])
check("a QEMU log without a triple fault passes", bc.smoke(SMOKE, "some -d int output\n") == [])
check("a triple fault in the QEMU log fails", any("triple fault" in p for p in bc.smoke(SMOKE, "Triple fault\n")))
check("a missing line fails and names it", any("CR3 loaded" in p for p in bc.smoke(SMOKE.replace("kernel survived the switch", "kernel did something"), None)))
check("the 8 GiB machine is pinned: a smaller identity map fails", any("0x280000000" in p for p in bc.smoke(SMOKE.replace("0..0x280000000", "0..0x180000000"), None)))
check("a missing gdt line fails", bc.smoke(SMOKE.replace("[gdt] loaded", "[gdx] loaded", 1) , None) != [])
check("a different calibration source fails (K2 will change this line on purpose)", any("calibration source" in p for p in bc.smoke(SMOKE.replace("calibration source: pit", "calibration source: hpet"), None)))
check("a different timer mode fails (K1 will change this line on purpose)", bc.smoke(SMOKE.replace("LAPIC periodic timer", "TSC-deadline timer"), None) != [])
head = SMOKE[:SMOKE.index("starting A/B preemption proof") + len("starting A/B preemption proof")]
check("no A/B output after the marker fails", any("no preemption proof" in p for p in bc.smoke(head + "\n", None)))
check("only A (no preemption) fails", any("no preemption proof" in p for p in bc.smoke(head + "\n" + "A" * 400, None)))
check("A/B that never switch fails", any("no preemption proof" in p for p in bc.smoke(head + "\n" + "A" * 200 + "B" * 200, None)))
check("interleaved A/B passes", bc.smoke(head + "\n" + "AB" * 100, None) == [])
check("an exception dump fails", any("[exception]" in p for p in bc.smoke(SMOKE + "\n[exception] vector=14 error_code=0x0 rip=0x1 cr2=0x2\n", None)))

for n in (1, 2, 3):
    W = log("wx_serial_%d.serial" % n)
    check("the real test-wx %d log passes" % n, bc.wx(n, W, None) == [])
    check("test-wx %d: the wrong error code fails" % n, any("error_code" in p for p in bc.wx(n, W.replace("error_code=0x3 ", "error_code=0x1 ").replace("error_code=0x11 ", "error_code=0x1 "), None)))
    check("test-wx %d: a vector other than 14 fails" % n, any("vector" in p for p in bc.wx(n, W.replace("vector=14", "vector=13"), None)))
    check("test-wx %d: no exception (the protection did not trigger) fails" % n, any("no [exception]" in p for p in bc.wx(n, W[:W.index("[exception]")] + "[test-wx] FAIL: no fault occurred, protection did not trigger\n", None)))
    check("test-wx %d: a fault at some other address fails" % n, any("cr2" in p for p in bc.wx(n, W.replace("cr2=0x", "cr2=0x1", 1), None)))
    check("test-wx %d: a hung handler (no halt) fails" % n, any("did not halt" in p for p in bc.wx(n, W.replace("[exception] halting", ""), None)))
check("test-wx 2: rip must be the jump target", any("rip" in p for p in bc.wx(2, log("wx_serial_2.serial").replace("rip=0x", "rip=0x1", 1), None)))
check("test-wx 1: rip is not constrained (the faulting store is inside the kernel)", bc.wx(1, log("wx_serial_1.serial").replace("rip=0x", "rip=0x1", 1), None) == [])
check("test-wx 3 log is not accepted as test-wx 1", bc.wx(1, log("wx_serial_3.serial"), None) != [])

# the command line: an empty or missing log is a failure, never a pass
t = tempfile.mkdtemp()
open(os.path.join(t, "empty.log"), "w").write("")
r = subprocess.run([sys.executable, os.path.join(HERE, "boot_check.py"), "smoke", os.path.join(t, "empty.log")], capture_output=True, text=True)
check("an empty log fails on the command line", r.returncode == 1 and "missing or empty" in r.stdout)
r = subprocess.run([sys.executable, os.path.join(HERE, "boot_check.py"), "wx", "1", os.path.join(t, "nope.log")], capture_output=True, text=True)
check("a missing log fails on the command line", r.returncode == 1)
r = subprocess.run([sys.executable, os.path.join(HERE, "boot_check.py"), "smoke", os.path.join(ROOT, "tests", "boot", "smoke_serial.serial")], capture_output=True, text=True)
check("the command line passes the real log", r.returncode == 0 and "ok" in r.stdout)

print("boot-check tests: %d failed" % len(fails) if fails else "boot-check tests: all passed")
sys.exit(1 if fails else 0)
