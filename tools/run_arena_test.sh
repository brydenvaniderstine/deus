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

# Host out-of-memory: arena_create must print its message and exit(1) -
# not abort, not return. ASan's allocator aborts on an absurd request by
# default instead of returning NULL; allocator_may_return_null=1 makes it
# behave like the plain allocator so both builds exercise the same branch.
status=0
oom_msg=$(ASAN_OPTIONS=allocator_may_return_null=1 "$BIN" --oom 2>&1 >/dev/null) || status=$?
if [ "$status" -eq 1 ] && printf '%s' "$oom_msg" | grep -q "out of memory creating"; then
    echo "PASS: unsatisfiable arena size exits 1 with the out-of-memory message"
else
    echo "FAIL: expected exit 1 + out-of-memory message, got status $status: $oom_msg"
    exit 1
fi
