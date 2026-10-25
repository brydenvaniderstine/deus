#!/usr/bin/env bash
# Test of tools/fuzz/run_fuzzers.sh's exit status (step 20e): a clean run exits 0, and a run that leaves a crash artifact exits 1 with the artifact
# listed ("FAILED"). Uses the lexer target for 3 seconds on a scratch artifact directory. Needs a clang with libFuzzer (LLVM_CLANG); without one it
# FAILS, it is not skipped. make test-fuzz-script
set -u
cd "$(dirname "$0")/../.."
LLVM_CLANG="${LLVM_CLANG:-/usr/local/opt/llvm/bin/clang}"
export LLVM_CLANG
[ -x "$LLVM_CLANG" ] || { echo "FAIL: no clang with libFuzzer at $LLVM_CLANG (set LLVM_CLANG)"; exit 1; }
ART=build/fuzz/artifacts
mkdir -p "$ART"
saved=$(mktemp -d)
mv "$ART"/* "$saved"/ 2>/dev/null
fail=0
DURATION_LEXER=3 tools/fuzz/run_fuzzers.sh lexer > build/fuzz_test.out 2>&1
rc=$?
if [ $rc -eq 0 ] && grep -q "No crash/timeout artifacts" build/fuzz_test.out; then echo "PASS: a clean run exits 0"; else echo "FAIL: a clean run gave rc=$rc"; tail -5 build/fuzz_test.out; fail=1; fi
echo planted > "$ART/crash-planted"
DURATION_LEXER=3 tools/fuzz/run_fuzzers.sh lexer > build/fuzz_test.out 2>&1
rc=$?
if [ $rc -eq 1 ] && grep -q "FAILED: crash/timeout/sanitizer artifacts" build/fuzz_test.out && grep -q "crash-planted" build/fuzz_test.out; then echo "PASS: a crash artifact makes the run exit 1 and names the artifact"; else echo "FAIL: a planted artifact gave rc=$rc"; tail -5 build/fuzz_test.out; fail=1; fi
rm -f "$ART/crash-planted"
# a run is clean only if the fuzzer exited 0 AND printed "Done N runs" (Linux review of batch B, finding 3)
cat > build/fuzz_fake_silent.sh <<'FAKE'
#!/bin/sh
exit 0
FAKE
cat > build/fuzz_fake_dies.sh <<'FAKE'
#!/bin/sh
echo "binary could not start"
exit 5
FAKE
cat > build/fuzz_fake_done.sh <<'FAKE'
#!/bin/sh
echo "Done 42 runs in 3 second(s)"
exit 0
FAKE
chmod +x build/fuzz_fake_*.sh
FUZZ_WRAP=build/fuzz_fake_silent.sh DURATION_LEXER=3 tools/fuzz/run_fuzzers.sh lexer > build/fuzz_test.out 2>&1
rc=$?
if [ $rc -eq 1 ] && grep -q "printed no 'Done N runs' line" build/fuzz_test.out; then echo "PASS: a fuzzer that exits 0 without 'Done N runs' fails the run"; else echo "FAIL: a silent fuzzer gave rc=$rc"; tail -5 build/fuzz_test.out; fail=1; fi
FUZZ_WRAP=build/fuzz_fake_dies.sh DURATION_LEXER=3 tools/fuzz/run_fuzzers.sh lexer > build/fuzz_test.out 2>&1
rc=$?
if [ $rc -eq 1 ] && grep -q "the fuzzer exited with status 5" build/fuzz_test.out; then echo "PASS: a fuzzer that dies without an artifact fails the run, with its status"; else echo "FAIL: a dying fuzzer gave rc=$rc"; tail -5 build/fuzz_test.out; fail=1; fi
FUZZ_WRAP=build/fuzz_fake_done.sh DURATION_LEXER=3 tools/fuzz/run_fuzzers.sh lexer > build/fuzz_test.out 2>&1
rc=$?
if [ $rc -eq 0 ]; then echo "PASS: exit 0 plus 'Done N runs' is clean"; else echo "FAIL: a clean fake gave rc=$rc"; tail -5 build/fuzz_test.out; fail=1; fi
rm -f build/fuzz_fake_*.sh
mv "$saved"/* "$ART"/ 2>/dev/null
rmdir "$saved" 2>/dev/null
exit $fail
