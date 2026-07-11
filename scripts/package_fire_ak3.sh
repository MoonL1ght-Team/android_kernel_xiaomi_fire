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

first_file() {
	local path

	for path in "$@"; do
		[ -f "$path" ] && {
			printf '%s\n' "$path"
			return 0
		}
	done

	return 1
}

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
KERNEL_ROOT=${KERNEL_ROOT:-/home/deb/kernel_platform/kernel}
KERNEL_SRC=${KERNEL_SRC:-${KERNEL_ROOT}/kernel-6.6}
DEVICE_MODULES_DIR=${DEVICE_MODULES_DIR:-kernel_device_modules-6.6}
DEVICE_MODULES_SRC=${DEVICE_MODULES_SRC:-${REPO_ROOT}}
OUT_DIR=${OUT_DIR:-${KERNEL_ROOT}/out}
PROJECT=${PROJECT:-mgk_64_k66}
MODE=${MODE:-userdebug}
AK3_TEMPLATE=${AK3_TEMPLATE:-/home/deb/kernel-4.19/out/fire/AnyKernel3}
DIST_DIR=${DIST_DIR:-${REPO_ROOT}/dist/ak3}
ROM_ARTIFACTS_DIR=${ROM_ARTIFACTS_DIR:-${REPO_ROOT}/dist/rom/${PROJECT}.${MODE}}
STAGE_BASE=${STAGE_BASE:-${OUT_DIR}/fire-ak3-stage}
MKDTIMG=${MKDTIMG:-${KERNEL_ROOT}/prebuilts/kernel-build-tools/linux-x86/bin/mkdtimg}
MKBOOTIMG=${MKBOOTIMG:-$(command -v mkbootimg || true)}
UNPACK_BOOTIMG=${UNPACK_BOOTIMG:-${KERNEL_ROOT}/system/tools/mkbootimg/unpack_bootimg.py}
CPP=${CPP:-$(command -v cpp || true)}
DTC=${DTC:-$(command -v dtc || true)}
FDTOVERLAY=${FDTOVERLAY:-$(command -v fdtoverlay || true)}
STRIP=${STRIP:-$(command -v llvm-strip || true)}
FIRE66_BOOT_LAYOUT=${FIRE66_BOOT_LAYOUT:-hybrid}
FIRE66_KERNEL_COMPRESSION=${FIRE66_KERNEL_COMPRESSION:-gzip}
FIRE66_BOOT_CMDLINE=${FIRE66_BOOT_CMDLINE:-"bootopt=64S3,32N2,64N2"}
FIRE66_VENDOR_CMDLINE=${FIRE66_VENDOR_CMDLINE:-}
FIRE66_BOOTCONFIG_FILE=${FIRE66_BOOTCONFIG_FILE:-}
FIRE66_BASE_BOOT_IMG=${FIRE66_BASE_BOOT_IMG:-}
FIRE66_BOOT_OS_VERSION=${FIRE66_BOOT_OS_VERSION:-16.0.0}
FIRE66_BOOT_OS_PATCH_LEVEL=${FIRE66_BOOT_OS_PATCH_LEVEL:-2026-06}
FIRE66_BOOT_MAX_BYTES=${FIRE66_BOOT_MAX_BYTES:-134217728}
AK3_FLASH_DTBO=${AK3_FLASH_DTBO:-1}
AK3_FLASH_VENDOR_BOOT=${AK3_FLASH_VENDOR_BOOT:-}
LEGACY_DTBO_COMPAT=${LEGACY_DTBO_COMPAT:-${AK3_TEMPLATE}/dtbo.img}
DTBO_ENTRY_COUNT=${DTBO_ENTRY_COUNT:-1}
LK_DTB_COMPAT=${LK_DTB_COMPAT:-}
SKIP_AK3=${SKIP_AK3:-0}
STRIP_DEBUG_MODULES=${STRIP_DEBUG_MODULES:-1}
FIRST_STAGE_VENDOR_BOOT_MODULES=${FIRST_STAGE_VENDOR_BOOT_MODULES:-"mtk-pmic-wrap.ko mt6358-regulator.ko clk-mt6768.ko clk-mt6768-pg.ko pinctrl-mt6768.ko mtk-mmc.ko"}
VENDOR_BOOT_PAGESIZE=${VENDOR_BOOT_PAGESIZE:-4096}
VENDOR_BOOT_BASE=${VENDOR_BOOT_BASE:-0x40078000}
VENDOR_BOOT_KERNEL_OFFSET=${VENDOR_BOOT_KERNEL_OFFSET:-0x00008000}
VENDOR_BOOT_RAMDISK_OFFSET=${VENDOR_BOOT_RAMDISK_OFFSET:-0x07c08000}
VENDOR_BOOT_TAGS_OFFSET=${VENDOR_BOOT_TAGS_OFFSET:-0x0bc08000}
VENDOR_BOOT_DTB_OFFSET=${VENDOR_BOOT_DTB_OFFSET:-0x0bc08000}
VENDOR_BOOT_MAX_BYTES=${VENDOR_BOOT_MAX_BYTES:-67108864}

case "$FIRE66_BOOT_LAYOUT" in
	compat|hybrid|vendor_boot|boot_v3_vendor_boot|boot_v4_vendor_boot) ;;
	*) die "unsupported FIRE66_BOOT_LAYOUT: $FIRE66_BOOT_LAYOUT" ;;
esac
if [ -z "${FIRE66_BOOTCONFIG+x}" ]; then
	case "$FIRE66_BOOT_LAYOUT" in
	boot_v3_vendor_boot|boot_v4_vendor_boot)
		FIRE66_BOOTCONFIG="androidboot.init_fatal_reboot_target=recovery"
	;;
	*)
		FIRE66_BOOTCONFIG=
	;;
	esac
fi
if [ -z "${FIRE66_BOOT_RAMDISK_VENDOR_MODULES+x}" ]; then
	case "$FIRE66_BOOT_LAYOUT" in
	boot_v3_vendor_boot|boot_v4_vendor_boot)
		FIRE66_BOOT_RAMDISK_VENDOR_MODULES=1
	;;
	*)
		FIRE66_BOOT_RAMDISK_VENDOR_MODULES=0
	;;
	esac
