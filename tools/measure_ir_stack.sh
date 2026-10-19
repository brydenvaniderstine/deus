#!/usr/bin/env bash
# Typed-IR step 3b: how little host stack does the IR dumper need on a maximal
# (MAX_EXPR_HEIGHT = 512) tree, per shape, plain and ASan+UBSan builds? Bisects
# `ulimit -s` (KB) over `ir_test --shape NAME` and reports, per shape, the
# smallest stack that passed and the margin 8192 / that. The gate (docs/TYPED_IR.md
# plan, review fix 3): every shape keeps a margin of at least 4x (passes at 2048
# KB); under 4x in ANY shape prints STOP and exits 1.
#
# Only a SIGNAL exit (rc > 128) counts as "did not survive this stack". Any other
# nonzero rc (an assert, a usage error) is a harness/test failure, not a stack
# result: it is reported as ERROR and aborts, never folded into the bisection
# (docs/REFINEMENT.md, Retro 2026-10-13 (a)). ASan reports a stack overflow by
# exiting with status 1 on Linux, indistinguishable from an ordinary failure
# (the reviewer's run: "asan binary: ERROR (non-signal failure at 32 KB)"), so
# ASAN_OPTIONS=abort_on_error=1 is exported below: ASan then dies by SIGABRT
# (rc 134), which is classed as a crash. Since the shape passes at the full
# stack, a low-stack abort cannot be a test assert.
# Not part of CI: `make measure-ir-stack`. CC=gcc on Linux.
set -u
export ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}abort_on_error=1"
CC="${CC:-clang}"
BUILD="${BUILD:-build/measure_ir}"
SRC="tools/mannac"
SRCS="$SRC/arena.c $SRC/ast.c $SRC/diag.c $SRC/parser_state.c $SRC/parser.c $SRC/lexer.c $SRC/ir.c $SRC/ir_dump.c $SRC/ir_build.c $SRC/ir_decls.c $SRC/ir_expr.c $SRC/ir_body.c $SRC/ir_calls.c $SRC/ir_names.c $SRC/ir_rename.c $SRC/ir_test.c"
SHAPES="binary mixed_prec field index postfix_mix in_calls in_ifs"
FULL=8192
GATE=2048
mkdir -p "$BUILD"
"$CC" -std=c11 -O2 -g -o "$BUILD/t" $SRCS || exit 2
"$CC" -std=c11 -g -fsanitize=address,undefined -o "$BUILD/t-asan" $SRCS || exit 2
echo "compiler: $CC ($("$CC" --version 2>&1 | head -1))   full stack: $FULL KB   gate: passes at $GATE KB (4x)"

# run BIN SHAPE KB -> prints pass | crash | error
run() {
    ( ulimit -s "$3" 2>/dev/null || { echo error; exit 0; }
      "$1" --shape "$2" >/dev/null 2>&1
      rc=$?
      if [ $rc -eq 0 ]; then echo pass; elif [ $rc -gt 128 ]; then echo crash; else echo error; fi ) 2>/dev/null
}

stop=0
for build in plain asan; do
    bin="$BUILD/t"; [ "$build" = asan ] && bin="$BUILD/t-asan"
    for sh in $SHAPES; do
        r=$(run "$bin" "$sh" $FULL)
        if [ "$r" != pass ]; then echo "$build $sh: does not pass even at $FULL KB ($r)"; stop=1; continue; fi
        lo=32; hi=$FULL   # lo assumed to fail (checked), hi known to pass
        r=$(run "$bin" "$sh" $lo)
        [ "$r" = error ] && { echo "$build $sh: ERROR (non-signal failure at $lo KB) - not a stack result"; exit 2; }
        while [ $((hi - lo)) -gt 16 ]; do
            mid=$(( (lo + hi) / 2 ))
            r=$(run "$bin" "$sh" $mid)
            case "$r" in
                pass) hi=$mid ;;
                crash) lo=$mid ;;
                *) echo "$build $sh: ERROR (non-signal failure at $mid KB) - not a stack result"; exit 2 ;;
            esac
        done
        margin=$(awk -v f=$FULL -v h=$hi 'BEGIN{printf "%.1f", f/h}')
        verdict=ok
        if [ "$hi" -gt "$GATE" ]; then verdict="STOP (under 4x)"; stop=1; fi
        printf '%-5s %-12s smallest passing stack <= %5d KB   margin %5sx   %s\n' "$build" "$sh" "$hi" "$margin" "$verdict"
    done
done
[ $stop -eq 0 ] || { echo "STOP: a shape is under the 4x gate (or failed outright); do not proceed"; exit 1; }
echo "all shapes keep >= 4x"
