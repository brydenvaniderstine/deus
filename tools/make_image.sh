#!/usr/bin/env bash
# Build a GPT disk image with a single FAT32 EFI System Partition containing
# EFI/BOOT/BOOTX64.EFI, limine.conf, and the kernel ELF, then install the
# Limine stage-2 data via the limine deployer.
set -euo pipefail

IMAGE="${1:?usage: make_image.sh <image> <kernel_elf> <limine_dir>}"
KERNEL_ELF="${2:?}"
LIMINE_DIR="${3:?}"

SIZE_MB=64

for tool in mformat mmd mcopy sgdisk; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "error: '$tool' not found. Install with: brew install mtools gptfdisk" >&2
        exit 1
    fi
done

rm -f "$IMAGE"
dd if=/dev/zero of="$IMAGE" bs=1M count="$SIZE_MB" status=none

sgdisk --clear \
    --new=1:2048:0 --typecode=1:ef00 --change-name=1:"EFI System" \
    "$IMAGE" >/dev/null

# Partition 1 starts at LBA 2048, 512-byte sectors.
OFFSET=$((2048 * 512))

mformat -i "$IMAGE"@@"$OFFSET" -F ::
mmd -i "$IMAGE"@@"$OFFSET" ::/EFI
mmd -i "$IMAGE"@@"$OFFSET" ::/EFI/BOOT
mmd -i "$IMAGE"@@"$OFFSET" ::/boot
mcopy -i "$IMAGE"@@"$OFFSET" "$LIMINE_DIR/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$IMAGE"@@"$OFFSET" limine.conf ::/limine.conf
mcopy -i "$IMAGE"@@"$OFFSET" "$KERNEL_ELF" ::/boot/kernel.elf

echo "image: $IMAGE ready"
