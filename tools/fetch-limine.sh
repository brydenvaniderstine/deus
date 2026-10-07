#!/usr/bin/env bash
# Vendors Limine (gitignored - not tracked in this repo) at a pinned
# binary-release tag. Clones it if missing; if a limine/ checkout already
# exists, verifies it's at the exact pinned commit rather than trusting
# whatever's there - a stale or wrong checkout would otherwise fail
# bafflingly wherever it's next consumed, instead of here, loudly.
set -euo pipefail

LIMINE_DIR="limine"
LIMINE_TAG="v8.7.0-binary"
LIMINE_PINNED_HASH="aad3edd370955449717a334f0289dee10e2c5f01"

if [ ! -d "$LIMINE_DIR" ]; then
    git clone --quiet --branch="$LIMINE_TAG" --depth=1 \
        https://github.com/limine-bootloader/limine.git "$LIMINE_DIR"
fi

actual_hash=$(git -C "$LIMINE_DIR" rev-parse HEAD)
if [ "$actual_hash" != "$LIMINE_PINNED_HASH" ]; then
    echo "fetch-limine.sh: $LIMINE_DIR is at $actual_hash, expected pinned $LIMINE_PINNED_HASH ($LIMINE_TAG) - refusing to build against an unverified Limine checkout." >&2
    exit 1
fi
