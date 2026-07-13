#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

set -euo pipefail

die() {
	echo "error: $*" >&2
	exit 1
}

need_tool() {
	command -v "$1" >/dev/null 2>&1 || die "$1 is required"
}

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
KERNEL_ROOT=${KERNEL_ROOT:-/home/deb/kernel_platform/kernel}
ARTIFACT_DIR=${1:-${REPO_ROOT}/dist/rom/mgk_64_k66.userdebug}
AVBTOOL=${AVBTOOL:-${KERNEL_ROOT}/prebuilts/kernel-build-tools/linux-x86/bin/avbtool}
UNPACK_BOOTIMG=${UNPACK_BOOTIMG:-${KERNEL_ROOT}/system/tools/mkbootimg/unpack_bootimg.py}
BOOT_IMG=${BOOT_IMG:-${ARTIFACT_DIR}/boot.img}
VENDOR_BOOT_IMG=${VENDOR_BOOT_IMG:-${ARTIFACT_DIR}/vendor_boot.img}
VBMETA_IMG=${VBMETA_IMG:-${ARTIFACT_DIR}/vbmeta.img}
DTB_IMG=${DTB_IMG:-${ARTIFACT_DIR}/mt6768.dtb}
FIRE66_VENDOR_BOOT_MODULES_FILE=${FIRE66_VENDOR_BOOT_MODULES_FILE:-}
FIRE66_EXPECT_VENDOR_CMDLINE_TOKEN=${FIRE66_EXPECT_VENDOR_CMDLINE_TOKEN:-bootopt=64S3,32N2,64N2}
FIRE66_EXPECT_TEXT_OFFSET=${FIRE66_EXPECT_TEXT_OFFSET:-0x80000}
FIRE66_BOOT_PARTITION_BYTES=${FIRE66_BOOT_PARTITION_BYTES:-134217728}
FIRE66_VENDOR_BOOT_PARTITION_BYTES=${FIRE66_VENDOR_BOOT_PARTITION_BYTES:-8388608}
FIRE66_LK_AVB_HEAP_BYTES=${FIRE66_LK_AVB_HEAP_BYTES:-0x8c00000}
FIRE66_LK_AVB_HEAP_RESERVE_BYTES=${FIRE66_LK_AVB_HEAP_RESERVE_BYTES:-0x400000}

need_tool python3
need_tool gzip
need_tool lz4
need_tool cpio
[ -x "$AVBTOOL" ] || die "avbtool not executable: $AVBTOOL"
[ -x "$UNPACK_BOOTIMG" ] || die "unpack_bootimg not executable: $UNPACK_BOOTIMG"
[ -f "$BOOT_IMG" ] || die "boot.img not found: $BOOT_IMG"
[ -f "$VENDOR_BOOT_IMG" ] || die "vendor_boot.img not found: $VENDOR_BOOT_IMG"
[ -f "$VBMETA_IMG" ] || die "vbmeta.img not found: $VBMETA_IMG"
[ -f "$DTB_IMG" ] || die "mt6768.dtb not found: $DTB_IMG"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

