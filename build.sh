# SPDX-License-Identifier: GPL-2.0
#!/bin/bash

set -e

DEVICE_MODULES_DIR=$(basename $(dirname $0))
source "${DEVICE_MODULES_DIR}/kernel/kleaf/_setup_env.sh"

if [[ -z "${DEFCONFIG_OVERLAYS:-}" && "${PROJECT:-}" == "mgk_64_k66" ]]
then
  DEFCONFIG_OVERLAYS="mt6768_overlay.config fire_overlay.config"
fi
export DEFCONFIG_OVERLAYS

CCACHE_EXEC=${CCACHE_EXEC:-$(command -v ccache || true)}
CCACHE_DIR=${CCACHE_DIR:-${HOME}/.cache/ccache/inferno-kernel}
CCACHE_MAXSIZE=${CCACHE_MAXSIZE:-35G}
CCACHE_CPP2=${CCACHE_CPP2:-yes}
CCACHE_NOHASHDIR=${CCACHE_NOHASHDIR:-true}
KLEAF_CCACHE_WRAPPER_DIR=${KLEAF_CCACHE_WRAPPER_DIR:-${CCACHE_DIR}/wrappers}

KLEAF_CCACHE_ARGS=()
if [[ -n "${CCACHE_EXEC}" ]]
then
  CCACHE_BASE_DIR=$(dirname "${CCACHE_DIR}")
  mkdir -p "${CCACHE_DIR}"
  mkdir -p "${KLEAF_CCACHE_WRAPPER_DIR}"
  ln -sf "${CCACHE_EXEC}" "${KLEAF_CCACHE_WRAPPER_DIR}/clang"
  ln -sf "${CCACHE_EXEC}" "${KLEAF_CCACHE_WRAPPER_DIR}/clang++"
  ln -sf "${CCACHE_EXEC}" "${KLEAF_CCACHE_WRAPPER_DIR}/gcc"
  ln -sf "${CCACHE_EXEC}" "${KLEAF_CCACHE_WRAPPER_DIR}/g++"
  "${CCACHE_EXEC}" -M "${CCACHE_MAXSIZE}" >/dev/null
  export CCACHE_EXEC CCACHE_DIR CCACHE_MAXSIZE CCACHE_CPP2 CCACHE_NOHASHDIR KLEAF_CCACHE_WRAPPER_DIR
  KLEAF_CCACHE_ARGS=(
	"--action_env=CCACHE_EXEC=${CCACHE_EXEC}"
	"--action_env=CCACHE_DIR=${CCACHE_DIR}"
	"--action_env=CCACHE_MAXSIZE=${CCACHE_MAXSIZE}"
	"--action_env=CCACHE_CPP2=${CCACHE_CPP2}"
	"--action_env=CCACHE_NOHASHDIR=${CCACHE_NOHASHDIR}"
	"--action_env=KLEAF_CCACHE_WRAPPER_DIR=${KLEAF_CCACHE_WRAPPER_DIR}"
	"--host_action_env=CCACHE_EXEC=${CCACHE_EXEC}"
	"--host_action_env=CCACHE_DIR=${CCACHE_DIR}"
	"--host_action_env=CCACHE_MAXSIZE=${CCACHE_MAXSIZE}"
	"--host_action_env=CCACHE_CPP2=${CCACHE_CPP2}"
	"--host_action_env=CCACHE_NOHASHDIR=${CCACHE_NOHASHDIR}"
	"--host_action_env=KLEAF_CCACHE_WRAPPER_DIR=${KLEAF_CCACHE_WRAPPER_DIR}"
	"--define=CCACHE_EXEC=${CCACHE_EXEC}"
	"--define=CCACHE_DIR=${CCACHE_DIR}"
	"--define=CCACHE_MAXSIZE=${CCACHE_MAXSIZE}"
	"--define=CCACHE_CPP2=${CCACHE_CPP2}"
	"--define=CCACHE_NOHASHDIR=${CCACHE_NOHASHDIR}"
	"--define=KLEAF_CCACHE_WRAPPER_DIR=${KLEAF_CCACHE_WRAPPER_DIR}"
	"--sandbox_writable_path=${CCACHE_BASE_DIR}"
	"--sandbox_writable_path=${CCACHE_DIR}"
  )
fi

# run kleaf commands or legacy build.sh
result=$(echo ${KLEAF_SUPPORTED_PROJECTS} | grep -wo ${PROJECT}) || result=""
if [[ ${result} != "" ]]
then # run kleaf commands

build_scope=internal
if [ ! -d "vendor/mediatek/tests/kernel" ]
then
  build_scope=customer
fi

if [ -z ${TARGET} ]
then
  TARGET=${build_scope}_modules_install
  KLEAF_BUILD_TARGET=//${DEVICE_MODULES_DIR}:${PROJECT}_${TARGET}.${MODE}
else
  KLEAF_BUILD_TARGET=${TARGET}.${MODE}
fi
KLEAF_DIST_TARGET=//${DEVICE_MODULES_DIR}:${PROJECT}_${build_scope}_dist.${MODE}

KLEAF_OUT=("--output_user_root=${OUT_DIR} --output_base=${OUT_DIR}/bazel/output_user_root/output_base")
KLEAF_ARGS=("${DEBUG_ARGS} ${SANDBOX_ARGS} ${KLEAF_CCACHE_ARGS[*]} \
	--experimental_writable_outputs \
	--noenable_bzlmod \
	--//build/bazel_mgk_rules:kernel_version=${KERNEL_VERSION_NUM}")

set -x
(
  tools/bazel ${KLEAF_OUT} build ${KLEAF_ARGS} ${KLEAF_BUILD_TARGET}
  tools/bazel ${KLEAF_OUT} run ${KLEAF_ARGS} \
	--nokmi_symbol_list_violations_check ${KLEAF_DIST_TARGET} -- --dist_dir=${OUT_DIR}/dist
)
set +x

if [[ ${MODE} == "user" && ${KLEAF_GKI_CHECKER} != "no" ]]
then
  KLEAF_GKI_CHECKER_COMMANDS=("${KLEAF_GKI_CHECKER_COMMANDS} \
	  -m ${OUT_DIR}/dist/${DEVICE_MODULES_DIR}/${PROJECT}_kernel_aarch64.${MODE}/vmlinux")
  set -x
  (
    ${KLEAF_GKI_CHECKER_COMMANDS} -o file
    ${KLEAF_GKI_CHECKER_COMMANDS} -o config
    ${KLEAF_GKI_CHECKER_COMMANDS} -o symbol
  )
  set +x
fi

else # run legacy build.sh
set -x
(
  OUT_DIR=${OUT_DIR} python ${DEVICE_MODULES_DIR}/scripts/gen_build_config.py -p ${PROJECT} \
	-o ${BUILD_CONFIG}.legacy -m ${MODE} --kernel-defconfig-overlays "${DEFCONFIG_OVERLAYS}"
  OUT_DIR=${OUT_DIR} BUILD_CONFIG=${BUILD_CONFIG}.legacy CC_WRAPPER=${CC_WRAPPER} build/build.sh
)
set +x
fi
