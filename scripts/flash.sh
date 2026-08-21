#!/usr/bin/env bash
# WSL Download 入口：经 powershell.exe 调用 Windows CubeProgrammer（flash.ps1）。
# 只烧当前 .debug 产物，不编译、不调试。缺文件则失败，不会静默烧旧固件。
#
# 用法：
#   bash scripts/flash.sh
#   bash scripts/flash.sh -ValidateOnly
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FLASH_PS1="$SCRIPT_DIR/flash.ps1"

if [ ! -f "$FLASH_PS1" ]; then
  echo "error: 缺少 $FLASH_PS1" >&2
  exit 1
fi

if ! command -v powershell.exe >/dev/null 2>&1; then
  echo "error: powershell.exe 未找到。Download 需经 WSL interop 调用 Windows CubeProgrammer。" >&2
  echo "请确认已启用 WSL 与 Windows 互操作，且能在本窗口执行 powershell.exe。" >&2
  exit 1
fi

if ! command -v wslpath >/dev/null 2>&1; then
  echo "error: wslpath 未找到，无法把 flash.ps1 转成 Windows 路径。" >&2
  exit 1
fi

WIN_FLASH_PS1="$(wslpath -w "$FLASH_PS1")"
echo "[flash] powershell.exe -File $WIN_FLASH_PS1 $*"
set +e
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$WIN_FLASH_PS1" "$@"
status=$?
set -e
if [ "$status" -ne 0 ]; then
  echo "error: Download 失败（退出码 $status）。缺 hex 请先 Build；找不到 Cube CLI 请安装 STM32CubeProgrammer 或设置 STM32_PROGRAMMER_CLI；ST-LINK 占用请关闭 Cube GUI / 其它 OpenOCD。" >&2
  exit "$status"
fi
