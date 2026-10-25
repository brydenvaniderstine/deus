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
mv "$saved"/* "$ART"/ 2>/dev/null
rmdir "$saved" 2>/dev/null
exit $fail
