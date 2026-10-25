#!/usr/bin/env bash
# Prints the command that runs an LLVM tool (llvm-cov, llvm-profdata): `xcrun <tool>` on macOS, else the plain or a versioned name on PATH (llvm-cov-18 ...).
# Exits 1 with a message if there is none: a missing tool is a failure, never a skipped check. Usage: tools/llvm_tool.sh llvm-cov
t="${1:?usage: llvm_tool.sh llvm-cov|llvm-profdata}"
if command -v xcrun > /dev/null 2>&1 && xcrun --find "$t" > /dev/null 2>&1; then echo "xcrun $t"; exit 0; fi
for c in "$t" "$t-19" "$t-18" "$t-17" "$t-16" "$t-15" "$t-14"; do
    if command -v "$c" > /dev/null 2>&1; then echo "$c"; exit 0; fi
done
echo "llvm_tool: $t not found (xcrun or PATH): install LLVM" >&2
exit 1