fi
if [ -z "${FIRE66_BOOTCONFIG_IN_BOOT_RAMDISK+x}" ]; then
	case "$FIRE66_BOOT_LAYOUT" in
	boot_v3_vendor_boot|boot_v4_vendor_boot)
		FIRE66_BOOTCONFIG_IN_BOOT_RAMDISK=1
	;;
	*)
		FIRE66_BOOTCONFIG_IN_BOOT_RAMDISK=0
	;;
	esac
fi
if [ -z "${FIRE66_VENDOR_BOOTCONFIG_STATIC+x}" ]; then
	case "$FIRE66_BOOT_LAYOUT" in
	boot_v4_vendor_boot)
		FIRE66_VENDOR_BOOTCONFIG_STATIC=1
	;;
	*)
		FIRE66_VENDOR_BOOTCONFIG_STATIC=0
	;;
	esac
fi
case "$FIRE66_KERNEL_COMPRESSION" in
	gzip) ;;
	*) die "unsupported FIRE66_KERNEL_COMPRESSION: $FIRE66_KERNEL_COMPRESSION" ;;
esac
if { [ "$FIRE66_BOOT_LAYOUT" = boot_v3_vendor_boot ] ||
	[ "$FIRE66_BOOT_LAYOUT" = boot_v4_vendor_boot ]; } &&
	[ "$VENDOR_BOOT_PAGESIZE" != 4096 ]; then
	die "Fire LK v3/v4 vendor_boot parser expects 4096-byte pages; got $VENDOR_BOOT_PAGESIZE"
fi
if [ -z "$AK3_FLASH_VENDOR_BOOT" ]; then
	if [ "$FIRE66_BOOT_LAYOUT" = vendor_boot ] ||
		[ "$FIRE66_BOOT_LAYOUT" = hybrid ] ||
		[ "$FIRE66_BOOT_LAYOUT" = boot_v3_vendor_boot ] ||
		[ "$FIRE66_BOOT_LAYOUT" = boot_v4_vendor_boot ]; then
		AK3_FLASH_VENDOR_BOOT=1
	else
		AK3_FLASH_VENDOR_BOOT=0
	fi
fi

if [ "$SKIP_AK3" != 1 ]; then
	need_tool rsync
	need_tool zip
	need_tool unzip
	[ -d "$AK3_TEMPLATE" ] || die "AK3 template not found: $AK3_TEMPLATE"
fi
need_tool modinfo
need_tool gzip
need_tool cpio
[ -n "$CPP" ] || die "cpp is required"
[ -n "$DTC" ] || die "dtc is required"
[ -n "$MKBOOTIMG" ] || die "mkbootimg is required"
[ "$STRIP_DEBUG_MODULES" != 1 ] || [ -n "$STRIP" ] || die "llvm-strip is required"
[ -x "$MKDTIMG" ] || die "mkdtimg not found: $MKDTIMG"
[ -x "$MKBOOTIMG" ] || die "mkbootimg not executable: $MKBOOTIMG"
[ "$FIRE66_BOOT_LAYOUT" != boot_v3_vendor_boot ] || [ -x "$UNPACK_BOOTIMG" ] || \
	die "unpack_bootimg not executable: $UNPACK_BOOTIMG"
[ "$FIRE66_BOOT_LAYOUT" != boot_v4_vendor_boot ] || [ -x "$UNPACK_BOOTIMG" ] || \
	die "unpack_bootimg not executable: $UNPACK_BOOTIMG"
case "$FIRE66_BOOT_LAYOUT" in
	boot_v3_vendor_boot|boot_v4_vendor_boot)
		[ -f "$FIRE66_BASE_BOOT_IMG" ] ||
			die "FIRE66_BASE_BOOT_IMG is required for $FIRE66_BOOT_LAYOUT layout"
	;;
esac
[ "$STRIP_DEBUG_MODULES" != 1 ] || [ -x "$STRIP" ] || die "llvm-strip not executable: $STRIP"
[ -d "$KERNEL_SRC" ] || die "kernel source not found: $KERNEL_SRC"
[ -d "$DEVICE_MODULES_SRC" ] || die "device module source not found: $DEVICE_MODULES_SRC"

