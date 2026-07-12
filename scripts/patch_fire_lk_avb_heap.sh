#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

set -euo pipefail

# Diagnostic-only helper. Fire's preloader verifies the LK payload after
# certificate validation, so a patched LK is not bootable on the stock SBC path.
# Production v4 GKI packages must keep the signed stock LK.

die() {
	echo "error: $*" >&2
	exit 1
}

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
LK_SOURCE=${LK_SOURCE:-/home/deb/crdroid_fire_payload_20260630/lk.img}
LK_OUT_DIR=${LK_OUT_DIR:-${REPO_ROOT}/dist/lk}
LK_OUT=${LK_OUT:-${LK_OUT_DIR}/fire-lk-avbheap-256m.img}
LK_PADDED_OUT=${LK_PADDED_OUT:-${LK_OUT_DIR}/fire-lk-avbheap-256m.padded.img}
LK_PARTITION_BYTES=${LK_PARTITION_BYTES:-8388608}
FIRE66_LK_AVB_HEAP_BYTES=${FIRE66_LK_AVB_HEAP_BYTES:-0x10000000}

[ -f "$LK_SOURCE" ] || die "LK_SOURCE not found: $LK_SOURCE"
mkdir -p "$LK_OUT_DIR"

case "$FIRE66_LK_AVB_HEAP_BYTES" in
	0x10000000|0X10000000|268435456)
		heap_name=256m
		patch_r0_hex=4ff08050
		patch_r2_hex=4ff08052
	;;
	*)
		die "unsupported FIRE66_LK_AVB_HEAP_BYTES: $FIRE66_LK_AVB_HEAP_BYTES"
	;;
esac

python3 - "$LK_SOURCE" "$LK_OUT" "$patch_r0_hex" "$patch_r2_hex" <<'PY'
import binascii
import pathlib
import sys

source = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2])
patch_r0 = binascii.unhexlify(sys.argv[3])
patch_r2 = binascii.unhexlify(sys.argv[4])

data = bytearray(source.read_bytes())
patches = [
    (0x24918, binascii.unhexlify("4ff42000"), patch_r0),
    (0x2494c, binascii.unhexlify("4ff42002"), patch_r2),
]

for off, expected, replacement in patches:
    cur = bytes(data[off:off + len(expected)])
    if cur != expected:
        raise SystemExit(
            f"{source}: unexpected bytes at 0x{off:x}: "
            f"{cur.hex()} != {expected.hex()}"
        )
    data[off:off + len(expected)] = replacement

out.write_bytes(data)
PY

cp -f "$LK_OUT" "$LK_PADDED_OUT"
truncate -s "$LK_PARTITION_BYTES" "$LK_PADDED_OUT"

{
	echo "Fire LK AVB heap patch (${heap_name})"
	echo "source=$LK_SOURCE"
	echo "raw=$LK_OUT"
	echo "padded=$LK_PADDED_OUT"
	echo "patches:"
	echo "  0x24918: 4ff42000 -> ${patch_r0_hex}"
	echo "  0x2494c: 4ff42002 -> ${patch_r2_hex}"
	echo
	sha256sum "$LK_SOURCE" "$LK_OUT" "$LK_PADDED_OUT"
} | tee "${LK_OUT_DIR}/fire-lk-avbheap-${heap_name}.txt"
