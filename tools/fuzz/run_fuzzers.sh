#!/usr/bin/env bash
set -euo pipefail

# Step 12: libFuzzer harness runner (docs/M2_PLAN.md), for a fixed wall-clock budget per target. Step 20e: CI runs it too (60 s per target on
# every push in the Linux job; 30 min per target nightly, .github/workflows/fuzz.yml). Any crash, timeout, out-of-memory or sanitizer report
# leaves an artifact under build/fuzz/artifacts and makes the script EXIT 1 (it used to print them and exit 0).
#
# Usage: tools/fuzz/run_fuzzers.sh [lexer|parser|ir|ir-ungated|both]   (default: both = lexer + parser)
#   ir / ir-ungated: the typed-IR target (typed-IR step 16), gated as used, and with the parse-diagnostics gate
#   compiled out (-DMANNA_FUZZ_IR_UNGATED); both grow one corpus, tools/fuzz/corpus/ir, seeded from tests/parser and tests/ir.
#
# Apple's Xcode clang does not ship the libFuzzer runtime at all
# (`-fsanitize=fuzzer` fails to link: "file cannot be open()ed ...
# libclang_rt.fuzzer_osx.a"). Uses brew's keg-only llvm instead, which
# does ship it - override LLVM_CLANG if yours lives somewhere else.
#
# Each fuzzer's PRIMARY corpus is the committed, pre-minimized one under
# tools/fuzz/corpus/{lexer,parser} - libFuzzer treats the first corpus
# directory given as both input and output, so a run adds any newly
# discovered interesting input directly there, growing it in place. The
# golden-test directories (tests/lexer, tests/parser) are passed as a
# second, read-only seed corpus on every run, so new goldens (including
# every err_recovery_*.mn) always get re-offered even if a past run
# predates them. Periodically re-minimize and commit
# tools/fuzz/corpus/{lexer,parser} (see the -merge=1 recipe in this
# script's own commit message / docs/M2_PLAN.md) rather than letting it
# grow unbounded. The parser binary is additionally built with
# -DMANNA_FUZZ_TRACE_LAST_RESORT (see parser.c), which turns reaching
# the top-level loop's last-resort single-token-skip backstop into an
# immediate, saved libFuzzer crash - compiled out of every other build,
# so this has zero effect anywhere else.
#
# Every crash/timeout artifact found becomes a golden test (minimized
# first, then copied into tests/lexer or tests/parser with a name
# describing what broke) - never left as just a file under build/fuzz.

TARGET="${1:-both}"
# a UBSan report must stop the run (recoverable by default, so libFuzzer would see no crash and save no artifact)
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
LLVM_CLANG="${LLVM_CLANG:-/usr/local/opt/llvm/bin/clang}"
DURATION_LEXER="${DURATION_LEXER:-180}"   # seconds
DURATION_PARSER="${DURATION_PARSER:-600}" # seconds
DURATION_IR="${DURATION_IR:-600}"         # seconds, per IR mode
BUILD_DIR="build/fuzz"
CORPUS_LEXER="tools/fuzz/corpus/lexer"
CORPUS_PARSER="tools/fuzz/corpus/parser"
CORPUS_IR="tools/fuzz/corpus/ir"
ARTIFACTS="$BUILD_DIR/artifacts"

if [ ! -x "$LLVM_CLANG" ]; then
    echo "error: $LLVM_CLANG not found or not executable." >&2
    echo "Apple's Xcode clang lacks the libFuzzer runtime; install brew's" >&2
    echo "llvm (brew install llvm) or set LLVM_CLANG to a clang that has it." >&2
    exit 1
fi

mkdir -p "$ARTIFACTS" "$CORPUS_LEXER" "$CORPUS_PARSER" "$CORPUS_IR"

FAILURES=()
# Runs one fuzzer binary ($1 = a name for the log, the rest = the command). A run counts as CLEAN only if the fuzzer exited 0 AND printed libFuzzer's
# "Done N runs" line; a binary that fails to start, a bad flag, a kill by the runner all fail with the reason (Linux review of batch B, finding 3).
# $FUZZ_WRAP (tests only) is a command put in front of the fuzzer.
run_fuzzer() {
    local name="$1"; shift
    local log="$BUILD_DIR/$name.log" rc
    set +e
    ${FUZZ_WRAP:-} "$@" 2>&1 | tee "$log"
    rc=${PIPESTATUS[0]}
    set -e
    if [ "$rc" -ne 0 ]; then
        FAILURES+=("$name: the fuzzer exited with status $rc")
    elif ! grep -Eq '^Done [0-9]+ runs' "$log"; then
        FAILURES+=("$name: the fuzzer exited 0 but printed no 'Done N runs' line (it did not run)")
    fi
}

