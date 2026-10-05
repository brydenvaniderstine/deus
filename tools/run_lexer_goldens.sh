#!/usr/bin/env bash
# Runs every tests/lexer/*.mn golden test against the given mannac binary.
# Used by both `make test-lexer` and `make test-lexer-asan` so the
# comparison logic can't drift between the two.
set -u

BIN="${1:?usage: run_lexer_goldens.sh <path-to-mannac-binary>}"
fail=0

for mn in tests/lexer/*.mn; do
    expected="${mn%.mn}.tokens"
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