BAZEL_BIN="${OUT_DIR}/bazel/output_user_root/output_base/execroot/__main__/bazel-out/k8-fastbuild/bin"
DEVICE_BIN="${BAZEL_BIN}/${DEVICE_MODULES_DIR}"
KERNEL_VERSION_NUM=${DEVICE_MODULES_DIR##kernel_device_modules-}
VENDOR_MODULE_TAG="${PROJECT}.${KERNEL_VERSION_NUM}.${MODE}"
[ -d "$DEVICE_BIN" ] || die "Bazel output not found: $DEVICE_BIN"

IMAGE=$(
	first_file \
		"${DEVICE_BIN}/${PROJECT}_kernel_aarch64.${MODE}/Image.lz4" \
		"${DEVICE_BIN}/${PROJECT}.${MODE}_kbuild_mixed_tree/Image.lz4" \
		"${DEVICE_BIN}/${PROJECT}_kernel_aarch64.${MODE}/Image" \
		"${DEVICE_BIN}/${PROJECT}.${MODE}_kbuild_mixed_tree/Image" \
	|| true
)
if [ -z "$IMAGE" ]; then
	IMAGE=$(find "$DEVICE_BIN" -path "*/${PROJECT}*${MODE}*/Image.lz4" -type f | head -n1)
fi
[ -n "$IMAGE" ] || die "kernel Image was not found under $DEVICE_BIN"

BOOT_IMAGE_GZ=$(
	first_file \
		"${DEVICE_BIN}/${PROJECT}_kernel_aarch64.${MODE}/Image.gz" \
		"${DEVICE_BIN}/${PROJECT}.${MODE}_kbuild_mixed_tree/Image.gz" \
	|| true
)
BOOT_IMAGE_LZ4=$(
	first_file \
		"${DEVICE_BIN}/${PROJECT}_kernel_aarch64.${MODE}/Image.lz4" \
		"${DEVICE_BIN}/${PROJECT}.${MODE}_kbuild_mixed_tree/Image.lz4" \
	|| true
)

CONFIG_OUT=$(
	for candidate in \
		"${DEVICE_BIN}/${PROJECT}.${MODE}_config/out_dir" \
		"${DEVICE_BIN}/${PROJECT}_kernel_aarch64.${MODE}_config/out_dir"; do
		[ -d "$candidate/include/generated" ] && {
			printf '%s\n' "$candidate"
			break
		}
	done
)
[ -n "$CONFIG_OUT" ] || die "generated config include directory was not found"

FINAL_CONFIG="${CONFIG_OUT}/.config"
[ -f "$FINAL_CONFIG" ] || die "generated kernel .config was not found: $FINAL_CONFIG"
if [ "$PROJECT" = mgk_64_k66 ]; then
	grep -qx 'CONFIG_PROJECT_FIRE=y' "$FINAL_CONFIG" ||
		die "generated .config is not a Fire config: $FINAL_CONFIG"
	grep -qx 'CONFIG_MMC_BLOCK_MINORS=32' "$FINAL_CONFIG" ||
		die "Fire requires CONFIG_MMC_BLOCK_MINORS=32 for high GPT partition minors: $FINAL_CONFIG"
fi

STAMP=${STAMP:-$(date +%Y%m%d-%H%M%S)}
WORK_DIR="${STAGE_BASE}/${STAMP}"
STAGE="${WORK_DIR}/AnyKernel3"
DT_OUT="${WORK_DIR}/dt"
MOD_DST="${WORK_DIR}/modules/vendor/lib/modules"
ZIP_PATH="${DIST_DIR}/MoonLightKernel-fire-GKI-6.6-AK3-${STAMP}.zip"

rm -rf "$WORK_DIR"
mkdir -p "$STAGE" "$DT_OUT" "$MOD_DST" "$DIST_DIR"

case "$FIRE66_KERNEL_COMPRESSION" in
	gzip)
		if [ -z "$BOOT_IMAGE_GZ" ]; then
			BOOT_IMAGE_GZ="${DT_OUT}/Image.gz"
			case "$IMAGE" in
				*.lz4)
					need_tool lz4
					lz4 -dc "$IMAGE" | gzip -n -9 > "$BOOT_IMAGE_GZ"
				;;
				*.gz)
					cp -f "$IMAGE" "$BOOT_IMAGE_GZ"
				;;
				*)
					gzip -n -9 < "$IMAGE" > "$BOOT_IMAGE_GZ"
				;;
			esac
		fi
		gzip -t "$BOOT_IMAGE_GZ" || die "boot kernel gzip validation failed: $BOOT_IMAGE_GZ"
		BOOT_IMAGE_STAGE=$BOOT_IMAGE_GZ
		BOOT_IMAGE_NAME=Image.gz
	;;
	lz4)
		need_tool lz4
		if [ -z "$BOOT_IMAGE_LZ4" ]; then
			BOOT_IMAGE_LZ4="${DT_OUT}/Image.lz4"
			case "$IMAGE" in
				*.lz4)
					cp -f "$IMAGE" "$BOOT_IMAGE_LZ4"
				;;
				*.gz)
					gzip -dc "$IMAGE" | lz4 -l -12 - "$BOOT_IMAGE_LZ4"
				;;
				*)
					lz4 -l -12 "$IMAGE" "$BOOT_IMAGE_LZ4"
				;;
			esac
		fi
		lz4 -q -t "$BOOT_IMAGE_LZ4" || die "boot kernel lz4 validation failed: $BOOT_IMAGE_LZ4"
		BOOT_IMAGE_STAGE=$BOOT_IMAGE_LZ4
		BOOT_IMAGE_NAME=Image.lz4
	;;
esac

echo "==> Building Fire dtb/dtbo"
dt_include_args=(
	-I"${CONFIG_OUT}/include"
	-I"${CONFIG_OUT}/include/generated"
	-I"${KERNEL_SRC}/include"
	-I"${KERNEL_SRC}/scripts/dtc/include-prefixes"
	-I"${DEVICE_MODULES_SRC}/include"
	-I"${DEVICE_MODULES_SRC}/arch/arm64/boot/dts"
	-I"${DEVICE_MODULES_SRC}/arch/arm64/boot/dts/mediatek"
)

build_dtb() {
	local name=$1
	local out_name=$2

	"$CPP" -nostdinc -undef -x assembler-with-cpp -D__DTS__ \
		"${dt_include_args[@]}" \
		"${DEVICE_MODULES_SRC}/arch/arm64/boot/dts/mediatek/${name}.dts" \
		> "${DT_OUT}/${name}.dts.pp"
	"$DTC" -@ -I dts -O dtb \
		-o "${DT_OUT}/${out_name}" \
		-i "${DEVICE_MODULES_SRC}/arch/arm64/boot/dts" \
		-i "${DEVICE_MODULES_SRC}/arch/arm64/boot/dts/mediatek" \
		"${DT_OUT}/${name}.dts.pp" \
		2> "${DT_OUT}/${name}.dtc.log"
}

extract_dts_block_names() {
	local block=$1
	local dts=$2

	awk -v block="$block" '
		$0 ~ "^[[:space:]]*" block "[[:space:]]*\\{" { inside = 1; next }
		inside && /^[[:space:]]*};/ { inside = 0; next }
		inside && /^[[:space:]]*[A-Za-z0-9_]+[[:space:]]*=/ {
			name = $1
			sub(/[[:space:]]*=.*/, "", name)
			print name
		}
	' "$dts"
}

validate_overlay_symbols() {
	local base_dtb=$1
	shift
	local overlay base_dts overlay_dts base_symbols overlay_fixups missing

	base_dts="${DT_OUT}/$(basename "$base_dtb").dts"
	base_symbols="${DT_OUT}/base-symbols.txt"
	"$DTC" -I dtb -O dts -s "$base_dtb" \
		> "$base_dts" \
		2> "${base_dts}.dtc.log"
	extract_dts_block_names "__symbols__" "$base_dts" | sort -u > "$base_symbols"
	[ -s "$base_symbols" ] || die "$(basename "$base_dtb") has no __symbols__; LK overlay fixups would fail"

	for overlay in "$@"; do
		overlay_dts="${DT_OUT}/$(basename "$overlay").dts"
		overlay_fixups="${DT_OUT}/$(basename "$overlay").fixups"
		"$DTC" -I dtb -O dts -s "$overlay" \
			> "$overlay_dts" \
			2> "${overlay_dts}.dtc.log"
		extract_dts_block_names "__fixups__" "$overlay_dts" | sort -u > "$overlay_fixups"
		[ -s "$overlay_fixups" ] || continue

		missing=$(comm -23 "$overlay_fixups" "$base_symbols" | tr '\n' ' ')
		[ -z "$missing" ] || \
			die "$(basename "$overlay") references symbols missing from $(basename "$base_dtb"): ${missing}"
	done
}

