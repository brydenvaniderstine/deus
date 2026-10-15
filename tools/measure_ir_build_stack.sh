#!/usr/bin/env bash
# Typed-IR step 6c: how much host stack does the REAL builder (parser -> ir_build) need on a
# maximal parser-built tree, per live shape and variant, plain and ASan+UBSan?
#   shape    binary / mixed_prec (height 512, the parser's E0111 bound) / neg_chain (as many
#            minus signs as the nesting bound E0103 accepts, ~253) / in_ifs (a 512-high chain
#            inside 120 nested ifs: the scope machinery plus the expression recursion)
#   variant  int  = built against an integer return type (check_group)
#            bool = the one-mismatch path (require_group)
# For each, bisects `ulimit -s` (KB, 4 KB steps) for the smallest stack that passes at full
# height (S_full) and at the minimum height (S_base); the per-level cost is
# (S_full - S_base) / levels, an upper bound to the bisect resolution. GATE: every case must
# pass at 2048 KB (4x margin at 8 MB); under 4x in ANY case prints STOP and exits 1. The
# M2_PLAN ceiling is ~16 KB per level at 512 levels in 8 MB.
# At the FULL stack only a pass is accepted: a signal exit (rc > 128) or any other nonzero rc
# there is a harness/test failure, reported and aborting (docs/REFINEMENT.md, Retro 2026-10-13
# (a)). Inside the bisection (the case already passed at the full stack) a smaller-stack failure
# of either kind counts as "did not survive". ASAN_OPTIONS=abort_on_error=1 makes ASan die by SIGABRT on Linux (where it
# otherwise exits 1); since the case passes at the full stack, a low-stack abort is not an
# assert. The parser's own frames are part of the measurement (parse, then build, in one
# process); for the shapes whose parse is iterative (binary, mixed_prec) the build dominates,
# for neg_chain both recurse.
# Not part of CI: `make measure-ir-build-stack`. CC=gcc on Linux.
set -u
export ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}abort_on_error=1"
CC="${CC:-clang}"
BUILD="${BUILD:-build/measure_ir_build}"
SRC="tools/mannac"
SRCS="$SRC/arena.c $SRC/ast.c $SRC/diag.c $SRC/parser_state.c $SRC/parser.c $SRC/lexer.c $SRC/ir.c $SRC/ir_dump.c $SRC/ir_build.c $SRC/ir_decls.c $SRC/ir_expr.c $SRC/ir_body.c $SRC/ir_test.c"
FULL=8192
GATE=2048
STEP=4
mkdir -p "$BUILD"
"$CC" -std=c11 -O2 -g -o "$BUILD/t" $SRCS || exit 2
"$CC" -std=c11 -g -fsanitize=address,undefined -o "$BUILD/t-asan" $SRCS || exit 2
echo "compiler: $CC ($("$CC" --version 2>&1 | head -1))   full stack: $FULL KB   gate: passes at $GATE KB (4x)"

# run BIN KB SHAPE HEIGHT VARIANT -> pass | crash | error   (stdout of the run goes to $BUILD/out)
run() {
    ( ulimit -s "$2" 2>/dev/null || { echo error; exit 0; }
      "$1" --build-shape "$3" "$4" "$5" >"$BUILD/out" 2>&1
      rc=$?
      if [ $rc -eq 0 ]; then echo pass; elif [ $rc -gt 128 ]; then echo crash; else echo error; fi ) 2>/dev/null
}

# bisect BIN SHAPE HEIGHT VARIANT -> prints the smallest passing KB (or FAIL / ERROR)
bisect() {
    local r lo=16 hi=$FULL mid
    r=$(run "$1" $FULL "$2" "$3" "$4")
    [ "$r" = pass ] || { echo "$r"; return; }
    # The level count comes from THIS run, which passed at the full stack; $BUILD/out is
    # overwritten by every later bisection step, and a crashed run prints no count.
    grep -o 'levels=[0-9]*' "$BUILD/out" | head -1 | cut -d= -f2 > "$BUILD/levels"
    while [ $((hi - lo)) -gt $STEP ]; do
        mid=$(( (lo + hi) / 2 ))
        r=$(run "$1" $mid "$2" "$3" "$4")
        case "$r" in
            pass) hi=$mid ;;
            # The case passed at the full stack, so below it a failure can only come from
            # the stack: a signal, or (ASan at a few dozen KB fails during its own startup,
            # with a plain exit status) a non-signal exit. Both mean "did not survive".
            crash|error) lo=$mid ;;
        esac
    done
    echo "$hi"
}

stop=0
printf '%-5s %-11s %-4s %9s %9s %8s %12s %7s\n' build shape var "S_full KB" "S_base KB" levels "B/level (<=)" margin
for build in plain asan; do
    bin="$BUILD/t"; [ "$build" = asan ] && bin="$BUILD/t-asan"
    for shape in binary mixed_prec neg_chain in_ifs; do
        case "$shape" in
            binary) full=512; base=1 ;;
            mixed_prec) full=512; base=2 ;;
            neg_chain) full=0; base=1 ;;
            in_ifs) full=512; base=1 ;;   # the chain inside 120 nested ifs; the base keeps the 120 ifs, so B/level is the expression's
        esac
        for var in int bool; do
            sf=$(bisect "$bin" "$shape" $full $var)
            case "$sf" in FAIL|crash|ERROR|error) echo "$build $shape $var: $sf"; stop=1; continue ;; esac
            levels=$(cat "$BUILD/levels")
            if ! [ "${levels:-0}" -gt 0 ] 2>/dev/null; then echo "$build $shape $var: no level count from the full-stack run"; stop=1; continue; fi
            sb=$(bisect "$bin" "$shape" $base $var)
            case "$sb" in crash|ERROR|error|FAIL) sb=0 ;; esac
            per=$(awk -v f=$sf -v b=$sb -v l=$levels 'BEGIN{printf "%.0f", (f-b+4)*1024/l}')
            margin=$(awk -v f=$FULL -v h=$sf 'BEGIN{printf "%.1f", f/h}')
            verdict=""
            if [ "$sf" -gt "$GATE" ]; then verdict="  STOP (under 4x)"; stop=1; fi
            printf '%-5s %-11s %-4s %9d %9d %8d %12d %6sx%s\n' "$build" "$shape" "$var" "$sf" "$sb" "$levels" "$per" "$margin" "$verdict"
        done
    done
done
echo "pending shapes (live when their constructs land): in_calls (step 10), field (step 11), index and postfix_mix (step 12)"
[ $stop -eq 0 ] || { echo "STOP: a case is under the 4x gate (or failed outright); do not proceed"; exit 1; }
echo "all live cases keep >= 4x"
