#!/usr/bin/env bash
# Typed-IR step 2b: measure how tall an expression tree the AST printer (the
# recursive walker the E0111 bound protects) survives on an 8 MB host stack,
# per chain shape, plain and ASan+UBSan builds, with the bound DISABLED
# (-DMANNA_MAX_EXPR_HEIGHT=65000, below the uint16_t field's range; HI=30000
# keeps the tallest shape, postfix_mix at height 2L+1, under it).
#
# Method (docs/M2_PLAN.md "Typed IR (stage 3)", Input contract):
#   1. for each shape, find L_max = the largest chain length whose tree parses
#      with no diagnostics AND is walked by decl_print without crashing;
#      bisect between 1 and HI (the plain build is tested at HI only - it is
#      not the binding build, and a pass at HI is reported as ">=HI");
#   2. report the tree HEIGHT at L_max (height_probe prints it), not L, so the
#      shapes are comparable;
#   3. N = the largest power of two <= (smallest ASan height) / 8, floor 512.
#      If that is < 512 this script prints STOP and N is NOT chosen.
# Output of every run goes to /dev/null (the dump is quadratic in height).
# Crash rule (REFINEMENT retro 2026-10-13, register V5, step 20g): only a SIGNAL exit (rc > 128) is a crash, i.e. a measurement. Any other nonzero exit
# (an assert, a usage error, an LSan leak report) is a harness failure: the script stops with status 2 and says so; it is never counted as a crash (which would
# move the measured limit) and never as a pass.
# Not part of CI: it runs for minutes. `make measure-height` (the rule has its own test, make test-measure-height-script).
set -u
HI="${HI:-30000}"
CC="${CC:-clang}"   # CC=gcc on Linux; the flags below are common to both
BUILD="${BUILD:-build/measure}"
SRC="tools/mannac"
SRCS="$SRC/arena.c $SRC/ast.c $SRC/diag.c $SRC/parser_state.c $SRC/parser.c $SRC/lexer.c $SRC/height_probe.c"
mkdir -p "$BUILD"
# SKIP_BUILD=1 with PROBE_PLAIN / PROBE_ASAN: use these probes instead of building them (the test of the crash rule feeds fake ones)
PROBE_PLAIN="${PROBE_PLAIN:-$BUILD/probe}"
PROBE_ASAN="${PROBE_ASAN:-$BUILD/probe-asan}"
if [ -z "${SKIP_BUILD:-}" ]; then
    "$CC" -std=c11 -O2 -g -DMANNA_MAX_EXPR_HEIGHT=65000 -o "$PROBE_PLAIN" $SRCS || exit 2
    "$CC" -std=c11 -g -fsanitize=address,undefined -DMANNA_MAX_EXPR_HEIGHT=65000 -o "$PROBE_ASAN" $SRCS || exit 2
fi
ulimit -s 8192 || { echo "cannot set an 8 MB stack"; exit 2; }
echo "compiler: $CC ($("$CC" --version 2>&1 | head -1))   stack limit: $(ulimit -s) KB   HI=$HI"

# gen SHAPE L  -> writes $BUILD/in.mn
gen() {
    local shape="$1" L="$2" expr
    case "$shape" in
        binary)      expr=$(awk -v n="$L" 'BEGIN{printf "1"; for(i=0;i<n;i++) printf "+1"}') ;;
        mixed_prec)  expr=$(awk -v n="$L" 'BEGIN{printf "a"; for(i=0;i<n;i++) printf "+b*c"}') ;;
        field)       expr=$(awk -v n="$L" 'BEGIN{printf "a"; for(i=0;i<n;i++) printf ".f"}') ;;
        index)       expr=$(awk -v n="$L" 'BEGIN{printf "a"; for(i=0;i<n;i++) printf "[0]"}') ;;
        postfix_mix) expr=$(awk -v n="$L" 'BEGIN{printf "a"; for(i=0;i<n;i++) printf ".f[0]"}') ;;
        in_calls)    expr=$(awk -v n="$L" 'BEGIN{for(i=0;i<200;i++) printf "f("; printf "1"; for(i=0;i<n;i++) printf "+1"; for(i=0;i<200;i++) printf ")"}') ;;
        in_ifs)
            { echo "manna v0;"; echo "fn f() {"; awk 'BEGIN{for(i=0;i<120;i++) print "if a {"}'
              printf 'let x = '; awk -v n="$L" 'BEGIN{printf "1"; for(i=0;i<n;i++) printf "+1"}'; echo ';'
              awk 'BEGIN{for(i=0;i<120;i++) print "}"}'; echo "}"; } > "$BUILD/in.mn"; return ;;
    esac
    printf 'manna v0;\nfn f() { let x = %s; }\n' "$expr" > "$BUILD/in.mn"
}

