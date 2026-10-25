#!/usr/bin/env bash
# The kernel's size budget (step 20g; register K7): CLAUDE.md "Goals": "Kernel scope: single-person-comprehensible, ~120k LOC max. Kernel only - this budget excludes
# mannac and the Wasm runtime". Counts the lines of every .c, .S and .h file under kernel/ and fails over the budget. Usage: tools/check_kernel_loc.sh [dir] [budget]
# (defaults: kernel, 120000). make check-kernel-loc
set -u
DIR="${1:-kernel}"
BUDGET="${2:-120000}"
[ -d "$DIR" ] || { echo "check_kernel_loc: FAILED: $DIR is not a directory"; exit 2; }
total=$(find "$DIR" -type f \( -name '*.c' -o -name '*.S' -o -name '*.h' \) -exec cat {} + | wc -l | tr -d ' ')
files=$(find "$DIR" -type f \( -name '*.c' -o -name '*.S' -o -name '*.h' \) | wc -l | tr -d ' ')
if [ "$total" -gt "$BUDGET" ]; then
    echo "check_kernel_loc: FAILED: $total lines in $files files under $DIR, over the budget of $BUDGET (CLAUDE.md Goals)"
    exit 1
fi
echo "check_kernel_loc: ok ($total lines in $files files under $DIR; budget $BUDGET)"