run_lexer() {
    echo "==> Building fuzz_lexer ($LLVM_CLANG)"
    "$LLVM_CLANG" -std=c11 -g -O1 -fsanitize=fuzzer,address,undefined \
        -o "$BUILD_DIR/fuzz_lexer" tools/mannac/lexer.c tools/mannac/fuzz_lexer.c
    echo "==> Fuzzing lexer for ${DURATION_LEXER}s"
    run_fuzzer lexer "$BUILD_DIR/fuzz_lexer" -max_total_time="$DURATION_LEXER" \
        -artifact_prefix="$ARTIFACTS/lexer_" "$CORPUS_LEXER" tests/lexer
}

run_parser() {
    echo "==> Building fuzz_parser (MANNA_FUZZ_TRACE_LAST_RESORT on)"
    "$LLVM_CLANG" -std=c11 -g -O1 -fsanitize=fuzzer,address,undefined \
        -DMANNA_FUZZ_TRACE_LAST_RESORT \
        -o "$BUILD_DIR/fuzz_parser" \
        tools/mannac/arena.c tools/mannac/ast.c tools/mannac/diag.c \
        tools/mannac/parser_state.c tools/mannac/parser.c tools/mannac/lexer.c \
        tools/mannac/fuzz_parser.c
    echo "==> Fuzzing parser for ${DURATION_PARSER}s"
    run_fuzzer parser "$BUILD_DIR/fuzz_parser" -max_total_time="$DURATION_PARSER" \
        -artifact_prefix="$ARTIFACTS/parser_" "$CORPUS_PARSER" tests/parser
}

run_ir() { # $1 = gated | ungated
    local flags=""
    [ "$1" = ungated ] && flags="-DMANNA_FUZZ_IR_UNGATED"
    echo "==> Building fuzz_ir_$1 ($LLVM_CLANG)"
    "$LLVM_CLANG" -std=c11 -g -O1 -fsanitize=fuzzer,address,undefined $flags \
        -o "$BUILD_DIR/fuzz_ir_$1" \
        tools/mannac/arena.c tools/mannac/ast.c tools/mannac/diag.c \
        tools/mannac/parser_state.c tools/mannac/parser.c tools/mannac/lexer.c \
        tools/mannac/ir.c tools/mannac/ir_dump.c tools/mannac/ir_build.c tools/mannac/ir_decls.c \
        tools/mannac/ir_expr.c tools/mannac/ir_body.c tools/mannac/ir_calls.c tools/mannac/ir_names.c \
        tools/mannac/ir_rename.c tools/mannac/ir_selfcheck.c tools/mannac/fuzz_ir.c
    echo "==> Fuzzing the IR ($1) for ${DURATION_IR}s"
    run_fuzzer "fuzz_ir_$1" "$BUILD_DIR/fuzz_ir_$1" -max_total_time="$DURATION_IR" \
        -artifact_prefix="$ARTIFACTS/ir_$1_" "$CORPUS_IR" tests/parser tests/ir
}

case "$TARGET" in
    lexer)  run_lexer ;;
    parser) run_parser ;;
    ir)     run_ir gated ;;
    ir-ungated) run_ir ungated ;;
    both)   run_lexer; run_parser ;;
    *) echo "usage: $0 [lexer|parser|ir|ir-ungated|both]" >&2; exit 1 ;;
esac

echo "==> Done."
status=0
if [ ${#FAILURES[@]} -ne 0 ]; then
    echo "FAILED: a fuzz run did not complete cleanly:"
    printf '  %s\n' "${FAILURES[@]}"
    status=1
fi
if ls "$ARTIFACTS"/* >/dev/null 2>&1; then
    echo "FAILED: crash/timeout/sanitizer artifacts found under $ARTIFACTS:"
    ls -la "$ARTIFACTS"
    status=1
fi
[ $status -eq 0 ] && echo "No crash/timeout artifacts."
exit $status