# run BIN -> sets RESULT=ok|crash|diag, FIRST (first diagnostic code) and MAXH
run() {
    local out rc
    out=$("$1" "$BUILD/in.mn" 2>/dev/null); rc=$?
    MAXH=$(printf '%s\n' "$out" | sed -n 's/.*maxh=\([0-9]*\).*/\1/p' | head -1)
    FIRST=$(printf '%s\n' "$out" | sed -n 's/.*first=\([A-Z0-9-]*\).*/\1/p' | head -1)
    if [ "$FIRST" = E0111 ]; then echo "ERROR: E0111 fired during measurement - the measurement bound is too low for HI=$HI"; exit 2; fi
    if [ $rc -gt 128 ]; then RESULT=crash                       # a signal: the printer did not survive this stack
    elif [ $rc -ne 0 ]; then
        echo "ERROR: the probe exited with status $rc, which is not a signal (rc > 128): a harness failure, not a measurement ($1 on $BUILD/in.mn)"; exit 2
    elif printf '%s\n' "$out" | grep -q 'diags=0' && printf '%s\n' "$out" | grep -q '^printed'; then RESULT=ok
    elif printf '%s\n' "$out" | grep -q 'diags=0'; then
        echo "ERROR: the probe exited 0 and parsed cleanly but never printed: a harness failure, not a measurement ($1)"; exit 2
    else RESULT=diag; fi
}

# measure BIN SHAPE -> sets LMAX (number or ">=HI") and HMAX (height at LMAX)
measure() {
    local bin="$1" shape="$2" lo hi mid
    gen "$shape" "$HI"; run "$bin"
    if [ "$RESULT" = ok ]; then LMAX=">=$HI"; HMAX=$MAXH; return; fi
    if [ "$RESULT" = diag ]; then LMAX="diag($FIRST)@$HI"; HMAX=0; return; fi
    lo=1; hi=$HI
    while [ $((hi - lo)) -gt 1 ]; do
        mid=$(( (lo + hi) / 2 )); gen "$shape" "$mid"; run "$bin"
        if [ "$RESULT" = ok ]; then lo=$mid; else hi=$mid; fi
    done
    gen "$shape" "$lo"; run "$bin"; LMAX=$lo; HMAX=$MAXH
}

printf '%-12s | %-26s | %-26s\n' shape "plain -O2: L_max (height)" "ASan+UBSan: L_max (height)"
printf -- '-------------+----------------------------+---------------------------\n'
min_h=999999
for shape in binary mixed_prec field index postfix_mix in_calls in_ifs; do
    measure "$PROBE_PLAIN" "$shape";     pl="$LMAX ($HMAX)"
    measure "$PROBE_ASAN" "$shape";      as="$LMAX ($HMAX)"
    printf '%-12s | %-26s | %-26s\n' "$shape" "$pl" "$as"
    case "$LMAX" in *">="*|*diag*) ;; *) [ "$HMAX" -lt "$min_h" ] && min_h=$HMAX ;; esac
done
echo
if [ "$min_h" -eq 999999 ]; then
    # No shape failed within HI on the ASan build, so there is no limit to
    # divide: "min_height/8 < 512" would be a made-up STOP. Say what happened.
    echo "NO LIMIT FOUND: every shape reached HI=$HI under ASan without a crash or a diagnostic."
    echo "The binding height is therefore > $HI (not measured). N is NOT chosen by this run;"
    echo "raise HI to find the limit (HI must stay below 65000/2 for the postfix_mix shape,"
    echo "because the measurement bound is 65000 and that shape's height is 2*HI+1)."
    exit 3
fi
echo "smallest ASan tree height that the printer survives: $min_h"
n=512; cand=$(( min_h / 8 ))
if [ "$cand" -lt 512 ]; then
    echo "STOP: min_height/8 = $cand < 512 - N is NOT chosen; report instead of lowering the floor."
    exit 1
fi
p=512; while [ $((p * 2)) -le "$cand" ]; do p=$((p * 2)); done
echo "N = $p   (largest power of two <= $min_h / 8 = $cand; floor 512)"