extract_first_dtbo_entry() {
	local image=$1
	local out=$2
	local dump offset size

	dump=$("$MKDTIMG" dump "$image" 2>/dev/null || true)
	if printf '%s\n' "$dump" | grep -q 'dt_table_header:'; then
		offset=$(printf '%s\n' "$dump" |
			awk '/dt_table_entry\[0\]/{entry = 1} entry && /dt_offset =/{print $3; exit}')
		size=$(printf '%s\n' "$dump" |
			awk '/dt_table_entry\[0\]/{entry = 1} entry && /dt_size =/{print $3; exit}')
		[ -n "$offset" ] && [ -n "$size" ] || die "cannot parse first dtbo entry from $image"
		dd if="$image" of="$out" bs=1 skip="$offset" count="$size" status=none
	else
		cp -f "$image" "$out"
	fi
}

build_dtb fire fire.dtbo
build_dtb mt6768 mt6768.dtb
compat_overlays=("${DT_OUT}/fire.dtbo")
if [ -f "$LEGACY_DTBO_COMPAT" ]; then
	extract_first_dtbo_entry "$LEGACY_DTBO_COMPAT" "${DT_OUT}/legacy-fire.dtbo"
	compat_overlays+=("${DT_OUT}/legacy-fire.dtbo")
fi
validate_overlay_symbols "${DT_OUT}/mt6768.dtb" "${compat_overlays[@]}"
if [ -n "$LK_DTB_COMPAT" ]; then
	[ -f "$LK_DTB_COMPAT" ] || die "LK_DTB_COMPAT not found: $LK_DTB_COMPAT"
	validate_overlay_symbols "$LK_DTB_COMPAT" "${compat_overlays[@]}"
	if [ -n "$FDTOVERLAY" ]; then
		"$FDTOVERLAY" -i "$LK_DTB_COMPAT" -o "${DT_OUT}/lk-overlay-test.dtb" \
			"${DT_OUT}/fire.dtbo" \
			> "${DT_OUT}/fdtoverlay-lk.log" 2>&1 || \
			die "fire.dtbo cannot be applied to LK_DTB_COMPAT; see ${DT_OUT}/fdtoverlay-lk.log"
	fi
fi

[[ "$DTBO_ENTRY_COUNT" =~ ^[1-9][0-9]*$ ]] || die "DTBO_ENTRY_COUNT must be a positive integer"
dtbo_entries=()
for ((i = 0; i < DTBO_ENTRY_COUNT; i++)); do
	dtbo_entries+=("${DT_OUT}/fire.dtbo")
done
"$MKDTIMG" create "${DT_OUT}/dtbo.img" --page_size=2048 "${dtbo_entries[@]}" \
	> "${DT_OUT}/mkdtimg.log" 2>&1

build_vendor_boot() {
	local vendor_ramdisk=$1
	local header_version=3
	local vendor_boot_size vendor_bootconfig_file
	local -a vendor_bootconfig_args=()

	[ "$FIRE66_BOOT_LAYOUT" != boot_v4_vendor_boot ] || header_version=4
	if [ "$header_version" = 4 ] && [ "$FIRE66_VENDOR_BOOTCONFIG_STATIC" = 1 ]; then
		if [ -n "$FIRE66_BOOTCONFIG_FILE" ]; then
			[ -f "$FIRE66_BOOTCONFIG_FILE" ] ||
				die "FIRE66_BOOTCONFIG_FILE not found: $FIRE66_BOOTCONFIG_FILE"
			vendor_bootconfig_file=$FIRE66_BOOTCONFIG_FILE
		elif [ -n "$FIRE66_BOOTCONFIG" ]; then
			vendor_bootconfig_file="${DT_OUT}/fire66.bootconfig"
			printf '%s\n' "$FIRE66_BOOTCONFIG" > "$vendor_bootconfig_file"
		else
			vendor_bootconfig_file=
		fi
		if [ -n "$vendor_bootconfig_file" ]; then
			vendor_bootconfig_args=(--vendor_bootconfig "$vendor_bootconfig_file")
		fi
	elif [ "$FIRE66_VENDOR_BOOTCONFIG_STATIC" = 1 ]; then
		die "static vendor bootconfig requires boot_v4_vendor_boot layout"
	fi

	echo "==> Building Fire vendor_boot"
	"$MKBOOTIMG" \
		--header_version "$header_version" \
		--pagesize "$VENDOR_BOOT_PAGESIZE" \
		--base "$VENDOR_BOOT_BASE" \
		--kernel_offset "$VENDOR_BOOT_KERNEL_OFFSET" \
		--ramdisk_offset "$VENDOR_BOOT_RAMDISK_OFFSET" \
		--tags_offset "$VENDOR_BOOT_TAGS_OFFSET" \
		--dtb_offset "$VENDOR_BOOT_DTB_OFFSET" \
		--vendor_cmdline "$FIRE66_VENDOR_CMDLINE" \
		--vendor_ramdisk "$vendor_ramdisk" \
		"${vendor_bootconfig_args[@]}" \
		--dtb "${DT_OUT}/mt6768.dtb" \
		--vendor_boot "${DT_OUT}/vendor_boot.img" \
		> "${DT_OUT}/mkbootimg-vendor_boot.log" 2>&1
	vendor_boot_size=$(stat -c %s "${DT_OUT}/vendor_boot.img")
	[ "$vendor_boot_size" -le "$VENDOR_BOOT_MAX_BYTES" ] || \
		die "vendor_boot.img is larger than ${VENDOR_BOOT_MAX_BYTES} bytes"
}

prepare_bootconfig_file() {
	local out=$1

	if [ -n "$FIRE66_BOOTCONFIG_FILE" ]; then
		[ -f "$FIRE66_BOOTCONFIG_FILE" ] ||
			die "FIRE66_BOOTCONFIG_FILE not found: $FIRE66_BOOTCONFIG_FILE"
		cp -f "$FIRE66_BOOTCONFIG_FILE" "$out"
	elif [ -n "$FIRE66_BOOTCONFIG" ]; then
		printf '%s\n' "$FIRE66_BOOTCONFIG" > "$out"
	else
		return 1
	fi
}

