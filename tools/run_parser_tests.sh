#!/usr/bin/env bash
# Runs every tests/parser/*.mn golden test against the given parser_dump
# binary. Used by both `make test-parser` and `make test-parser-asan` so
# the comparison logic can't drift between the two (tools/run_lexer_
# goldens.sh's pattern, reused here).
set -u

BIN="${1:?usage: run_parser_tests.sh <path-to-parser_dump-binary>}"
fail=0

for mn in tests/parser/*.mn; do
    expected="${mn%.mn}.ast"
    stderr_file=$(mktemp)
    actual=$("$BIN" "$mn" 2>"$stderr_file")
    sanitizer_output=$(cat "$stderr_file")
    rm -f "$stderr_file"

    if [ -n "$sanitizer_output" ]; then
        echo "FAIL $mn (sanitizer report)"
        echo "$sanitizer_output"
        fail=1
        continue
    fi

    if [ "$actual" = "$(cat "$expected")" ]; then
        echo "PASS $mn"
    else
        echo "FAIL $mn"
        diff <(echo "$actual") "$expected"
        fail=1
    fi
done

exit $fail
