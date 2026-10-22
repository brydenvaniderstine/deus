#!/usr/bin/env bash
# Re-runs every tests/audit_repros input and compares with the recorded output. Usage: tools/run_audit_repros.sh <ir_dump>
set -u
BIN="${1:?usage: run_audit_repros.sh <path-to-ir_dump>}"
D=tests/audit_repros
fail=0; n=0
tmp=$(mktemp -d)
for mn in "$D"/*.mn; do
    case "$mn" in *.fixed.mn) continue ;; esac
    base="${mn%.mn}"; n=$((n+1))
    "$BIN" "$mn" > "$tmp/o" 2>&1;               cmp -s "$tmp/o" "$base.out"       || { echo "FAIL $mn (ir_dump)"; diff "$tmp/o" "$base.out"; fail=1; }
    "$BIN" --fixed "$mn" > "$tmp/f" 2>&1;       cmp -s "$tmp/f" "$base.fixed.mn"  || { echo "FAIL $mn (--fixed)"; diff "$tmp/f" "$base.fixed.mn"; fail=1; }
    "$BIN" "$base.fixed.mn" > "$tmp/r" 2>&1;    cmp -s "$tmp/r" "$base.fixed.out" || { echo "FAIL $mn (re-check)"; diff "$tmp/r" "$base.fixed.out"; fail=1; }
done
rm -f "$tmp/o" "$tmp/f" "$tmp/r"; rmdir "$tmp"
[ $fail -eq 0 ] && echo "audit repros: $n inputs, all match"
exit $fail