check_boot() {
	local boot=$1

	echo "==> Checking boot.img"
	python3 - "$boot" "$FIRE66_BOOT_PARTITION_BYTES" "$FIRE66_EXPECT_TEXT_OFFSET" <<'PY'
import gzip
import pathlib
import struct
import sys

boot = pathlib.Path(sys.argv[1])
partition_size = int(sys.argv[2], 0)
expected_text_offset = int(sys.argv[3], 0)
data = boot.read_bytes()

if data[:8] != b"ANDROID!":
    raise SystemExit(f"{boot}: missing ANDROID! magic")
if len(data) != partition_size:
    raise SystemExit(f"{boot}: expected {partition_size} bytes, got {len(data)}")

kernel_size = struct.unpack_from("<I", data, 8)[0]
ramdisk_size = struct.unpack_from("<I", data, 12)[0]
header_version = struct.unpack_from("<I", data, 40)[0]
if header_version != 4:
    raise SystemExit(f"{boot}: expected boot header v4, got v{header_version}")

signature_size = struct.unpack_from("<I", data, 1580)[0]
if signature_size != 4096:
    raise SystemExit(f"{boot}: expected GKI signature size 4096, got {signature_size}")

footer_fmt = ">4sIIQQQ28s"
footer_size = struct.calcsize(footer_fmt)
magic, _major, _minor, original, vbmeta_offset, vbmeta_size, _reserved = struct.unpack(
    footer_fmt, data[-footer_size:]
)
if magic != b"AVBf":
    raise SystemExit(f"{boot}: missing AVB footer")
if data[vbmeta_offset:vbmeta_offset + 4] != b"AVB0":
    raise SystemExit(f"{boot}: missing embedded AVB0 vbmeta")

kernel = data[4096:4096 + kernel_size]
if kernel[:2] == b"\x1f\x8b":
    kernel = gzip.decompress(kernel)
if len(kernel) < 0x40 or kernel[0x38:0x3c] != b"ARMd":
    raise SystemExit(f"{boot}: kernel is not an ARM64 Image")
text_offset = struct.unpack_from("<Q", kernel, 0x08)[0]
if text_offset != expected_text_offset:
    raise SystemExit(
        f"{boot}: ARM64 text_offset is 0x{text_offset:x}, "
        f"expected 0x{expected_text_offset:x}"
    )

print(f"boot header      : v{header_version}")
print(f"kernel size      : {kernel_size}")
print(f"ramdisk size     : {ramdisk_size}")
print(f"GKI signature    : {signature_size}")
print(f"ARM64 text_offset: 0x{text_offset:x}")
print(f"AVB original     : {original}")
print(f"AVB vbmeta       : offset={vbmeta_offset} size={vbmeta_size}")
PY
}

check_vendor_boot() {
	local vendor_boot=$1
	local out_dir="${tmp}/vendor_boot"
	local unpack_log="${tmp}/vendor_boot.unpack"
	local ramdisk modules_load
	local ramdisk_dir="${tmp}/vendor_ramdisk"

	echo "==> Checking vendor_boot.img"
	python3 - "$vendor_boot" "$FIRE66_VENDOR_BOOT_PARTITION_BYTES" "$FIRE66_EXPECT_VENDOR_CMDLINE_TOKEN" <<'PY'
import pathlib
import struct
import sys

vendor_boot = pathlib.Path(sys.argv[1])
partition_size = int(sys.argv[2], 0)
expected_cmdline_token = sys.argv[3]
data = vendor_boot.read_bytes()

if data[:8] != b"VNDRBOOT":
    raise SystemExit(f"{vendor_boot}: missing VNDRBOOT magic")
if len(data) > partition_size:
    raise SystemExit(
        f"{vendor_boot}: size {len(data)} exceeds partition size {partition_size}"
    )
header_version = struct.unpack_from("<I", data, 8)[0]
page_size = struct.unpack_from("<I", data, 12)[0]
kernel_addr = struct.unpack_from("<I", data, 16)[0]
ramdisk_addr = struct.unpack_from("<I", data, 20)[0]
cmdline = data[28:28 + 2048].split(b"\0", 1)[0].decode("ascii", "replace")
if header_version != 4:
    raise SystemExit(f"{vendor_boot}: expected vendor_boot header v4, got v{header_version}")
if page_size != 4096:
    raise SystemExit(f"{vendor_boot}: expected page size 4096, got {page_size}")
if expected_cmdline_token and expected_cmdline_token not in cmdline.split():
    raise SystemExit(
        f"{vendor_boot}: vendor cmdline missing {expected_cmdline_token!r}: {cmdline!r}"
    )

print(f"vendor_boot header: v{header_version}")
print(f"vendor_boot size  : {len(data)}")
print(f"kernel_addr       : 0x{kernel_addr:08x}")
print(f"ramdisk_addr      : 0x{ramdisk_addr:08x}")
print(f"vendor cmdline    : {cmdline}")
PY

	mkdir -p "$out_dir" "$ramdisk_dir"
	"$UNPACK_BOOTIMG" --boot_img "$vendor_boot" --out "$out_dir" > "$unpack_log"
	while IFS= read -r ramdisk; do
		(
			cd "$ramdisk_dir"
			if gzip -t "$ramdisk" >/dev/null 2>&1; then
				gzip -dc "$ramdisk"
			elif lz4 -q -t "$ramdisk" >/dev/null 2>&1; then
				lz4 -dc "$ramdisk"
			else
				die "unsupported vendor ramdisk compression: $ramdisk"
			fi | cpio -idmu --no-absolute-filenames >/dev/null 2>&1
		)
	done < <(find "$out_dir" -maxdepth 1 -type f -name 'vendor_ramdisk*' | sort)
	[ -n "$(find "$out_dir" -maxdepth 1 -type f -name 'vendor_ramdisk*' -print -quit)" ] ||
		die "vendor_boot has no vendor ramdisk"
	[ -f "${ramdisk_dir}/first_stage_ramdisk/fstab.mt6768" ] ||
		die "vendor_boot is missing first_stage_ramdisk/fstab.mt6768"
	modules_load=$(find "$ramdisk_dir" -type f -path '*/lib/modules*/modules.load' -print -quit)
	[ -n "$modules_load" ] || die "vendor_boot is missing modules.load"
	if [ -n "$FIRE66_VENDOR_BOOT_MODULES_FILE" ]; then
		diff -u "$FIRE66_VENDOR_BOOT_MODULES_FILE" "$modules_load" ||
			die "vendor_boot modules.load differs from $FIRE66_VENDOR_BOOT_MODULES_FILE"
	fi
	echo "vendor_boot fstab : first_stage_ramdisk/fstab.mt6768"
	echo "vendor_boot modules: $(grep -c . "$modules_load") entries"
}

