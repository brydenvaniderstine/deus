#!/usr/bin/env bash
# Boots build/esp (make stage-esp) under QEMU with the Milestone 1 machine and waits for the serial log to reach a stop line, then kills QEMU (step 20h).
#   tools/qemu_boot.sh OUTDIR STOP_REGEX [SECS]
# Writes OUTDIR/serial.log and OUTDIR/qemu.log. Succeeds when STOP_REGEX appears in the serial log within SECS (default 180; plus a settle time so the lines after it
# are written); FAILS (status 1) when it does not, or when QEMU exits early. The machine is $QEMU_MACHINE, exported by the Makefile from ONE definition
# (-machine q35 -m 8G -cpu qemu64,+apic,+pdpe1gb): this script never shrinks it. Needs QEMU, OVMF_CODE and OVMF_VARS (the Makefile passes them).
set -u
OUT="${1:?usage: qemu_boot.sh OUTDIR STOP_REGEX [SECS]}"
STOP="${2:?}"
SECS="${3:-180}"
: "${QEMU:=qemu-system-x86_64}" "${QEMU_MACHINE:?QEMU_MACHINE not set (run through make)}" "${OVMF_CODE:?}" "${OVMF_VARS:?}" "${ESP:=build/esp}"
for f in "$OVMF_CODE" "$OVMF_VARS"; do [ -f "$f" ] || { echo "qemu_boot: FAILED: no firmware file $f (install OVMF/edk2 or set OVMF_CODE / OVMF_VARS)"; exit 1; }; done
command -v "$QEMU" > /dev/null || { echo "qemu_boot: FAILED: $QEMU not found"; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT"
cp "$OVMF_VARS" "$OUT/vars.fd"
# shellcheck disable=SC2086
$QEMU $QEMU_MACHINE \
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
    -drive if=pflash,format=raw,file="$OUT/vars.fd" \
    -drive format=raw,file=fat:rw:"$ESP" \
    -serial file:"$OUT/serial.log" -monitor none -display none -no-reboot \
    -d int,cpu_reset -D "$OUT/qemu.log" &
pid=$!
t=0
while [ $t -lt "$SECS" ]; do
    if ! kill -0 $pid 2> /dev/null; then break; fi
    if [ -f "$OUT/serial.log" ] && sed 's/\x1b\[[0-9;?=]*[A-Za-z]//g; s/\r//g' "$OUT/serial.log" | grep -Eq "$STOP"; then break; fi
    sleep 1
    t=$((t + 1))
done
found=0
if [ -f "$OUT/serial.log" ] && sed 's/\x1b\[[0-9;?=]*[A-Za-z]//g; s/\r//g' "$OUT/serial.log" | grep -Eq "$STOP"; then found=1; sleep 3; fi
kill $pid 2> /dev/null; wait $pid 2> /dev/null
if [ $found -ne 1 ]; then
    echo "qemu_boot: FAILED: the serial log never reached /$STOP/ within ${SECS}s (the kernel hung, died, or QEMU/OVMF did not start); log: $OUT/serial.log"
    exit 1
fi
echo "qemu_boot: reached /$STOP/ after ${t}s"
