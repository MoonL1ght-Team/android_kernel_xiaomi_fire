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
DTB_IMG=${DTB_IMG:-${ARTIFACT_DIR}/dtb.img}
FIRE66_VENDOR_BOOT_MODULES_FILE=${FIRE66_VENDOR_BOOT_MODULES_FILE:-}
FIRE66_VENDOR_BOOT_RECOVERY_MODULES_FILE=${FIRE66_VENDOR_BOOT_RECOVERY_MODULES_FILE:-}
FIRE66_EXPECT_VENDOR_CMDLINE_TOKEN=${FIRE66_EXPECT_VENDOR_CMDLINE_TOKEN:-bootopt=64S3,32N2,64N2}
FIRE66_EXPECT_BOOT_HEADER_VERSION=${FIRE66_EXPECT_BOOT_HEADER_VERSION:-3}
FIRE66_EXPECT_VENDOR_BOOT_HEADER_VERSION=${FIRE66_EXPECT_VENDOR_BOOT_HEADER_VERSION:-3}
FIRE66_EXPECT_TEXT_OFFSET=${FIRE66_EXPECT_TEXT_OFFSET:-0x0}
FIRE66_EXPECT_DTBO_IN_VBMETA=${FIRE66_EXPECT_DTBO_IN_VBMETA:-0}
FIRE66_EXPECT_BOOT_AVB_ROLLBACK_LOCATION=${FIRE66_EXPECT_BOOT_AVB_ROLLBACK_LOCATION:-0}
FIRE66_EXPECT_VBMETA_BOOT_CHAIN_ROLLBACK_LOCATION=${FIRE66_EXPECT_VBMETA_BOOT_CHAIN_ROLLBACK_LOCATION:-3}
FIRE66_BOOT_PARTITION_BYTES=${FIRE66_BOOT_PARTITION_BYTES:-134217728}
FIRE66_VENDOR_BOOT_PARTITION_BYTES=${FIRE66_VENDOR_BOOT_PARTITION_BYTES:-67108864}

need_tool python3
need_tool gzip
need_tool lz4
need_tool cpio
[ -x "$AVBTOOL" ] || die "avbtool not executable: $AVBTOOL"
[ -x "$UNPACK_BOOTIMG" ] || die "unpack_bootimg not executable: $UNPACK_BOOTIMG"
[ -f "$BOOT_IMG" ] || die "boot.img not found: $BOOT_IMG"
[ -f "$VENDOR_BOOT_IMG" ] || die "vendor_boot.img not found: $VENDOR_BOOT_IMG"
[ -f "$VBMETA_IMG" ] || die "vbmeta.img not found: $VBMETA_IMG"
[ -f "$DTB_IMG" ] || die "dtb.img not found: $DTB_IMG"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

check_boot() {
	local boot=$1
	local avb_info="${tmp}/boot.avb.info"

	echo "==> Checking boot.img"
	python3 - "$boot" "$FIRE66_BOOT_PARTITION_BYTES" "$FIRE66_EXPECT_TEXT_OFFSET" "$FIRE66_EXPECT_BOOT_HEADER_VERSION" <<'PY'
import gzip
import pathlib
import struct
import sys

boot = pathlib.Path(sys.argv[1])
partition_size = int(sys.argv[2], 0)
expected_text_offset = int(sys.argv[3], 0)
expected_header_version = int(sys.argv[4], 0)
data = boot.read_bytes()

if data[:8] != b"ANDROID!":
    raise SystemExit(f"{boot}: missing ANDROID! magic")
if len(data) != partition_size:
    raise SystemExit(f"{boot}: expected {partition_size} bytes, got {len(data)}")

kernel_size = struct.unpack_from("<I", data, 8)[0]
ramdisk_size = struct.unpack_from("<I", data, 12)[0]
header_version = struct.unpack_from("<I", data, 40)[0]
if header_version != expected_header_version:
    raise SystemExit(
        f"{boot}: expected boot header v{expected_header_version}, got v{header_version}"
    )
signature_size = 0
if header_version >= 4:
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
if header_version >= 4:
    print(f"GKI signature    : {signature_size}")
print(f"ARM64 text_offset: 0x{text_offset:x}")
print(f"AVB original     : {original}")
print(f"AVB vbmeta       : offset={vbmeta_offset} size={vbmeta_size}")
PY
	"$AVBTOOL" info_image --image "$boot" > "$avb_info"
	python3 - "$avb_info" "$FIRE66_EXPECT_BOOT_AVB_ROLLBACK_LOCATION" <<'PY'
import pathlib
import re
import sys

info = pathlib.Path(sys.argv[1]).read_text()
expected = int(sys.argv[2], 0)
match = re.search(r"^Rollback Index Location:\s+([0-9]+)$", info, re.M)
if not match:
    raise SystemExit("boot AVB info has no rollback location")
actual = int(match.group(1))
if actual != expected:
    raise SystemExit(
        f"boot AVB rollback location is {actual}, expected {expected}"
    )
print(f"boot AVB rollback location: {actual}")
PY
}

