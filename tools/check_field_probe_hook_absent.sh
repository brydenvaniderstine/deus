#!/bin/sh
# Register C3 (step 23b): the field-lookup counter must not exist in a normal build. Builds ir_dump twice from the same sources, once as the Makefile does (no
# -DMANNA_COUNT_FIELD_PROBES) and once with the macro, and looks at the symbol tables with nm: the normal one must hold no `ir_field_probe` / `ir_field_lookups` symbol; the
# counting one must hold all three counters, which proves the check can fail. (Same shape as check_inject_hook_absent.sh.)
set -eu
cd "$(dirname "$0")/.."
out=build/fl
mkdir -p "$out"
cc=${MANNAC_CC:-clang}
srcs="tools/mannac/arena.c tools/mannac/ast.c tools/mannac/diag.c tools/mannac/parser_state.c tools/mannac/parser.c tools/mannac/lexer.c tools/mannac/ir.c tools/mannac/ir_dump.c tools/mannac/ir_build.c tools/mannac/ir_decls.c tools/mannac/ir_expr.c tools/mannac/ir_body.c tools/mannac/ir_calls.c tools/mannac/ir_names.c tools/mannac/ir_rename.c tools/mannac/ir_selfcheck.c tools/mannac/ir_main.c"
$cc -std=c11 -O2 -g -o "$out/ir_dump_normal" $srcs
$cc -std=c11 -O2 -g -DMANNA_COUNT_FIELD_PROBES -o "$out/ir_dump_counting" $srcs
bad=$(nm "$out/ir_dump_normal" | grep -E 'ir_field_probe|ir_field_lookups' || true)
if [ -n "$bad" ]; then
    echo "check_field_probe_hook_absent: the NORMAL build has the field-lookup counter:"; echo "$bad"; exit 1
fi
good=$(nm "$out/ir_dump_counting" | grep -E 'ir_field_probes|ir_field_lookups|ir_field_probe_max' || true)
if [ "$(echo "$good" | grep -c .)" -ne 3 ]; then
    echo "check_field_probe_hook_absent: the counting build lacks a counter, so this check proves nothing (it cannot fail)"; echo "$good"; exit 1
fi
echo "check_field_probe_hook_absent: ok (normal build: no counter; counting build: $(echo "$good" | wc -l | tr -d ' ') of them)"
