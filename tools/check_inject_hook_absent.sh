#!/bin/sh
# Step 21b (reviewer change 6): the fault-injection hook must not exist in a normal build. Builds ir_dump twice from the same sources, once as the Makefile
# does (no -DMANNA_POISON_INJECT) and once with the macro, and looks at the symbol tables with nm: the normal one must hold no `inject` symbol and no
# resolve_type_impl wrapper; the injected one must hold both, which proves the check can fail. (Every golden being unchanged is `make test-ir`, in CI.)
set -eu
cd "$(dirname "$0")/.."
out=build/nocascade
mkdir -p "$out"
cc=${MANNAC_CC:-clang}
srcs="tools/mannac/arena.c tools/mannac/ast.c tools/mannac/diag.c tools/mannac/parser_state.c tools/mannac/parser.c tools/mannac/lexer.c tools/mannac/ir.c tools/mannac/ir_dump.c tools/mannac/ir_build.c tools/mannac/ir_decls.c tools/mannac/ir_expr.c tools/mannac/ir_body.c tools/mannac/ir_calls.c tools/mannac/ir_names.c tools/mannac/ir_rename.c tools/mannac/ir_selfcheck.c tools/mannac/ir_main.c"
$cc -std=c11 -O2 -g -o "$out/ir_dump_normal" $srcs
$cc -std=c11 -O0 -g -DMANNA_POISON_INJECT -o "$out/ir_dump_injected" $srcs
bad=$(nm "$out/ir_dump_normal" | grep -E 'inject|resolve_type_impl' || true)
if [ -n "$bad" ]; then
    echo "check_inject_hook_absent: the NORMAL build has injection symbols:"; echo "$bad"; exit 1
fi
good=$(nm "$out/ir_dump_injected" | grep -E 'ir_inject_type|ir_inject_target' || true)
if [ -z "$good" ]; then
    echo "check_inject_hook_absent: the injected build shows no injection symbol, so this check proves nothing (it cannot fail)"; exit 1
fi
echo "check_inject_hook_absent: ok (normal build: no inject symbol; injected build: $(echo "$good" | wc -l | tr -d ' ') of them)"