check_vendor_boot() {
	local vendor_boot=$1
	local out_dir="${tmp}/vendor_boot"
	local unpack_log="${tmp}/vendor_boot.unpack"
	local ramdisk modules_load modules_load_recovery
	local ramdisk_dir="${tmp}/vendor_ramdisk"

	echo "==> Checking vendor_boot.img"
	python3 - "$vendor_boot" "$FIRE66_VENDOR_BOOT_PARTITION_BYTES" "$FIRE66_EXPECT_VENDOR_CMDLINE_TOKEN" "$FIRE66_EXPECT_VENDOR_BOOT_HEADER_VERSION" <<'PY'
import pathlib
import struct
import sys

vendor_boot = pathlib.Path(sys.argv[1])
partition_size = int(sys.argv[2], 0)
expected_cmdline_token = sys.argv[3]
expected_header_version = int(sys.argv[4], 0)
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
if header_version != expected_header_version:
    raise SystemExit(
        f"{vendor_boot}: expected vendor_boot header v{expected_header_version}, got v{header_version}"
    )
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
	modules_load_recovery=$(find "$ramdisk_dir" -type f -path '*/lib/modules*/modules.load.recovery' -print -quit)
	if [ -n "$FIRE66_VENDOR_BOOT_RECOVERY_MODULES_FILE" ]; then
		[ -n "$modules_load_recovery" ] || die "vendor_boot is missing modules.load.recovery"
		diff -u "$FIRE66_VENDOR_BOOT_RECOVERY_MODULES_FILE" "$modules_load_recovery" ||
			die "vendor_boot modules.load.recovery differs from $FIRE66_VENDOR_BOOT_RECOVERY_MODULES_FILE"
	fi
	echo "vendor_boot fstab : first_stage_ramdisk/fstab.mt6768"
	echo "vendor_boot modules: $(grep -c . "$modules_load") entries"
	[ -z "$modules_load_recovery" ] || \
		echo "vendor_boot recovery modules: $(grep -c . "$modules_load_recovery") entries"
}

check_dtb_image() {
	local dtb=$1

	echo "==> Checking dtb.img"
	python3 - "$dtb" <<'PY'
import pathlib
import struct
import sys

dtb = pathlib.Path(sys.argv[1])
data = dtb.read_bytes()
if len(data) < 32:
    raise SystemExit(f"{dtb}: too small for Android DT table header")
magic, total_size, header_size, entry_size, entry_count, entries_offset, page_size, version = struct.unpack(
    ">8I", data[:32]
)
if magic != 0xD7B7AB1E:
    raise SystemExit(f"{dtb}: expected Android DT table magic d7b7ab1e, got 0x{magic:08x}")
if total_size != len(data):
    raise SystemExit(f"{dtb}: DT table total_size {total_size} != file size {len(data)}")
if header_size != 32 or entry_size != 32:
    raise SystemExit(f"{dtb}: unexpected DT table header_size={header_size} entry_size={entry_size}")
if entry_count < 1:
    raise SystemExit(f"{dtb}: DT table has no entries")
if page_size != 2048:
    raise SystemExit(f"{dtb}: expected DT table page_size 2048, got {page_size}")
print(f"dtb table entries: {entry_count}")
print(f"dtb table size   : {total_size}")
print(f"dtb table version: {version}")
PY
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
	[ "$FIRE66_EXPECT_DTBO_IN_VBMETA" != 1 ] ||
		grep -q 'Partition Name:[[:space:]]*dtbo$' "$info" ||
		die "vbmeta does not describe dtbo"
	python3 - "$info" "$FIRE66_VENDOR_BOOT_PARTITION_BYTES" "$FIRE66_EXPECT_VBMETA_BOOT_CHAIN_ROLLBACK_LOCATION" <<'PY'
import pathlib
import re
import sys

info = pathlib.Path(sys.argv[1]).read_text()
limit = int(sys.argv[2], 0)
expected_boot_chain_location = int(sys.argv[3], 0)
image_size = None
boot_chain_location = None
for block in info.split("Chain Partition descriptor:")[1:]:
    if not re.search(r"Partition Name:\s+boot(?:\n|$)", block):
        continue
    match = re.search(r"Rollback Index Location:\s+([0-9]+)", block)
    if not match:
        raise SystemExit("vbmeta boot chain descriptor has no rollback location")
    boot_chain_location = int(match.group(1))
    break
if boot_chain_location is None:
    raise SystemExit("vbmeta boot chain descriptor not found")
if boot_chain_location != expected_boot_chain_location:
    raise SystemExit(
        f"vbmeta boot chain rollback location is {boot_chain_location}, "
        f"expected {expected_boot_chain_location}"
    )
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
print(f"vbmeta boot chain rollback location: {boot_chain_location}")
PY
}

check_boot "$BOOT_IMG"
check_vendor_boot "$VENDOR_BOOT_IMG"
check_dtb_image "$DTB_IMG"
check_vbmeta "$VBMETA_IMG"
echo "Fire GKI 6.6 artifacts look coherent: $ARTIFACT_DIR"
