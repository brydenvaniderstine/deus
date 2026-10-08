#!/usr/bin/env bash
set -euo pipefail

# Step 12: libFuzzer harness runner (docs/M2_PLAN.md). Not run in CI -
# invoke locally, for a fixed wall-clock budget per target.
#
# Usage: tools/fuzz/run_fuzzers.sh [lexer|parser|both]   (default: both)
#
# Apple's Xcode clang does not ship the libFuzzer runtime at all
# (`-fsanitize=fuzzer` fails to link: "file cannot be open()ed ...
# libclang_rt.fuzzer_osx.a"). Uses brew's keg-only llvm instead, which
# does ship it - override LLVM_CLANG if yours lives somewhere else.
#
# Seeds each corpus directly from the existing golden-test inputs
# (tests/lexer, tests/parser) rather than a separate committed corpus -
# this already includes every err_recovery_*.mn file, so the parser
# fuzzer starts mutating from known-interesting recovery inputs instead
# of from nothing. The parser binary is additionally built with
# -DMANNA_FUZZ_TRACE_LAST_RESORT (see parser.c), which turns reaching
# the top-level loop's last-resort single-token-skip backstop into an
# immediate, saved libFuzzer crash - compiled out of every other build,
# so this has zero effect anywhere else.
#
# Every crash/timeout artifact found becomes a golden test (minimized
# first, then copied into tests/lexer or tests/parser with a name
# describing what broke) - never left as just a file under build/fuzz.

TARGET="${1:-both}"
LLVM_CLANG="${LLVM_CLANG:-/usr/local/opt/llvm/bin/clang}"
DURATION_LEXER="${DURATION_LEXER:-180}"   # seconds
DURATION_PARSER="${DURATION_PARSER:-600}" # seconds
BUILD_DIR="build/fuzz"
CORPUS_LEXER="$BUILD_DIR/corpus_lexer"
CORPUS_PARSER="$BUILD_DIR/corpus_parser"
ARTIFACTS="$BUILD_DIR/artifacts"

if [ ! -x "$LLVM_CLANG" ]; then
    echo "error: $LLVM_CLANG not found or not executable." >&2
    echo "Apple's Xcode clang lacks the libFuzzer runtime; install brew's" >&2
    echo "llvm (brew install llvm) or set LLVM_CLANG to a clang that has it." >&2
    exit 1
fi

mkdir -p "$ARTIFACTS"

run_lexer() {
    mkdir -p "$CORPUS_LEXER"
    cp tests/lexer/*.mn "$CORPUS_LEXER"/ 2>/dev/null || true
    echo "==> Building fuzz_lexer ($LLVM_CLANG)"
    "$LLVM_CLANG" -std=c11 -g -O1 -fsanitize=fuzzer,address,undefined \
        -o "$BUILD_DIR/fuzz_lexer" tools/mannac/lexer.c tools/mannac/fuzz_lexer.c
    echo "==> Fuzzing lexer for ${DURATION_LEXER}s"
    "$BUILD_DIR/fuzz_lexer" -max_total_time="$DURATION_LEXER" \
        -artifact_prefix="$ARTIFACTS/lexer_" "$CORPUS_LEXER" || true
}

run_parser() {
    mkdir -p "$CORPUS_PARSER"
    cp tests/parser/*.mn "$CORPUS_PARSER"/ 2>/dev/null || true
    echo "==> Building fuzz_parser (MANNA_FUZZ_TRACE_LAST_RESORT on)"
    "$LLVM_CLANG" -std=c11 -g -O1 -fsanitize=fuzzer,address,undefined \
        -DMANNA_FUZZ_TRACE_LAST_RESORT \
        -o "$BUILD_DIR/fuzz_parser" \
        tools/mannac/arena.c tools/mannac/ast.c tools/mannac/diag.c \
        tools/mannac/parser_state.c tools/mannac/parser.c tools/mannac/lexer.c \
        tools/mannac/fuzz_parser.c
    echo "==> Fuzzing parser for ${DURATION_PARSER}s"
    "$BUILD_DIR/fuzz_parser" -max_total_time="$DURATION_PARSER" \
        -artifact_prefix="$ARTIFACTS/parser_" "$CORPUS_PARSER" || true
}

case "$TARGET" in
    lexer)  run_lexer ;;
    parser) run_parser ;;
    both)   run_lexer; run_parser ;;
    *) echo "usage: $0 [lexer|parser|both]" >&2; exit 1 ;;
esac

echo "==> Done."
if ls "$ARTIFACTS"/* >/dev/null 2>&1; then
    echo "Crash/timeout artifacts found under $ARTIFACTS:"
    ls -la "$ARTIFACTS"
else
    echo "No crash/timeout artifacts."
fi