append_bootconfig_trailer() {
	local ramdisk=$1
	local bootconfig=$2
	local out=$3

	need_tool python3
	python3 - "$ramdisk" "$bootconfig" "$out" <<'PY'
import pathlib
import struct
import sys

ramdisk = pathlib.Path(sys.argv[1])
bootconfig = pathlib.Path(sys.argv[2])
out = pathlib.Path(sys.argv[3])

payload = bootconfig.read_bytes()
if payload and not payload.endswith(b"\n"):
    payload += b"\n"
padding = b"\0" * ((4 - (len(payload) % 4)) % 4)
checksum = sum(payload) & 0xFFFFFFFF
trailer = payload + padding + struct.pack("<II", len(payload), checksum) + b"#BOOTCONFIG\n"
out.write_bytes(ramdisk.read_bytes() + trailer)
PY
}

build_boot_ramdisk() {
	local base_ramdisk=$1
	local vendor_ramdisk=$2
	local out=$3
	local ramdisk_dir="${WORK_DIR}/boot_ramdisk"
	local staged="${DT_OUT}/boot-ramdisk.base.cpio.gz"
	local bootconfig_file="${DT_OUT}/fire66.bootconfig"

	cp -f "$base_ramdisk" "$staged"

	if [ "$FIRE66_BOOT_RAMDISK_VENDOR_MODULES" = 1 ]; then
		echo "==> Merging Fire first-stage modules into boot ramdisk"
		rm -rf "$ramdisk_dir"
		mkdir -p "$ramdisk_dir"
		(
			cd "$ramdisk_dir"
			gzip -dc "$base_ramdisk" |
				cpio -idmu --no-absolute-filenames \
					2> "${DT_OUT}/boot-ramdisk-base.cpio.log"
			gzip -dc "$vendor_ramdisk" |
				cpio -idmu --no-absolute-filenames \
					2> "${DT_OUT}/boot-ramdisk-vendor.cpio.log"
			find . -print0 | LC_ALL=C sort -z |
				cpio --null -o -H newc --owner root:root \
					2> "${DT_OUT}/boot-ramdisk.cpio.log" |
				gzip -n -9 > "$staged"
		)
	fi

	if [ "$FIRE66_BOOTCONFIG_IN_BOOT_RAMDISK" = 1 ] &&
		prepare_bootconfig_file "$bootconfig_file"; then
		echo "==> Appending Fire bootconfig trailer to boot ramdisk"
		append_bootconfig_trailer "$staged" "$bootconfig_file" "$out"
	else
		cp -f "$staged" "$out"
	fi
}

build_fire_boot_v3_v4() {
	local vendor_ramdisk=$1
	local base_boot_dir="${WORK_DIR}/base_boot"
	local header_version=3
	local boot_ramdisk="${DT_OUT}/boot-ramdisk.cpio.gz"
	local boot_size

	[ -f "$FIRE66_BASE_BOOT_IMG" ] ||
		die "FIRE66_BASE_BOOT_IMG is required for $FIRE66_BOOT_LAYOUT layout"
	[ "$FIRE66_BOOT_LAYOUT" != boot_v4_vendor_boot ] || header_version=4

	echo "==> Building Fire boot.img header v${header_version}"
	rm -rf "$base_boot_dir"
	mkdir -p "$base_boot_dir"
	"$UNPACK_BOOTIMG" --boot_img "$FIRE66_BASE_BOOT_IMG" --out "$base_boot_dir" \
		> "${DT_OUT}/unpack-base-boot.log" 2>&1
	[ -f "${base_boot_dir}/ramdisk" ] ||
		die "base boot image has no ramdisk: $FIRE66_BASE_BOOT_IMG"
	build_boot_ramdisk "${base_boot_dir}/ramdisk" "$vendor_ramdisk" "$boot_ramdisk"

	"$MKBOOTIMG" \
		--header_version "$header_version" \
		--pagesize "$VENDOR_BOOT_PAGESIZE" \
		--kernel "$BOOT_IMAGE_STAGE" \
		--ramdisk "$boot_ramdisk" \
		--cmdline "$FIRE66_BOOT_CMDLINE" \
		--os_version "$FIRE66_BOOT_OS_VERSION" \
		--os_patch_level "$FIRE66_BOOT_OS_PATCH_LEVEL" \
		--output "${DT_OUT}/boot.img" \
		> "${DT_OUT}/mkbootimg-boot-v3.log" 2>&1
	boot_size=$(stat -c %s "${DT_OUT}/boot.img")
	[ "$boot_size" -le "$FIRE66_BOOT_MAX_BYTES" ] ||
		die "boot.img is larger than ${FIRE66_BOOT_MAX_BYTES} bytes"
}

echo "==> Staging AnyKernel3"
if [ "$SKIP_AK3" != 1 ]; then
	rsync -a --delete --exclude='.git' "${AK3_TEMPLATE}/" "${STAGE}/"
	find "$STAGE" -maxdepth 1 -type f \( \
		-name 'Image*' -o \
		-name 'boot.img' -o \
		-name 'dtb' -o \
		-name 'dtb.img' -o \
		-name 'dtbo.img' -o \
		-name 'vendor_boot.img' \
		\) -delete
fi
rm -rf "${STAGE}/modules"
mkdir -p "$MOD_DST"
if [ "$SKIP_AK3" != 1 ]; then
	if [ "$FIRE66_BOOT_LAYOUT" != boot_v3_vendor_boot ] &&
		[ "$FIRE66_BOOT_LAYOUT" != boot_v4_vendor_boot ]; then
		cp -f "$BOOT_IMAGE_STAGE" "${STAGE}/${BOOT_IMAGE_NAME}"
	fi
	if [ "$FIRE66_BOOT_LAYOUT" = compat ] || [ "$FIRE66_BOOT_LAYOUT" = hybrid ]; then
		cp -f "${DT_OUT}/mt6768.dtb" "${STAGE}/dtb"
	fi
	if [ "$AK3_FLASH_DTBO" = 1 ]; then
		cp -f "${DT_OUT}/dtbo.img" "${STAGE}/dtbo.img"
	fi
fi

declare -a module_order=()
declare -A module_source=()

copy_module_once() {
	local ko=$1
	local base

	base=$(basename "$ko")
	[ -n "${module_source[$base]:-}" ] && return 0
	cp -Lf "$ko" "${MOD_DST}/${base}"
	module_source[$base]=$ko
	module_order+=("$base")
}

