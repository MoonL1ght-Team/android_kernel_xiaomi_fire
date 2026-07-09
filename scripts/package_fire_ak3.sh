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
CPP=${CPP:-$(command -v cpp || true)}
DTC=${DTC:-$(command -v dtc || true)}
FDTOVERLAY=${FDTOVERLAY:-$(command -v fdtoverlay || true)}
AK3_FLASH_DTBO=${AK3_FLASH_DTBO:-0}
AK3_FLASH_VENDOR_BOOT=${AK3_FLASH_VENDOR_BOOT:-0}
LEGACY_DTBO_COMPAT=${LEGACY_DTBO_COMPAT:-${AK3_TEMPLATE}/dtbo.img}
DTBO_ENTRY_COUNT=${DTBO_ENTRY_COUNT:-3}
LK_DTB_COMPAT=${LK_DTB_COMPAT:-}
SKIP_AK3=${SKIP_AK3:-0}
VENDOR_BOOT_PAGESIZE=${VENDOR_BOOT_PAGESIZE:-2048}
VENDOR_BOOT_BASE=${VENDOR_BOOT_BASE:-0x40000000}
VENDOR_BOOT_DTB_OFFSET=${VENDOR_BOOT_DTB_OFFSET:-0x0bc80000}
VENDOR_BOOT_MAX_BYTES=${VENDOR_BOOT_MAX_BYTES:-67108864}

if [ "$SKIP_AK3" != 1 ]; then
	need_tool rsync
	need_tool zip
	need_tool unzip
	[ -d "$AK3_TEMPLATE" ] || die "AK3 template not found: $AK3_TEMPLATE"
fi
need_tool modinfo
[ -n "$CPP" ] || die "cpp is required"
[ -n "$DTC" ] || die "dtc is required"
[ -n "$MKBOOTIMG" ] || die "mkbootimg is required"
[ -x "$MKDTIMG" ] || die "mkdtimg not found: $MKDTIMG"
[ -x "$MKBOOTIMG" ] || die "mkbootimg not executable: $MKBOOTIMG"
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
[ -n "$IMAGE" ] || die "Image.lz4 was not found under $DEVICE_BIN"

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

STAMP=${STAMP:-$(date +%Y%m%d-%H%M%S)}
WORK_DIR="${STAGE_BASE}/${STAMP}"
STAGE="${WORK_DIR}/AnyKernel3"
DT_OUT="${WORK_DIR}/dt"
MOD_DST="${STAGE}/modules/system/vendor/lib/modules"
ZIP_PATH="${DIST_DIR}/MoonLightKernel-fire-GKI-6.6-AK3-${STAMP}.zip"

rm -rf "$WORK_DIR"
mkdir -p "$STAGE" "$DT_OUT" "$MOD_DST" "$DIST_DIR"

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

echo "==> Building Fire vendor_boot"
: > "${DT_OUT}/empty-vendor-ramdisk"
"$MKBOOTIMG" \
	--header_version 3 \
	--pagesize "$VENDOR_BOOT_PAGESIZE" \
	--base "$VENDOR_BOOT_BASE" \
	--dtb_offset "$VENDOR_BOOT_DTB_OFFSET" \
	--vendor_ramdisk "${DT_OUT}/empty-vendor-ramdisk" \
	--dtb "${DT_OUT}/mt6768.dtb" \
	--vendor_boot "${DT_OUT}/vendor_boot.img" \
	> "${DT_OUT}/mkbootimg-vendor_boot.log" 2>&1
vendor_boot_size=$(stat -c %s "${DT_OUT}/vendor_boot.img")
[ "$vendor_boot_size" -le "$VENDOR_BOOT_MAX_BYTES" ] || \
	die "vendor_boot.img is larger than ${VENDOR_BOOT_MAX_BYTES} bytes"

echo "==> Staging AnyKernel3"
if [ "$SKIP_AK3" != 1 ]; then
	rsync -a --delete --exclude='.git' "${AK3_TEMPLATE}/" "${STAGE}/"
	find "$STAGE" -maxdepth 1 -type f \( \
		-name 'Image*' -o \
		-name 'dtb' -o \
		-name 'dtb.img' -o \
		-name 'dtbo.img' -o \
		-name 'vendor_boot.img' \
		\) -delete
fi
rm -rf "${STAGE}/modules"
mkdir -p "$MOD_DST"
if [ "$SKIP_AK3" != 1 ]; then
	cp -f "$IMAGE" "${STAGE}/Image.lz4"
	cp -f "${DT_OUT}/mt6768.dtb" "${STAGE}/dtb"
	if [ "$AK3_FLASH_DTBO" = 1 ]; then
		cp -f "${DT_OUT}/dtbo.img" "${STAGE}/dtbo.img"
	fi
	if [ "$AK3_FLASH_VENDOR_BOOT" = 1 ]; then
		cp -f "${DT_OUT}/vendor_boot.img" "${STAGE}/vendor_boot.img"
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

generate_module_metadata

echo "==> Exporting ROM artifacts"
rm -rf "$ROM_ARTIFACTS_DIR"
mkdir -p "${ROM_ARTIFACTS_DIR}/modules/vendor/lib/modules"
cp -f "$IMAGE" "${ROM_ARTIFACTS_DIR}/Image.lz4"
cp -f "${DT_OUT}/mt6768.dtb" "${ROM_ARTIFACTS_DIR}/dtb"
cp -f "${DT_OUT}/dtbo.img" "${ROM_ARTIFACTS_DIR}/dtbo.img"
cp -f "${DT_OUT}/vendor_boot.img" "${ROM_ARTIFACTS_DIR}/vendor_boot.img"
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
do.modules=1
do.systemless=1
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

