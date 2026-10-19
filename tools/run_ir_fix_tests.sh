#!/usr/bin/env bash
# Typed-IR step 14: the rename fix-its, end to end. For every tests/ir_fix/NAME.mn:
#   1. `ir_dump --fixed NAME.mn` (the source with EVERY fix applied) must equal NAME.fixed.mn;
#   2. re-checking the fixed source with `ir_dump` must give NAME.remaining if that file exists (the diagnostics
#      that are deliberately not fixable), otherwise it must be clean: not an ERRORS list (the IR dump, status 0);
#   3. applying the fixes to the fixed source again must change nothing (the fixes are complete) - or, when one fix
#      unblocks another (a declaration error hid the uses of a rename), give NAME.fixed2.mn.
# Used by `make test-ir-fix` and `make test-ir-fix-asan` (run_ir_tests.sh's pattern).
set -u

BIN="${1:?usage: run_ir_fix_tests.sh <path-to-ir_dump-binary>}"
fail=0
tmp=$(mktemp -d)

for mn in tests/ir_fix/*.mn; do
    case "$mn" in *.fixed.mn|*.fixed2.mn) continue ;; esac
    base="${mn%.mn}"
    "$BIN" --fixed "$mn" >"$tmp/fixed.mn" 2>"$tmp/err"
    rc=$?
    if [ -s "$tmp/err" ] || [ $rc -ne 0 ]; then
        echo "FAIL $mn (--fixed: rc=$rc stderr: $(cat "$tmp/err"))"
        fail=1
        continue
    fi
    if ! cmp -s "$tmp/fixed.mn" "$base.fixed.mn"; then
        echo "FAIL $mn (fixed source differs from $base.fixed.mn)"
        diff "$tmp/fixed.mn" "$base.fixed.mn"
        fail=1
        continue
    fi
    "$BIN" "$tmp/fixed.mn" >"$tmp/recheck" 2>"$tmp/err"
    rc=$?
    if [ -s "$tmp/err" ] || [ $rc -ne 0 ]; then
        echo "FAIL $mn (re-check: rc=$rc stderr: $(cat "$tmp/err"))"
        fail=1
        continue
    fi
    if [ -f "$base.remaining" ]; then
        if ! cmp -s "$tmp/recheck" "$base.remaining"; then
            echo "FAIL $mn (re-check differs from $base.remaining)"
            diff "$tmp/recheck" "$base.remaining"
            fail=1
            continue
        fi
    elif head -1 "$tmp/recheck" | grep -q '^ERRORS:'; then
        echo "FAIL $mn (the fixed source still has errors; list them in $base.remaining if that is intended)"
        cat "$tmp/recheck"
        fail=1
        continue
    fi
    "$BIN" --fixed "$tmp/fixed.mn" >"$tmp/fixed2.mn" 2>/dev/null
    want2="$tmp/fixed.mn"
    [ -f "$base.fixed2.mn" ] && want2="$base.fixed2.mn"
    if ! cmp -s "$want2" "$tmp/fixed2.mn"; then
        echo "FAIL $mn (applying the fixes again: not the expected second-round source)"
        diff "$want2" "$tmp/fixed2.mn"
        fail=1
        continue
    fi
    echo "PASS $mn"
done

rm -rf "$tmp"
exit $fail
