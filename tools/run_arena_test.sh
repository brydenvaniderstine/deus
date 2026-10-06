#!/usr/bin/env bash
# Runs the arena_test binary's normal in-process suite, then separately
# checks that overflowing with no overflow target registered aborts
# (signal, not a normal exit) - that case has to be its own process since
# abort() ends the whole program. Used by both test-arena and
# test-arena-asan so the check can't drift between them.
set -u

BIN="${1:?usage: run_arena_test.sh <path-to-arena_test-binary>}"

"$BIN" || exit 1

status=0
"$BIN" --no-target-overflow || status=$?
if [ "$status" -ge 128 ]; then
    echo "PASS: no-target overflow aborts (signal $((status - 128)))"
else
    echo "FAIL: expected abort (status >=128), got $status"
    exit 1
fi
