#!/usr/bin/env bash
# Step 21j: reading the Makefile must not print anything on stderr when QEMU is not installed (CI run 40546449691, macOS: "usage: dirname string [...]" twice, from
# `dirname $(dirname $(command -v qemu...))` with nothing to dirname). Also: when a QEMU IS on PATH, QEMU_PREFIX is still its prefix (the fix must not blank it).
set -u
cd "$(dirname "$0")/.."
fail=0
check() { if [ "$2" = ok ]; then echo "PASS: $1"; else echo "FAIL: $1"; fail=1; fi; }

# A PATH that holds every tool of the system directories EXCEPT qemu-system-x86_64, so the test runs the same on every machine, also where QEMU is installed in /usr/bin
# (Linux packages, Homebrew's prefix on an Intel Mac): a directory of symlinks, one per executable of /usr/bin and /bin, skipping any qemu-system-*.
bin=$(mktemp -d)
trap 'rm -rf "$bin"' EXIT
mkdir "$bin/path"
for d in /usr/bin /bin; do
    for f in "$d"/*; do
        n=${f##*/}
        case "$n" in qemu-system-*) continue;; esac
        [ -e "$bin/path/$n" ] || ln -s "$f" "$bin/path/$n" 2>/dev/null
    done
done
if PATH="$bin/path" command -v qemu-system-x86_64 >/dev/null; then echo "FAIL: qemu-system-x86_64 is still on the simulated PATH: this test cannot show the no-QEMU case"; exit 1; fi
if env -i PATH="$bin/path" make -n check-batch-cap >"$bin/out" 2>"$bin/err"; then check "make -n with no QEMU on PATH succeeds" ok; else check "make -n with no QEMU on PATH succeeds" no; fi
if [ ! -s "$bin/err" ]; then check "... and prints nothing on stderr" ok; else check "... and prints nothing on stderr ($(head -c 80 "$bin/err"))" no; fi

# a fake qemu in /x/y/bin: the prefix is /x/y (this one also works where a real QEMU exists: the fake comes first on PATH)
fake=$(mktemp -d)
mkdir -p "$fake/y/bin"
printf '#!/bin/sh\n' >"$fake/y/bin/qemu-system-x86_64"
chmod +x "$fake/y/bin/qemu-system-x86_64"
got=$(printf 'print-prefix:\n\t@echo $(QEMU_PREFIX)\n' | PATH="$fake/y/bin:$PATH" make -f Makefile -f - print-prefix 2>/dev/null | tail -1)
rm -rf "$fake"
case "$got" in */y) check "with a QEMU on PATH the prefix is its install prefix ($got)" ok;; *) check "with a QEMU on PATH the prefix is its install prefix (got '$got')" no;; esac
exit $fail
