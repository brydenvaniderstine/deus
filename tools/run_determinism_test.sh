#!/usr/bin/env bash
# Step 11: determinism (docs/M2_PLAN.md S5, MANNA_PRINCIPLES.md principle
# 12). Same input must produce byte-identical output across two separate
# invocations of the same binary, AND across plain vs ASan builds - the
# precondition diverse double-compiling (docs/M2_PLAN.md S1) depends on:
# if the C-bootstrap and self-hosted compilers' outputs are ever going to
# be meaningfully diffed against each other, each one individually
# producing byte-identical output across repeated runs has to hold first.
#
# This does NOT replace run_lexer_goldens.sh/run_parser_tests.sh (those
# check output against a fixed expected golden); this checks for run-to-
# run and build-to-build DRIFT directly, independent of whether the
# output happens to be "correct" - covers every existing golden input,
# both the lexer and parser stages (the only two implemented so far).
set -u

LEXER_PLAIN="${1:?usage: run_determinism_test.sh <lexer-plain> <lexer-asan> <parser-plain> <parser-asan>}"
LEXER_ASAN="${2:?usage: run_determinism_test.sh <lexer-plain> <lexer-asan> <parser-plain> <parser-asan>}"
PARSER_PLAIN="${3:?usage: run_determinism_test.sh <lexer-plain> <lexer-asan> <parser-plain> <parser-asan>}"
PARSER_ASAN="${4:?usage: run_determinism_test.sh <lexer-plain> <lexer-asan> <parser-plain> <parser-asan>}"
# Optional (typed-IR step 3): the ir_dump binaries, plain and ASan. The IR goldens get the
# same treatment as the other stages: twice plain, once ASan, all byte-compared.
IR_PLAIN="${5:-}"
IR_ASAN="${6:-}"

fail=0

check_one() {
    local plain_bin="$1" asan_bin="$2" input="$3"
    local run1 run2 run_asan

    run1=$("$plain_bin" "$input" 2>&1)
    run2=$("$plain_bin" "$input" 2>&1)
    if [ "$run1" != "$run2" ]; then
        echo "FAIL (non-deterministic across two plain runs) $input"
        diff <(echo "$run1") <(echo "$run2")
        fail=1
        return
    fi

    run_asan=$("$asan_bin" "$input" 2>&1)
    if [ "$run1" != "$run_asan" ]; then
        echo "FAIL (plain vs ASan output differs) $input"
        diff <(echo "$run1") <(echo "$run_asan")
        fail=1
        return
    fi

    echo "PASS $input"
}

check_one_fixed() {
    local plain_bin="$1" asan_bin="$2" input="$3"
    local run1 run2 run_asan
    run1=$("$plain_bin" --fixed "$input" 2>&1)
    run2=$("$plain_bin" --fixed "$input" 2>&1)
    run_asan=$("$asan_bin" --fixed "$input" 2>&1)
    if [ "$run1" != "$run2" ]; then
        echo "FAIL (--fixed: non-deterministic across two plain runs) $input"
        fail=1
        return
    fi
    if [ "$run1" != "$run_asan" ]; then
        echo "FAIL (--fixed: plain vs ASan output differs) $input"
        fail=1
        return
    fi
    echo "PASS $input (--fixed)"
}

for f in tests/lexer/*.mn; do
    check_one "$LEXER_PLAIN" "$LEXER_ASAN" "$f"
done

for f in tests/parser/*.mn; do
    check_one "$PARSER_PLAIN" "$PARSER_ASAN" "$f"
done

if [ -n "$IR_PLAIN" ] && [ -n "$IR_ASAN" ]; then
    for f in tests/ir/*.mn tests/nocascade/*.mn; do
        check_one "$IR_PLAIN" "$IR_ASAN" "$f"
    done
    # Typed-IR step 14: the sources with every rename fix applied (`ir_dump --fixed`) must be just as stable.
    for f in tests/ir_fix/*.mn; do
        case "$f" in *.fixed.mn|*.fixed2.mn) continue ;; esac
        check_one_fixed "$IR_PLAIN" "$IR_ASAN" "$f"
    done
fi

exit $fail
