#!/usr/bin/env bash
# Rebuilds tests/audit_repros/: one input per m2-ir-v0 audit finding, with the output the CURRENT code gives
# (NN_name.out = `ir_dump`; NN_name.fixed.mn = `ir_dump --fixed`; NN_name.fixed.out = `ir_dump` of that fixed source).
# Inputs are copied from tests/ir and tests/ir_fix (finding 5's is generated here; the R2_*.mn files of the re-audit are committed as given). The outputs were read by hand
# when this was written; `tools/run_audit_repros.sh` is the checker. Usage: tools/make_audit_repros.sh <ir_dump>
set -eu
BIN="${1:?usage: make_audit_repros.sh <path-to-ir_dump>}"
D=tests/audit_repros
mkdir -p "$D"

put() { cp "$2" "$D/$1.mn"; }
put 01_oversized_let_annotation_with_a_literal   tests/ir/one_error_let_oversized_annotation_with_a_literal.mn
put 02_assign_target_calls                       tests/ir/assign_target_calls_count_toward_recursion.mn
put 03_len_after_a_closed_scope_of_a_local_len   tests/ir/len_bare_after_the_scope_of_a_local_named_len_closed.mn
put 04_e0261_fix_respects_the_expected_type      tests/ir_fix/fix_e0261_no_fix_when_no_lossless_wrap_gives_the_expected_type.mn
put 04b_e0261_fix_widens_to_the_expected_type    tests/ir_fix/fix_e0261_with_an_expected_type_widens_to_it.mn
put 06_duplicate_let_rename                      tests/ir_fix/fix_duplicate_let_is_renamed_with_the_first.mn
put 07_sibling_loops_share_a_fix_target          tests/ir_fix/fix_sibling_loops_share_a_fix_target.mn
put 08a_function_used_as_a_value                 tests/ir_fix/fix_function_used_as_a_value.mn
put 08b_field_given_twice                        tests/ir_fix/fix_field_given_twice_in_a_construction.mn
put 09_size_overflow_in_the_outer_multiplication tests/ir/one_error_let_size_overflow_in_the_outer_multiplication.mn

# The re-audit's inputs (R2_*.mn, the auditor's files, copied verbatim into the directory; the loop below gives them outputs too).

# Finding 5 (KNOWN ISSUE, not fixed): a 511-operation chain whose E0261 fix adds one level and fails E0111.
python3 - "$D/05_e0261_fix_on_a_511_operation_chain_KNOWN_ISSUE.mn" <<'PY'
import sys
n = 511
body = "a" + " + a" * (n - 1) + " + b"      # n operations, the last one mixes u8 and u32
open(sys.argv[1], "w").write("manna v0;\nfn f(a: u8, b: u32) {\n    let r: u32 = %s;\n}\n" % body)
PY

for mn in "$D"/*.mn; do
    case "$mn" in *.fixed.mn) continue ;; esac
    base="${mn%.mn}"
    "$BIN" "$mn" > "$base.out" || true
    "$BIN" --fixed "$mn" > "$base.fixed.mn"
    "$BIN" "$base.fixed.mn" > "$base.fixed.out" || true
done
