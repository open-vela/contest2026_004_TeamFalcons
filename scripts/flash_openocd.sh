#!/usr/bin/env bash
#
# flash_openocd.sh - 用 OpenOCD 直烧 STM32H750B-DK 片内 Flash
#
# 用法:
#   ./scripts/flash_openocd.sh               # 先编译，再烧录 nuttx/nuttx.hex
#   ./scripts/flash_openocd.sh --no-build    # 只烧录（不编译，快速迭代用）
#   ./scripts/flash_openocd.sh --hex <路径>  # 烧指定的 hex
#   ./scripts/flash_openocd.sh --openocd <路径>  # 指定 OpenOCD 可执行文件
#
# 环境变量（可选）:
#   OPENVELA_ROOT    openvela 根目录（默认从脚本位置向上推导）
#   OPENOCD_BIN      OpenOCD 可执行文件完整路径
#   WIN_TEMP         Windows 侧临时目录（WSL 模式复制 hex 用）
#
# 特性:
#   - 自动选择: Linux 侧能看到 ST-LINK(/dev/bus/usb) 用 Linux OpenOCD，
#     否则回落到 Windows xPack OpenOCD（WSL 互操作，推荐本机）
#   - 防呆: 拒绝烧录 QSPI 镜像(0x90000000)，只允许片内 0x08000000
#   - 烧录后自动 verify + reset
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
OPENVELA_ROOT="${OPENVELA_ROOT:-$(cd "${REPO_DIR}/.." && pwd)}"
NUTTX_DIR="${OPENVELA_ROOT}/nuttx"
HEX_PATH="${HEX_PATH:-${NUTTX_DIR}/nuttx.hex}"

BUILD=1

