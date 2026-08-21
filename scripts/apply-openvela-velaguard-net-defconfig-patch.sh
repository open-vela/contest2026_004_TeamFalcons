#!/usr/bin/env bash
# 应用 stm32h750b-dk:velaguard-net 预设 defconfig（幂等，重复执行安全）。
#
# velaguard-net = velaguard-min 底座 + RJ45 以太网（LAN8740A/MII）+
# DHCP/NETINIT + NSH ping + MQTT-C。依赖两个既有补丁（须先应用）：
#   scripts/openvela-eth-mii-stm32h750b-dk.patch（nuttx：MII/PHY 链路轮询）
#   scripts/openvela-netinit-carrier-poll.patch（apps：carrier/DHCP 重连）

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OPENVELA_ROOT="${1:-$(cd "$REPO_ROOT/.." && pwd)}"
NUTTX_ROOT="$OPENVELA_ROOT/nuttx"
PATCH_FILE="$SCRIPT_DIR/openvela-velaguard-net-defconfig.patch"
DEFCONFIG="$NUTTX_ROOT/boards/arm/stm32h7/stm32h750b-dk/configs/velaguard-net/defconfig"

if [[ ! -d "$NUTTX_ROOT/.git" && ! -f "$NUTTX_ROOT/.git" ]]; then
  echo "error: NuttX git checkout not found: $NUTTX_ROOT" >&2
  exit 1
fi

if [[ ! -f "$PATCH_FILE" ]]; then
  echo "error: velaguard-net defconfig patch not found: $PATCH_FILE" >&2
  exit 1
fi

defconfig_markers_present()
{
  test -f "$DEFCONFIG" &&
    grep -Fq 'CONFIG_ARCH_BOARD_STM32H750B_DK' "$DEFCONFIG" &&
    grep -Fq 'CONFIG_NET=y' "$DEFCONFIG" &&
    grep -Fq 'CONFIG_STM32H7_ETHMAC=y' "$DEFCONFIG" &&
    grep -Fq 'CONFIG_INIT_ENTRYPOINT="velaguard_app_main"' "$DEFCONFIG" &&
    grep -Fq 'CONFIG_VG_BRINGUP_TOOLS=y' "$DEFCONFIG"
}

if git -C "$NUTTX_ROOT" apply --reverse --check "$PATCH_FILE" >/dev/null 2>&1; then
  echo "VelaGuard net defconfig patch is already applied."
  exit 0
fi

if defconfig_markers_present; then
  echo "VelaGuard net defconfig patch is already applied (with later overlapping edits)."
  exit 0
fi

if ! git -C "$NUTTX_ROOT" apply --check "$PATCH_FILE"; then
  echo "error: VelaGuard net defconfig patch does not apply cleanly." >&2
  echo "Inspect overlapping changes in $NUTTX_ROOT before retrying." >&2
  exit 1
fi

git -C "$NUTTX_ROOT" apply "$PATCH_FILE"
echo "Applied stm32h750b-dk:velaguard-net defconfig patch."