collect_modules() {
	local mode_name=$1
	shift
	local roots=()
	local root

	for root in "$@"; do
		[ -d "$root" ] && roots+=("$root")
	done
	[ "${#roots[@]}" -gt 0 ] || return 0

	case "$mode_name" in
		staging)
			while IFS= read -r -d '' ko; do
				copy_module_once "$ko"
			done < <(find "${roots[@]}" -path '*/staging/lib/modules/*/extra/*' -type f -name '*.ko' -print0)
		;;
		stripped)
			while IFS= read -r -d '' ko; do
				copy_module_once "$ko"
			done < <(find "${roots[@]}" -type f -name '*.ko' ! -path '*/staging/*' ! -path '*/unstripped/*' -print0)
		;;
		unstripped)
			while IFS= read -r -d '' ko; do
				copy_module_once "$ko"
			done < <(find "${roots[@]}" -path '*/unstripped/*.ko' -type f -print0)
		;;
	esac
}

collect_vendor_modules() {
	local mode_name=$1
	local root="${BAZEL_BIN}/vendor/mediatek/kernel_modules"

	[ -d "$root" ] || return 0
	case "$mode_name" in
		staging)
			while IFS= read -r -d '' ko; do
				copy_module_once "$ko"
			done < <(find "$root" -path "*/${VENDOR_MODULE_TAG}/staging/lib/modules/*/extra/*" -type f -name '*.ko' -print0)
		;;
		stripped)
			while IFS= read -r -d '' ko; do
				copy_module_once "$ko"
			done < <(find "$root" -path "*/${VENDOR_MODULE_TAG}/*" -type f -name '*.ko' ! -path '*/staging/*' ! -path '*/unstripped/*' -print0)
		;;
		unstripped)
			while IFS= read -r -d '' ko; do
				copy_module_once "$ko"
			done < <(find "$root" -path "*/${VENDOR_MODULE_TAG}/unstripped/*.ko" -type f -print0)
		;;
	esac
}

echo "==> Collecting modules"
collect_modules staging "$DEVICE_BIN"
collect_vendor_modules staging
collect_modules stripped "$DEVICE_BIN"
collect_vendor_modules stripped
collect_modules unstripped "$DEVICE_BIN"
collect_vendor_modules unstripped

module_count=$(find "$MOD_DST" -maxdepth 1 -type f -name '*.ko' | wc -l)
[ "$module_count" -gt 0 ] || die "no modules were collected"

add_compat_module() {
	local from=$1
	local to=$2

	if [ ! -f "${MOD_DST}/${to}" ] && [ -f "${MOD_DST}/${from}" ]; then
		cp -pf "${MOD_DST}/${from}" "${MOD_DST}/${to}"
		module_order+=("$to")
		echo "    compat: ${to} -> ${from}"
	fi
}

add_compat_module mtk_fpsgo.ko fpsgo.ko
add_compat_module wlan_drv_gen4m_6768.ko wlan_drv_gen4m.ko

strip_module_debug_symbols() {
	[ "$STRIP_DEBUG_MODULES" = 1 ] || return 0

	echo "==> Stripping module debug symbols"
	while IFS= read -r -d '' ko; do
		"$STRIP" --strip-debug "$ko" ||
			die "stripping debug symbols failed for ${ko}"
	done < <(find "$MOD_DST" -maxdepth 1 -type f -name '*.ko' -print0)
}