usage()
{
  sed -n '2,21p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
  case "$1" in
    --no-build)
      BUILD=0
      ;;
    --hex)
      [ $# -ge 2 ] || { echo "error: --hex 需要一个参数" >&2; exit 2; }
      HEX_PATH="$2"
      shift
      ;;
    --openocd)
      [ $# -ge 2 ] || { echo "error: --openocd 需要一个参数" >&2; exit 2; }
      OPENOCD_BIN="$2"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "error: 未知参数: $1" >&2
      usage
      exit 2
      ;;
  esac
  shift
done

# ---------------------------------------------------------------- 编译
build_firmware()
{
  export PATH="${OPENVELA_ROOT}/prebuilts/tools/python/bin:${OPENVELA_ROOT}/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:${OPENVELA_ROOT}/prebuilts/build-tools/linux-x86_64/bin:${PATH}"
  export PYTHONPATH="${OPENVELA_ROOT}/prebuilts/tools/python/dist-packages/kconfiglib:${OPENVELA_ROOT}/prebuilts/tools/python/dist-packages:${PYTHONPATH:-}"
  echo "==> 编译固件 (make -C ${NUTTX_DIR})"
  make -C "${NUTTX_DIR}" -j"$(nproc)"
}

if [ "${BUILD}" = "1" ]; then
  [ -d "${NUTTX_DIR}" ] || { echo "error: 找不到 nuttx 目录: ${NUTTX_DIR}" >&2; exit 1; }
  build_firmware
fi

# ------------------------------------------------------------ 产物检查
[ -f "${HEX_PATH}" ] || { echo "error: 固件不存在: ${HEX_PATH}" >&2; exit 1; }

if grep -q ":020000049000" "${HEX_PATH}"; then
  echo "error: ${HEX_PATH} 是 QSPI 镜像(基址 0x90000000)，本脚本只烧片内 Flash" >&2
  exit 1
fi

if ! grep -q ":020000040800" "${HEX_PATH}"; then
  echo "error: ${HEX_PATH} 不是片内 Flash 镜像(未找到 0x08000000 段基址)" >&2
  exit 1
fi

BIN_PATH="${HEX_PATH%.hex}.bin"
if [ -f "${BIN_PATH}" ]; then
  BIN_SIZE="$(stat -c %s "${BIN_PATH}")"
  if [ "${BIN_SIZE}" -gt 131072 ]; then
    echo "warning: 镜像 ${BIN_SIZE}B 超过片内 128KiB，OpenOCD 可能烧不进去" >&2
  else
    echo "==> 镜像 ${BIN_SIZE}B (≤128KiB) 校验通过，基址 0x08000000"
  fi
fi

# ------------------------------------------------------- 选择 OpenOCD
OPENOCD_KIND=""

if [ -n "${OPENOCD_BIN:-}" ] && [ -x "${OPENOCD_BIN}" ]; then
  if [[ "${OPENOCD_BIN}" == *.exe ]]; then OPENOCD_KIND="windows"; else OPENOCD_KIND="linux"; fi
elif ls /dev/bus/usb >/dev/null 2>&1 && command -v openocd >/dev/null 2>&1; then
  OPENOCD_BIN="$(command -v openocd)"
  OPENOCD_KIND="linux"
else
  # WSL2 下 ST-LINK 由 Windows 侧访问，回落到 xPack OpenOCD
  for cand in \
    "/mnt/d/Develop/xpack-openocd-0.12.0-7/bin/openocd.exe" \
    "/mnt/c/Develop/xpack-openocd-0.12.0-7/bin/openocd.exe" \
    "${HOME}/xpack-openocd-*/bin/openocd.exe"; do
    if compgen -G "${cand}" >/dev/null 2>&1; then
      OPENOCD_BIN="$(compgen -G "${cand}" | head -1)"
      OPENOCD_KIND="windows"
      break
    fi
  done
fi

if [ -z "${OPENOCD_KIND}" ]; then
  echo "error: 找不到 OpenOCD。请安装后重试，或用 --openocd 指定路径。" >&2
  exit 1
fi
echo "==> 使用 OpenOCD: ${OPENOCD_BIN} (${OPENOCD_KIND})"

# ---------------------------------------------------------------- 烧录
flash_linux()
{
  openocd \
    -f interface/stlink.cfg \
    -f target/stm32h7x.cfg \
    -c "transport select swd" \
    -c "program ${HEX_PATH} verify reset exit"
}

flash_windows()
{
  local openocd_scripts
  local win_hex win_hex_path

  # xPack 的 scripts 目录与可执行文件同级的 ../openocd/scripts
  openocd_scripts="$(cd "$(dirname "${OPENOCD_BIN}")/../openocd/scripts" && pwd)"
  [ -f "${openocd_scripts}/interface/stlink.cfg" ] || {
    echo "error: 找不到 OpenOCD 脚本目录: ${openocd_scripts}" >&2
    exit 1
  }

  if [ -z "${WIN_TEMP:-}" ]; then
    local win_user
    if command -v cmd.exe >/dev/null 2>&1; then
      win_user="$(cmd.exe /c "echo %USERNAME%" 2>/dev/null | tr -d '\r\n')"
    elif [ -x /mnt/c/Windows/System32/cmd.exe ]; then
      win_user="$(/mnt/c/Windows/System32/cmd.exe /c "echo %USERNAME%" 2>/dev/null | tr -d '\r\n')"
    fi
    if [ -n "${win_user}" ] && [ -d "/mnt/c/Users/${win_user}/AppData/Local/Temp" ]; then
      WIN_TEMP="/mnt/c/Users/${win_user}/AppData/Local/Temp"
    else
      WIN_TEMP="/mnt/c/Users/19y/AppData/Local/Temp"
    fi
  fi

  win_hex_path="${WIN_TEMP}/openvela_nuttx.hex"
  cp "${HEX_PATH}" "${win_hex_path}"
  win_hex="$(wslpath -w "${win_hex_path}")"
  win_hex="${win_hex//\\//}"   # Tcl 会把反斜杠当转义符，统一转成正斜杠

  "${OPENOCD_BIN}" \
    -s "$(wslpath -w "${openocd_scripts}")" \
    -f interface/stlink.cfg \
    -f target/stm32h7x.cfg \
    -c "transport select swd" \
    -c "program ${win_hex} verify reset exit"
}

echo "==> 开始烧录 ${HEX_PATH}"
if [ "${OPENOCD_KIND}" = "linux" ]; then
  flash_linux
else
  flash_windows
fi

echo "==> 烧录完成（verify OK，目标已复位）"