# The current Fire boot chain uses boot header v2 with dtb in boot.img. Keep
# AnyKernel3 from moving the dtb to vendor_boot automatically; generated
# dtbo/vendor_boot images are staged only when package_fire_ak3.sh is invoked
# with AK3_FLASH_DTBO=1 and/or AK3_FLASH_VENDOR_BOOT=1.
touch vendor_v3_setup;

. tools/ak3-core.sh;

ak_is_mounted() {
	mount | grep -q " $1 ";
}

ak_partition_exists() {
	part=$1;
	for path in /dev/block/mapper /dev/block/by-name /dev/block/bootdevice/by-name; do
		if [ -e "$path/${part}${SLOT}" ] || [ -e "$path/${part}" ]; then
			return 0;
		fi;
	done;
	return 1;
}

ak_has_space_for_copy() {
	copy_src=$1;
	copy_mount=$2;
	copy_existing=$3;
	copy_required=$(du -sk "$copy_src" 2>/dev/null | awk '{print $1}');
	copy_free=$(df -Pk "$copy_mount" 2>/dev/null | awk 'NR == 2 {print $4}');
	copy_old=0;
	copy_reserve=32768;

	[ -n "$copy_required" ] || return 1;
	[ -n "$copy_free" ] || return 1;
	if [ -d "$copy_existing" ]; then
		copy_old=$(du -sk "$copy_existing" 2>/dev/null | awk '{print $1}');
		[ -n "$copy_old" ] || copy_old=0;
	fi;

	[ $((copy_free + copy_old)) -ge $((copy_required + copy_reserve)) ];
}

assert_boot_dtbo_pair() {
	if [ -f dtbo.img ] && [ ! -f dtb ]; then
		abort "dtbo.img is present without matching boot dtb. Aborting to avoid LK overlay crash...";
	fi;
}

install_vendor_modules() {
	src=$AKHOME/modules/system/vendor/lib/modules;
	dst=/vendor/lib/modules;

	[ -d "$src" ] || abort "Vendor modules payload is missing. Aborting...";
	ui_print " " "Installing vendor modules...";

	if [ -d /vendor ]; then
		if ! ak_is_mounted /vendor; then
			mount /vendor 2>/dev/null || true;
		fi;
		if ak_is_mounted /vendor; then
			mount -o rw,remount -t auto /vendor 2>/dev/null || true;
			if mkdir -p "$dst" 2>/dev/null && touch "$dst/.ak3-write-test" 2>/dev/null; then
				rm -f "$dst/.ak3-write-test";
				if ! ak_has_space_for_copy "$src" /vendor "$dst"; then
					mount -o ro,remount -t auto /vendor 2>/dev/null || true;
					ui_print " " "Vendor has too little free space for modules; using systemless overlay.";
					install_systemless_vendor_modules "$src";
					return $?;
				fi;
				rm -rf "$dst";
				mkdir -p "$dst";
				cp -rLf "$src/." "$dst/" || abort "Copying vendor modules failed. Aborting...";
				chown -R 0:0 "$dst" 2>/dev/null || chown -R 0.0 "$dst";
				find "$dst" -type d -exec chmod 755 {} +;
				find "$dst" -type f -exec chmod 644 {} +;
				/system/bin/chcon -hR u:object_r:vendor_file:s0 "$dst" 2>/dev/null || true;
				mount -o ro,remount -t auto /vendor 2>/dev/null || true;
				ui_print " " "Vendor modules installed directly.";
				return 0;
			fi;
			mount -o ro,remount -t auto /vendor 2>/dev/null || true;
		fi;
	fi;

	install_systemless_vendor_modules "$src";
}

install_systemless_vendor_modules() {
	src=$1;
	module=/data/adb/modules/fire66-kmods;
	dst=$module/system/vendor/lib/modules;

	if ! ak_is_mounted /data; then
		mount /data 2>/dev/null || true;
	fi;
	ak_is_mounted /data || abort "Vendor is read-only and /data is not mounted for systemless modules. Aborting...";
	[ -d /data/adb/modules ] || abort "Vendor is read-only and Magisk/KernelSU module path was not found. Aborting...";
	ak_has_space_for_copy "$src" /data "$module" || abort "/data has too little free space for systemless vendor modules. Aborting...";

	rm -rf "$module";
	mkdir -p "$dst";
	cp -rLf "$src/." "$dst/" || abort "Copying systemless vendor modules failed. Aborting...";
	cat > "$module/module.prop" <<'MODULE_EOF'
id=fire66-kmods
name=Fire 6.6 kernel vendor modules
version=6.6
versionCode=66
author=MoonLightKernel
description=Vendor module overlay for the Fire/Heat 6.6 GKI kernel package
MODULE_EOF
	touch "$module/update";
	rm -f "$module/remove" "$module/disable";
	chown -R 0:0 "$module" 2>/dev/null || chown -R 0.0 "$module";
	find "$module" -type d -exec chmod 755 {} +;
	find "$module" -type f -exec chmod 644 {} +;
	ui_print " " "Vendor modules installed as Magisk/KernelSU overlay.";
}

dump_boot;
assert_boot_dtbo_pair;
install_vendor_modules;
ak_partition_exists vendor_boot || rm -f vendor_boot.img;
write_boot;
AK3_EOF
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