check_vbmeta() {
	local vbmeta=$1
	local info="${tmp}/vbmeta.info"

	echo "==> Checking vbmeta.img"
	"$AVBTOOL" info_image --image "$vbmeta" > "$info"
	grep -q 'Partition Name:[[:space:]]*boot$' "$info" ||
		die "vbmeta does not describe boot"
	grep -q 'Partition Name:[[:space:]]*vendor_boot$' "$info" ||
		die "vbmeta does not describe vendor_boot"
	grep -q 'Partition Name:[[:space:]]*dtbo$' "$info" ||
		die "vbmeta does not describe dtbo"
	python3 - "$info" "$FIRE66_VENDOR_BOOT_PARTITION_BYTES" <<'PY'
import pathlib
import re
import sys

info = pathlib.Path(sys.argv[1]).read_text()
limit = int(sys.argv[2], 0)
image_size = None
for block in info.split("Hash descriptor:")[1:]:
    if not re.search(r"Partition Name:\s+vendor_boot(?:\n|$)", block):
        continue
    match = re.search(r"Image Size:\s+([0-9]+) bytes", block)
    if not match:
        raise SystemExit("vbmeta vendor_boot descriptor has no image size")
    image_size = int(match.group(1))
    break
if image_size is None:
    raise SystemExit("vbmeta vendor_boot hash descriptor not found")
if image_size > limit:
    raise SystemExit(
        f"vendor_boot descriptor size {image_size} exceeds partition {limit}"
    )
print(f"vendor_boot descriptor size: {image_size}")
PY
}

check_lk_heap_budget() {
	local loaded budget

	loaded=$((FIRE66_BOOT_PARTITION_BYTES + FIRE66_VENDOR_BOOT_PARTITION_BYTES))
	budget=$((FIRE66_LK_AVB_HEAP_BYTES - FIRE66_LK_AVB_HEAP_RESERVE_BYTES))
	echo "==> Checking stock LK AVB heap model"
	echo "AVB loaded bytes : ${loaded}"
	echo "AVB heap budget  : ${budget}"
	[ "$loaded" -le "$budget" ] ||
		die "boot + vendor_boot exceeds stock LK AVB heap budget"
}

check_boot "$BOOT_IMG"
check_vendor_boot "$VENDOR_BOOT_IMG"
check_vbmeta "$VBMETA_IMG"
check_lk_heap_budget
echo "Fire GKI 6.6 artifacts look coherent: $ARTIFACT_DIR"
