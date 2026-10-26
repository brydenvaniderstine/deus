#!/usr/bin/env bash
# Runs every tests/ir/*.mn golden against the given ir_dump binary, then a
# crash/sanitizer sweep of every whole-file input in tests/parser/ (not goldens:
# those files were written for parsing, not typing). Used by `make test-ir` and
# `make test-ir-asan` so the comparison logic cannot drift between them
# (run_parser_tests.sh's pattern, plus an exit-status check: an empty golden -
# the empty module - would otherwise pass a binary that crashed silently).
set -u

BIN="${1:?usage: run_ir_tests.sh <path-to-ir_dump-binary>}"
fail=0

for mn in tests/ir/*.mn tests/nocascade/*.mn; do
    expected="${mn%.mn}.ir"
    stderr_file=$(mktemp)
    actual=$("$BIN" "$mn" 2>"$stderr_file")
    rc=$?
    sanitizer_output=$(cat "$stderr_file")
    rm -f "$stderr_file"

    if [ -n "$sanitizer_output" ]; then
        echo "FAIL $mn (stderr: $sanitizer_output)"
        fail=1
    elif [ $rc -ne 0 ]; then
        echo "FAIL $mn (exit status $rc; no golden may record a crash or IR: unimplemented)"
        fail=1
    elif [ "$actual" = "$(cat "$expected")" ]; then
        echo "PASS $mn"
    else
        echo "FAIL $mn"
        diff <(echo "$actual") "$expected"
        fail=1
    fi
done

# Sweep: status 0 (dump or ERRORS) or 3 (IR: unimplemented) only; no sanitizer report.
swept=0
for mn in tests/parser/*.mn; do
    stderr_file=$(mktemp)
    "$BIN" "$mn" >/dev/null 2>"$stderr_file"
    rc=$?
    if grep -q -e "Sanitizer" -e "runtime error" "$stderr_file" || { [ $rc -ne 0 ] && [ $rc -ne 3 ]; }; then
        echo "FAIL sweep $mn (exit status $rc)"
        cat "$stderr_file"
        fail=1
    fi
    rm -f "$stderr_file"
    swept=$((swept + 1))
done
echo "swept $swept tests/parser inputs"

exit $fail