generate_module_metadata() {
	local ko base name dep deps dep_file alias softdep
	local -A name_to_file=()

	for ko in "${MOD_DST}"/*.ko; do
		[ -f "$ko" ] || continue
		base=$(basename "$ko")
		name=$(modinfo -F name "$ko" 2>/dev/null | head -n1 || true)
		[ -n "$name" ] || name=${base%.ko}
		name_to_file[$name]=$base
		name_to_file[${name//-/_}]=$base
		name_to_file[${base%.ko}]=$base
	done

	{
		for base in "${module_order[@]}"; do
			[ -f "${MOD_DST}/${base}" ] && printf '%s\n' "$base"
		done
		find "$MOD_DST" -maxdepth 1 -type f -name '*.ko' -printf '%f\n' | sort
	} | awk '!seen[$0]++' > "${MOD_DST}/modules.order"

	: > "${MOD_DST}/modules.dep"
	: > "${MOD_DST}/modules.alias"
	: > "${MOD_DST}/modules.softdep"
	: > "${WORK_DIR}/missing-module-deps.txt"

	while IFS= read -r base; do
		ko="${MOD_DST}/${base}"
		printf '%s:' "$base" >> "${MOD_DST}/modules.dep"
		deps=$(modinfo -F depends "$ko" 2>/dev/null | tr ',' ' ' || true)
		for dep in $deps; do
			dep_file=${name_to_file[$dep]:-${name_to_file[${dep//-/_}]:-}}
			if [ -n "$dep_file" ]; then
				printf ' %s' "$dep_file" >> "${MOD_DST}/modules.dep"
			else
				printf '%s -> %s\n' "$base" "$dep" >> "${WORK_DIR}/missing-module-deps.txt"
			fi
		done
		printf '\n' >> "${MOD_DST}/modules.dep"

		name=$(modinfo -F name "$ko" 2>/dev/null | head -n1 || true)
		[ -n "$name" ] || name=${base%.ko}
		while IFS= read -r alias; do
			[ -n "$alias" ] && printf 'alias %s %s\n' "$alias" "$name" >> "${MOD_DST}/modules.alias"
		done < <(modinfo -F alias "$ko" 2>/dev/null || true)
		while IFS= read -r softdep; do
			[ -n "$softdep" ] && printf 'softdep %s %s\n' "$name" "$softdep" >> "${MOD_DST}/modules.softdep"
		done < <(modinfo -F softdep "$ko" 2>/dev/null || true)
	done < "${MOD_DST}/modules.order"

	: > "${MOD_DST}/modules.builtin"
	: > "${MOD_DST}/modules.builtin.modinfo"
	: > "${MOD_DST}/modules.devname"
	: > "${MOD_DST}/modules.symbols"
	: > "${MOD_DST}/modules.symbols.bin"
}

strip_module_debug_symbols
generate_module_metadata

build_first_stage_vendor_ramdisk() {
	local vendor_ramdisk_dir="${WORK_DIR}/vendor_ramdisk"
	local vendor_module_dir="${vendor_ramdisk_dir}/lib/modules"
	local base dep deps line name
	local -A selected=()
	local -a selected_order=()

	add_first_stage_module() {
		local module=$1
		local module_dep module_deps module_line

		[ -f "${MOD_DST}/${module}" ] ||
			die "first-stage vendor_boot module is missing: ${module}"
		[ -n "${selected[$module]:-}" ] && return 0

		module_line=$(grep -F "${module}:" "${MOD_DST}/modules.dep" | head -n1 || true)
		[ -n "$module_line" ] ||
			die "modules.dep has no entry for first-stage module: ${module}"
		module_deps=${module_line#*:}
		for module_dep in $module_deps; do
			add_first_stage_module "$module_dep"
		done

		selected[$module]=1
		selected_order+=("$module")
	}

	echo "==> Building first-stage vendor ramdisk"
	rm -rf "$vendor_ramdisk_dir"
	mkdir -p "$vendor_module_dir"

	for base in $FIRST_STAGE_VENDOR_BOOT_MODULES; do
		add_first_stage_module "$base"
	done

	: > "${vendor_module_dir}/modules.dep"
	: > "${vendor_module_dir}/modules.alias"
	: > "${vendor_module_dir}/modules.softdep"
	: > "${vendor_module_dir}/modules.order"
	: > "${vendor_module_dir}/modules.load"

	for base in "${selected_order[@]}"; do
		cp -f "${MOD_DST}/${base}" "${vendor_module_dir}/${base}"
		printf '%s\n' "$base" >> "${vendor_module_dir}/modules.order"
		printf '%s\n' "$base" >> "${vendor_module_dir}/modules.load"

		line=$(grep -F "${base}:" "${MOD_DST}/modules.dep" | head -n1 || true)
		printf '%s:' "$base" >> "${vendor_module_dir}/modules.dep"
		deps=${line#*:}
		for dep in $deps; do
			[ -n "${selected[$dep]:-}" ] && printf ' %s' "$dep" >> "${vendor_module_dir}/modules.dep"
		done
		printf '\n' >> "${vendor_module_dir}/modules.dep"

		name=$(modinfo -F name "${MOD_DST}/${base}" 2>/dev/null | head -n1 || true)
		[ -n "$name" ] || name=${base%.ko}
		awk -v module="$name" '$NF == module { print }' "${MOD_DST}/modules.alias" \
			>> "${vendor_module_dir}/modules.alias"
		awk -v module="$name" '$2 == module { print }' "${MOD_DST}/modules.softdep" \
			>> "${vendor_module_dir}/modules.softdep"
	done

	: > "${vendor_module_dir}/modules.builtin"
	: > "${vendor_module_dir}/modules.builtin.modinfo"
	: > "${vendor_module_dir}/modules.devname"
	: > "${vendor_module_dir}/modules.symbols"
	: > "${vendor_module_dir}/modules.symbols.bin"

	(
		cd "$vendor_ramdisk_dir"
		find . -print0 | LC_ALL=C sort -z |
			cpio --null -o -H newc --owner root:root 2> "${DT_OUT}/vendor-ramdisk.cpio.log" |
			gzip -n -9 > "${DT_OUT}/vendor-ramdisk.cpio.gz"
	)

	echo "First-stage vendor_boot modules: ${#selected_order[@]}"
}

build_first_stage_vendor_ramdisk
build_vendor_boot "${DT_OUT}/vendor-ramdisk.cpio.gz"
case "$FIRE66_BOOT_LAYOUT" in
	boot_v3_vendor_boot|boot_v4_vendor_boot)
		build_fire_boot_v3_v4 "${DT_OUT}/vendor-ramdisk.cpio.gz"
	;;
esac
if [ "$SKIP_AK3" != 1 ] && [ "$AK3_FLASH_VENDOR_BOOT" = 1 ]; then
	cp -f "${DT_OUT}/vendor_boot.img" "${STAGE}/vendor_boot.img"
fi
if [ "$SKIP_AK3" != 1 ]; then
	case "$FIRE66_BOOT_LAYOUT" in
		boot_v3_vendor_boot|boot_v4_vendor_boot)
			cp -f "${DT_OUT}/boot.img" "${STAGE}/boot.img"
		;;
	esac
fi

echo "==> Exporting ROM artifacts"
rm -rf "$ROM_ARTIFACTS_DIR"
mkdir -p "${ROM_ARTIFACTS_DIR}/modules/vendor/lib/modules"
cp -f "$BOOT_IMAGE_STAGE" "${ROM_ARTIFACTS_DIR}/${BOOT_IMAGE_NAME}"
[ "$BOOT_IMAGE_NAME" = Image.gz ] || [ -z "$BOOT_IMAGE_GZ" ] || \
	cp -f "$BOOT_IMAGE_GZ" "${ROM_ARTIFACTS_DIR}/Image.gz"
[ "$BOOT_IMAGE_NAME" = Image.lz4 ] || [ -z "$BOOT_IMAGE_LZ4" ] || \
	cp -f "$BOOT_IMAGE_LZ4" "${ROM_ARTIFACTS_DIR}/Image.lz4"
case "$(basename "$IMAGE")" in
	Image)
		cp -f "$IMAGE" "${ROM_ARTIFACTS_DIR}/$(basename "$IMAGE")"
	;;
esac
cp -f "${DT_OUT}/mt6768.dtb" "${ROM_ARTIFACTS_DIR}/dtb"
cp -f "${DT_OUT}/dtbo.img" "${ROM_ARTIFACTS_DIR}/dtbo.img"
cp -f "${DT_OUT}/vendor_boot.img" "${ROM_ARTIFACTS_DIR}/vendor_boot.img"
[ ! -f "${DT_OUT}/fire66.bootconfig" ] || \
	cp -f "${DT_OUT}/fire66.bootconfig" "${ROM_ARTIFACTS_DIR}/vendor_boot.bootconfig"
[ ! -f "${DT_OUT}/boot.img" ] || cp -f "${DT_OUT}/boot.img" "${ROM_ARTIFACTS_DIR}/boot.img"
cp -a "${MOD_DST}/." "${ROM_ARTIFACTS_DIR}/modules/vendor/lib/modules/"

missing_required=()
for required in trace_mmstat.ko fpsgo.ko wmt_drv.ko wmt_chrdev_wifi.ko wlan_drv_gen4m.ko; do
	[ -f "${MOD_DST}/${required}" ] || missing_required+=("$required")
done
if [ "${#missing_required[@]}" -gt 0 ]; then
	printf 'error: missing required vendor modules:' >&2
	printf ' %s' "${missing_required[@]}" >&2
	printf '\n' >&2
	exit 1
fi

missing_dep_count=$(grep -c . "${WORK_DIR}/missing-module-deps.txt" || true)
if [ "$missing_dep_count" -gt 0 ]; then
	echo "warning: ${missing_dep_count} module dependencies were not found in the flat package" >&2
	echo "warning: see ${WORK_DIR}/missing-module-deps.txt" >&2
fi

if [ "$SKIP_AK3" != 1 ]; then
	cat > "${STAGE}/anykernel.sh" <<'AK3_EOF'
### AnyKernel3 Ramdisk Mod Script
## Kernel package generated by scripts/package_fire_ak3.sh

properties() { '
kernel.string=MoonLightKernel fire GKI 6.6
do.devicecheck=1
do.modules=0
do.systemless=0
do.systemmount=0
do.cleanup=1
do.cleanuponabort=0
do.unmount=0
device.name1=fire
device.name2=heat
supported.versions=
supported.patchlevels=
supported.vendorpatchlevels=
'; }

boot_attributes() {
set_perm_recursive 0 0 755 644 $RAMDISK/*;
set_perm_recursive 0 0 750 750 $RAMDISK/init* $RAMDISK/sbin;
}

BLOCK=boot;
IS_SLOT_DEVICE=1;
RAMDISK_COMPRESSION=auto;
PATCH_VBMETA_FLAG=auto;

FIRE66_BOOT_LAYOUT="__FIRE66_BOOT_LAYOUT__";

# Keep AnyKernel3 from auto-splitting staged DTB/vendor ramdisk trees. This
# package either repacks the legacy boot image or flashes an already matched
# boot/vendor_boot/dtbo image set.
touch vendor_v3_setup;
touch init_v4_setup;

. tools/ak3-core.sh;

FIRE66_BOOT_CMDLINE="__FIRE66_BOOT_CMDLINE__";

normalize_fire66_boot_cmdline() {
	cmd="$FIRE66_BOOT_CMDLINE";
	wrote_cmd=0;
	[ -n "$cmd" ] || abort "Fire 6.6 boot cmdline is empty. Aborting...";

	old_cmd="";
	for cmdfile in "$SPLITIMG/cmdline" "$SPLITIMG/cmdline.txt" "$SPLITIMG/boot.img-cmdline"; do
		if [ -f "$cmdfile" ]; then
			[ -n "$old_cmd" ] || old_cmd=$(cat "$cmdfile" 2>/dev/null);
			printf '%s\n' "$cmd" > "$cmdfile" ||
				abort "Writing normalized boot cmdline failed. Aborting...";
			wrote_cmd=1;
		fi;
	done;

	if [ -f "$SPLITIMG/header" ]; then
		[ -n "$old_cmd" ] ||
		old_cmd=$(grep '^cmdline=' "$SPLITIMG/header" 2>/dev/null | head -n 1 | cut -d= -f2-);
		tmp="$SPLITIMG/header.fire66";
		awk -v c="$cmd" '
			BEGIN { done = 0 }
			/^cmdline=/ {
				print "cmdline=" c;
				done = 1;
				next;
			}
			{ print }
			END {
				if (!done)
					print "cmdline=" c;
			}
		' "$SPLITIMG/header" > "$tmp" &&
			mv "$tmp" "$SPLITIMG/header" ||
			abort "Writing normalized boot header failed. Aborting...";
		wrote_cmd=1;
	fi;

	if [ "$wrote_cmd" != 1 ]; then
		printf '%s\n' "$cmd" > "$SPLITIMG/cmdline" ||
			abort "Writing normalized boot cmdline failed. Aborting...";
		printf '%s\n' "$cmd" > "$SPLITIMG/cmdline.txt" ||
			abort "Writing normalized boot cmdline failed. Aborting...";
	fi;

	old_len=$(printf '%s' "$old_cmd" | wc -c);
	new_len=$(printf '%s' "$cmd" | wc -c);
	ui_print " " "Boot cmdline normalized (${old_len}->${new_len} bytes).";
}

patch_ramdisk_module_loaders() {
	mtk_rc="$RAMDISK/init.mt6768.rc";

	if [ -f "$mtk_rc" ]; then
		sed -i \
			's|^[[:space:]]*insmod /vendor/lib/modules/fpsgo\.ko|    exec_background u:r:vendor_modprobe:s0 -- /vendor/bin/modprobe -a -d /vendor/lib/modules fpsgo.ko|' \
			"$mtk_rc" || abort "Patching ramdisk fpsgo module loader failed. Aborting...";
	fi;
}

cd "$AKHOME";
case "$FIRE66_BOOT_LAYOUT" in
boot_v3_vendor_boot|boot_v4_vendor_boot)
	[ -f boot.img ] ||
		abort "$FIRE66_BOOT_LAYOUT layout selected but boot.img is missing. Aborting...";
	[ -f vendor_boot.img ] ||
		abort "$FIRE66_BOOT_LAYOUT layout selected but vendor_boot.img is missing. Aborting...";
	[ -f dtbo.img ] ||
		abort "$FIRE66_BOOT_LAYOUT layout selected but dtbo.img is missing. Aborting...";
	ui_print " " "Fire 6.6 layout: flashing matched boot/vendor_boot/dtbo images.";
	flash_generic boot;
	flash_generic vendor_boot;
	flash_generic dtbo;
	exit 0;
;;
esac;

dump_boot;
cd "$AKHOME";
normalize_fire66_boot_cmdline;
patch_ramdisk_module_loaders;
cd "$AKHOME";
write_boot;
AK3_EOF
	sed -i "s|__FIRE66_BOOT_LAYOUT__|${FIRE66_BOOT_LAYOUT}|g" "${STAGE}/anykernel.sh"
	sed -i "s|__FIRE66_BOOT_CMDLINE__|${FIRE66_BOOT_CMDLINE}|g" "${STAGE}/anykernel.sh"
	chmod 755 "${STAGE}/anykernel.sh"

	echo "==> Creating AK3 zip"
	rm -f "$ZIP_PATH" "${ZIP_PATH}.sha256"
	(
		cd "$STAGE"
		zip -r9 "$ZIP_PATH" . -x '*.git*'
	)
	unzip -tq "$ZIP_PATH"
	sha256sum "$ZIP_PATH" > "${ZIP_PATH}.sha256"

	echo "AK3: $ZIP_PATH"
	echo "SHA256: ${ZIP_PATH}.sha256"
fi
echo "ROM artifacts: $ROM_ARTIFACTS_DIR"
echo "Modules: $(find "$MOD_DST" -maxdepth 1 -type f -name '*.ko' | wc -l)"
echo "DTC logs: ${DT_OUT}"
