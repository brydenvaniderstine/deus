#!/usr/bin/env bash
# Test of the crash rule in tools/measure_expr_height.sh (step 20g; register V5): only a signal exit (rc > 128) is a crash; any other nonzero exit is a harness
# failure (status 2); a probe that always survives finds no limit (status 3). Fake probes stand in for the real one. make test-measure-height-script
set -u
cd "$(dirname "$0")/.."
T=$(mktemp -d); fail=0
mk() { printf '#!/bin/bash\n%s\n' "$2" > "$T/$1"; chmod +x "$T/$1"; }
mk ok 'echo "diags=0 first=- maxh=40"; echo printed'
mk sig 'if [ $(wc -c < "$1") -gt 120 ]; then kill -SEGV $$; fi; echo "diags=0 first=- maxh=40"; echo printed'   # survives small inputs, dies on big ones
mk rc1 'echo "assertion failed" >&2; exit 1'
mk rc3 'echo "diags=0 first=- maxh=40"; echo printed; exit 3'
run() { PROBE_PLAIN="$T/$1" PROBE_ASAN="$T/$1" SKIP_BUILD=1 BUILD="$T/b" HI=64 tools/measure_expr_height.sh > "$T/out" 2>&1; echo $?; }
rc=$(run rc1)
if [ "$rc" = 2 ] && grep -q "not a signal" "$T/out"; then echo "PASS: a probe that exits 1 is a harness failure (status 2), not a crash"; else echo "FAIL: exit-1 probe: rc=$rc"; tail -3 "$T/out"; fail=1; fi
rc=$(run rc3)
if [ "$rc" = 2 ] && grep -q "status 3, which is not a signal" "$T/out"; then echo "PASS: a probe that prints and then exits 3 is a harness failure, not a pass"; else echo "FAIL: exit-3 probe: rc=$rc"; tail -3 "$T/out"; fail=1; fi
rc=$(run sig)
if [ "$rc" = 1 ] && grep -q "STOP" "$T/out" && ! grep -q "NO LIMIT" "$T/out"; then echo "PASS: a probe killed by a signal is a crash: the limit is found (and here STOPs the script), never a pass"; else echo "FAIL: signal probe: rc=$rc"; tail -3 "$T/out"; fail=1; fi
rc=$(run ok)
if [ "$rc" = 3 ] && grep -q "NO LIMIT FOUND" "$T/out"; then echo "PASS: a probe that always survives finds no limit (status 3)"; else echo "FAIL: ok probe: rc=$rc"; tail -3 "$T/out"; fail=1; fi
# the REAL ASan probe under a small stack (Linux review of batch C, finding 3): its stack overflow must be classed as a crash (a measurement), never a harness
# failure. ASAN_OPTIONS=abort_on_error=0 is put in the environment on purpose: that is what a Linux ASan run does by default (exit status 1 on the overflow), and the
# script has to override it. The plain probe is the same fake as above (it survives); only the ASan build matters here.
mk plainok 'echo "diags=0 first=- maxh=40"; echo printed'
CC="${CC:-clang}"; SRC=tools/mannac
if "$CC" -std=c11 -g -fsanitize=address,undefined -DMANNA_MAX_EXPR_HEIGHT=65000 -o "$T/probe-asan" $SRC/arena.c $SRC/ast.c $SRC/diag.c $SRC/parser_state.c $SRC/parser.c $SRC/lexer.c $SRC/height_probe.c 2> "$T/cc.err"; then
    ASAN_OPTIONS=abort_on_error=0 PROBE_PLAIN="$T/plainok" PROBE_ASAN="$T/probe-asan" SKIP_BUILD=1 BUILD="$T/r" HI=4000 STACK_KB=128 tools/measure_expr_height.sh > "$T/out" 2>&1; rc=$?
    if [ $rc -ne 2 ] && ! grep -q "not a signal" "$T/out" && grep -q "binary" "$T/out" && grep -Eq '^binary +\| [^|]+\| [0-9]+ \(' "$T/out"; then echo "PASS: the real ASan probe's stack overflow is a crash (a measured limit), not a harness failure, even with abort_on_error=0 in the environment"; else echo "FAIL: real ASan probe under a 128 KB stack: rc=$rc"; tail -8 "$T/out"; fail=1; fi
else echo "FAIL: the ASan probe does not build"; cat "$T/cc.err"; fail=1; fi
rm -rf "$T"
exit $fail
