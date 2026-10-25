#!/usr/bin/env bash
# Tests of tools/check_kernel_loc.sh (step 20g). make test-kernel-loc-script
set -u
cd "$(dirname "$0")/.."
T=$(mktemp -d); fail=0
mkdir -p "$T/k/sub"
seq 1 60 > "$T/k/a.c"; seq 1 30 > "$T/k/sub/b.S"; seq 1 10 > "$T/k/sub/c.h"; seq 1 500 > "$T/k/notes.txt"   # 100 counted lines; the .txt is not code
out=$(tools/check_kernel_loc.sh "$T/k" 100); rc=$?
if [ $rc -eq 0 ] && echo "$out" | grep -q "100 lines in 3 files"; then echo "PASS: exactly at the budget is fine; only .c .S .h count"; else echo "FAIL: at the budget: rc=$rc $out"; fail=1; fi
out=$(tools/check_kernel_loc.sh "$T/k" 99); rc=$?
if [ $rc -eq 1 ] && echo "$out" | grep -q "FAILED: 100 lines"; then echo "PASS: one line over the budget fails"; else echo "FAIL: over the budget: rc=$rc $out"; fail=1; fi
out=$(tools/check_kernel_loc.sh "$T/nope" 100); rc=$?
if [ $rc -eq 2 ]; then echo "PASS: a missing directory is an error, not a pass"; else echo "FAIL: missing dir rc=$rc $out"; fail=1; fi
out=$(tools/check_kernel_loc.sh); rc=$?
if [ $rc -eq 0 ] && echo "$out" | grep -q "budget 120000"; then echo "PASS: the real kernel/ is within the 120000-line budget"; else echo "FAIL: real kernel: rc=$rc $out"; fail=1; fi
rm -rf "$T"
exit $fail
