#!/usr/bin/env bash
# Step 21j: reading the Makefile must not print anything on stderr when QEMU is not installed (CI run 40546449691, macOS: "usage: dirname string [...]" twice, from
# `dirname $(dirname $(command -v qemu...))` with nothing to dirname). Also: when a QEMU IS on PATH, QEMU_PREFIX is still its prefix (the fix must not blank it).
set -u
cd "$(dirname "$0")/.."
fail=0
check() { if [ "$2" = ok ]; then echo "PASS: $1"; else echo "FAIL: $1"; fail=1; fi; }

# a PATH of the system directories only: it has make, the shell and dirname, and (checked below) no qemu-system-x86_64
sys_path=/usr/bin:/bin
bin=$(mktemp -d)
trap 'rm -rf "$bin"' EXIT
if PATH="$sys_path" command -v qemu-system-x86_64 >/dev/null; then echo "FAIL: qemu-system-x86_64 is in $sys_path: this test cannot show the no-QEMU case"; exit 1; fi
if env -i PATH="$sys_path" make -n check-batch-cap >"$bin/out" 2>"$bin/err"; then check "make -n with no QEMU on PATH succeeds" ok; else check "make -n with no QEMU on PATH succeeds" no; fi
if [ ! -s "$bin/err" ]; then check "... and prints nothing on stderr" ok; else check "... and prints nothing on stderr ($(head -c 80 "$bin/err"))" no; fi

# a fake qemu in /x/y/bin: the prefix is /x/y
fake=$(mktemp -d)
mkdir -p "$fake/y/bin"
printf '#!/bin/sh\n' >"$fake/y/bin/qemu-system-x86_64"
chmod +x "$fake/y/bin/qemu-system-x86_64"
got=$(printf 'print-prefix:\n\t@echo $(QEMU_PREFIX)\n' | PATH="$fake/y/bin:$PATH" make -f Makefile -f - print-prefix 2>/dev/null | tail -1)
rm -rf "$fake"
case "$got" in */y) check "with a QEMU on PATH the prefix is its install prefix ($got)" ok;; *) check "with a QEMU on PATH the prefix is its install prefix (got '$got')" no;; esac
exit $fail
