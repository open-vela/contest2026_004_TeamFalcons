#!/usr/bin/env bash
# Verify openvela public trees contain VelaGuard upstream changes (no patches).
#
# Changes live on fork feature branches; local integration branch merges all nuttx PRs.
# Optional sync: VG_SYNC_UPSTREAM=1 or build.sh --sync-upstream
#
#   nuttx  → velaguard/integration
#   apps   → velaguard/netinit-esp8266
#   MQTT-C → velaguard/mqtt-pal-hook
set -euo pipefail

# shellcheck source=vg_mainline_target.sh
. "$(cd "$(dirname "$0")" && pwd)/vg_mainline_target.sh"

TARGET="$(vg_normalize_build_target "${1:-velaguard-lvgl}")"
OPENVELA_ROOT="${2:-$(cd "$(dirname "$0")/../.." && pwd)}"
SYNC="${VG_SYNC_UPSTREAM:-0}"

NUTTX_ROOT="$OPENVELA_ROOT/nuttx"
APPS_ROOT="$OPENVELA_ROOT/apps"
MQTTC_ROOT="$OPENVELA_ROOT/apps/netutils/mqttc/MQTT-C"
BOARD="$NUTTX_ROOT/boards/arm/stm32h7/stm32h750b-dk"

NUTTX_BRANCH="velaguard/integration"
APPS_BRANCH="velaguard/netinit-esp8266"
MQTTC_BRANCH="velaguard/mqtt-pal-hook"

fail() {
  echo "error: $*" >&2
  echo >&2
  echo "VelaGuard 已迁移为公共仓直改 + PR，不再使用 scripts/openvela-*.patch。" >&2
  echo "请切换到对应 feature 分支，或运行：" >&2
  echo "  bash scripts/build.sh --sync-upstream ${TARGET}" >&2
  echo >&2
  echo "  git -C nuttx checkout ${NUTTX_BRANCH}" >&2
  if [[ "$TARGET" == "velaguard-lvgl" || "$TARGET" == "ai-probe" || "$TARGET" == "emmc" ]]; then
    echo "  git -C apps checkout ${APPS_BRANCH}" >&2
    echo "  git -C apps/netutils/mqttc/MQTT-C checkout ${MQTTC_BRANCH}" >&2
  fi
  exit 1
}

git_tree_clean() {
  local dir="$1"
  git -C "$dir" diff --quiet && git -C "$dir" diff --cached --quiet
}

maybe_checkout() {
  local dir="$1" branch="$2" label="$3"
  local current
  current="$(git -C "$dir" branch --show-current 2>/dev/null || true)"
  if [[ "$current" == "$branch" ]]; then
    echo "[upstream] ${label}: already on ${branch}"
    return 0
  fi
  if [[ "$SYNC" != "1" ]]; then
    return 1
  fi
  if ! git_tree_clean "$dir"; then
    echo "error: ${label} has uncommitted changes; commit or stash before --sync-upstream" >&2
    exit 1
  fi
  if ! git -C "$dir" show-ref --verify --quiet "refs/heads/${branch}"; then
    echo "error: ${label} missing local branch ${branch}" >&2
    exit 1
  fi
  echo "[upstream] ${label}: checkout ${branch} (was ${current:-detached})"
  git -C "$dir" checkout "$branch"
}

nuttx_qspi_ready() {
  grep -Fq 'if ARCH_BOARD_STM32H750B_DK' "$NUTTX_ROOT/boards/Kconfig" &&
    grep -Fq 'STM32_QSPI_BOOT_BASE 0x90000000' \
      "$NUTTX_ROOT/arch/arm/src/stm32h7/stm32_mpuinit.c" &&
    grep -Fq 'config STM32H750B_DK_QSPI_BOOT' "$BOARD/Kconfig" &&
    test -f "$BOARD/scripts/qspi_flash.ld"
}

nuttx_board_pins_ready() {
  grep -Fq 'GPIO_ESP_EN' "$BOARD/include/board.h" &&
    grep -Fq 'GPIO_UART7_RS485_DIR' "$BOARD/include/board.h" &&
    grep -Fq 'symlink("/dev/ttyS2", "/dev/rs485")' "$BOARD/src/stm32_bringup.c" &&
    test -f "$BOARD/src/stm32_pwm.c"
}

nuttx_eth_mii_ready() {
  grep -Fq 'BOARD_ETH_PHY_POLL' "$BOARD/include/board.h"
}

nuttx_lvgl_hmi_defconfig_ready() {
  local dc="$BOARD/configs/velaguard-lvgl/defconfig"
  local src
  src="$(cd "$(dirname "$0")" && pwd)/configs/velaguard-lvgl.defconfig"
  if [[ -f "$src" ]]; then
    mkdir -p "$(dirname "$dc")"
    if [[ ! -f "$dc" ]] || ! cmp -s "$src" "$dc"; then
      echo "[upstream] install contest velaguard-lvgl defconfig"
      cp "$src" "$dc"
    fi
  fi
  test -f "$dc" &&
    grep -Fq 'CONFIG_GRAPHICS_LVGL=y' "$dc" &&
    grep -Fq 'CONFIG_VG_HMI=y' "$dc" &&
    grep -Fq 'CONFIG_VG_HMI_AUTOSTART=y' "$dc" &&
    grep -Fq 'CONFIG_INIT_ENTRYPOINT="velaguard_app_main"' "$dc" &&
    grep -Fq 'CONFIG_NET=y' "$dc" &&
    grep -Fq 'CONFIG_VG_BRINGUP_TOOLS=y' "$dc" &&
    grep -Fq 'CONFIG_VG_BUS_DISCOVER=y' "$dc" &&
    grep -Fq 'CONFIG_STM32H7_LTDC=y' "$dc" &&
    grep -Fq 'CONFIG_INPUT_FT5X06=y' "$dc" &&
    grep -Fq '# CONFIG_FT5X06_POLLMODE is not set' "$dc" &&
    grep -Fq 'CONFIG_LV_DEF_REFR_PERIOD=16' "$dc" &&
    grep -Fq 'CONFIG_STM32H750B_DK_QSPI_BOOT=y' "$dc"
}

nuttx_net_defconfig_ready() {
  local dc="$BOARD/configs/velaguard-net/defconfig"
  test -f "$dc" &&
    grep -Fq 'CONFIG_NET=y' "$dc" &&
    grep -Fq 'CONFIG_NETUTILS_ESP8266=y' "$dc" &&
    grep -Fq 'CONFIG_VG_NET_FAILOVER=y' "$dc" &&
    grep -Fq 'CONFIG_INIT_ENTRYPOINT="velaguard_app_main"' "$dc" &&
    grep -Fq 'CONFIG_STM32H7_SDMMC1=y' "$dc" &&
    grep -Fq 'CONFIG_MMCSD_MMCSUPPORT=y' "$dc" &&
    grep -Fq 'CONFIG_FS_FAT=y' "$dc" &&
    grep -Fq 'CONFIG_VG_CONFIG_STORE=y' "$dc" &&
    grep -Fq 'CONFIG_VG_FRAME_STATS=y' "$dc" &&
    grep -Fq 'CONFIG_VG_CONFIG_BASEDIR="/data/velaguard/config"' "$dc" &&
    grep -Fq 'CONFIG_EXAMPLES_AI_AGENT_VELA=y' "$dc" &&
    grep -Fq 'CONFIG_EXAMPLES_AI_AGENT_VELA_DATA_DIR="/data/agent"' "$dc" &&
    grep -Fq 'CONFIG_CRYPTO_MBEDTLS=y' "$dc"
}

nuttx_min_defconfig_ready() {
  local dc="$BOARD/configs/velaguard-min/defconfig"
  test -f "$dc" &&
    grep -Fq 'CONFIG_INIT_ENTRYPOINT="velaguard_app_main"' "$dc" &&
    grep -Fq 'CONFIG_VG_BRINGUP_TOOLS=y' "$dc"
}

nuttx_ai_probe_defconfig_ready() {
  local dc="$BOARD/configs/velaguard-ai-probe/defconfig"
  test -f "$dc" &&
    grep -Fq 'CONFIG_EXAMPLES_AI_AGENT_VELA=y' "$dc" &&
    grep -Fq 'CONFIG_CRYPTO_MBEDTLS=y' "$dc" &&
    grep -Fq 'CONFIG_INIT_ENTRYPOINT="nsh_main"' "$dc"
}

nuttx_emmc_ready() {
  local dc="$BOARD/configs/velaguard-emmc/defconfig"
  test -f "$BOARD/src/stm32_sdmmc.c" &&
    grep -Fq 'GPIO_SDMMC1_D7' "$BOARD/include/board.h" &&
    grep -Fq 'stm32_sdio_initialize' "$BOARD/src/stm32_bringup.c" &&
    test -f "$dc" &&
    grep -Fq 'CONFIG_STM32H7_SDMMC1=y' "$dc" &&
    grep -Fq 'CONFIG_MMCSD_MMCSUPPORT=y' "$dc" &&
    grep -Fq 'CONFIG_FS_FAT=y' "$dc" &&
    grep -Fq 'CONFIG_FAT_LFN=y' "$dc" &&
    grep -Fq 'CONFIG_INIT_ENTRYPOINT="velaguard_app_main"' "$dc"
}

nuttx_pwm_tim15_ready() {
  grep -A6 'CONFIG_STM32H7_TIM15_CHANNEL2' \
    "$NUTTX_ROOT/arch/arm/src/stm32h7/stm32_pwm.c" |
    grep -Fq 'CONFIG_STM32H7_TIM15_CH2OUT'
}

nuttx_display_ready() {
  grep -Fq 'BOARD_SDRAM2_HEAP_OFFSET' "$BOARD/include/board.h" &&
    grep -Fq 'static volatile bool g_flip_pending' \
      "$NUTTX_ROOT/arch/arm/src/stm32h7/stm32_ltdc.c"
}

nuttx_touch_ready() {
  test -f "$BOARD/src/stm32_ft5x06.c" &&
    grep -Fq 'GPIO_INPUT|GPIO_PULLUP' "$BOARD/src/stm32h750b-dk.h" &&
    grep -Fq 'stm32_gpiosetevent(GPIO_FT5X06_INT, false, true, true,' \
      "$BOARD/src/stm32_ft5x06.c" &&
    grep -Fq '<nuttx/spinlock.h>' "$BOARD/src/stm32_ft5x06.c" &&
    grep -Fq 'FT5X06_G_MODE_INTERRUPT_TRIGGER' \
      "$NUTTX_ROOT/drivers/input/ft5x06.c" &&
    grep -Fq 'deltay < CONFIG_FT5X06_THRESHY' \
      "$NUTTX_ROOT/drivers/input/ft5x06.c"
}

apps_netinit_ready() {
  grep -Fq 'netinit_set_ipv4_config' "$APPS_ROOT/include/netutils/netinit.h" &&
    grep -Fq 'NETINIT_HAVE_NETDEV' "$APPS_ROOT/netutils/netinit/netinit.c" &&
    grep -Fq 'config NETINIT_CARRIER_POLL' "$APPS_ROOT/netutils/netinit/Kconfig"
}

apps_esp8266_ready() {
  grep -Fq 'LESP_SSID_SIZE               lespSSID_SIZE' \
    "$APPS_ROOT/netutils/esp8266/esp8266.c"
}

mqttc_hook_ready() {
  grep -Fq 'vg_mqtt_pal_try_sendall' "$MQTTC_ROOT/src/mqtt_pal.c" &&
    grep -Fq 'vg_mqtt_pal_try_recvall' "$MQTTC_ROOT/src/mqtt_pal.c"
}

verify_nuttx() {
  local missing=()
  nuttx_qspi_ready || missing+=("nuttx/QSPI boot")
  case "$TARGET" in
    velaguard-lvgl)
      nuttx_board_pins_ready || missing+=("nuttx/board pins+bringup")
      nuttx_eth_mii_ready || missing+=("nuttx/Ethernet MII PHY poll")
      nuttx_emmc_ready || missing+=("nuttx/SDMMC+eMMC bringup")
      nuttx_lvgl_hmi_defconfig_ready || missing+=("nuttx/velaguard-lvgl defconfig")
      nuttx_display_ready || missing+=("nuttx/LTDC display acceleration")
      nuttx_touch_ready || missing+=("nuttx/FT5x06 touch performance")
      ;;
    min)
      nuttx_board_pins_ready || missing+=("nuttx/board pins+bringup")
      nuttx_pwm_tim15_ready || missing+=("nuttx/TIM15 PWM guard")
      nuttx_min_defconfig_ready || missing+=("nuttx/velaguard-min defconfig")
      ;;
    ai-probe)
      nuttx_board_pins_ready || missing+=("nuttx/board pins+bringup")
      nuttx_eth_mii_ready || missing+=("nuttx/Ethernet MII PHY poll")
      nuttx_ai_probe_defconfig_ready || missing+=("nuttx/velaguard-ai-probe defconfig")
      ;;
    emmc)
      nuttx_board_pins_ready || missing+=("nuttx/board pins+bringup")
      nuttx_eth_mii_ready || missing+=("nuttx/Ethernet MII PHY poll")
      nuttx_emmc_ready || missing+=("nuttx/velaguard-emmc SDMMC+defconfig")
      ;;
    lvgl)
      nuttx_display_ready || missing+=("nuttx/LTDC display acceleration")
      nuttx_touch_ready || missing+=("nuttx/FT5x06 touch performance")
      ;;
  esac
  if ((${#missing[@]})); then
    maybe_checkout "$NUTTX_ROOT" "$NUTTX_BRANCH" "nuttx" || fail "nuttx missing: ${missing[*]}"
    missing=()
    nuttx_qspi_ready || missing+=("nuttx/QSPI boot")
    case "$TARGET" in
      velaguard-lvgl)
        nuttx_board_pins_ready || missing+=("nuttx/board pins+bringup")
        nuttx_eth_mii_ready || missing+=("nuttx/Ethernet MII PHY poll")
        nuttx_emmc_ready || missing+=("nuttx/SDMMC+eMMC bringup")
        nuttx_lvgl_hmi_defconfig_ready || missing+=("nuttx/velaguard-lvgl defconfig")
        nuttx_display_ready || missing+=("nuttx/LTDC display acceleration")
        nuttx_touch_ready || missing+=("nuttx/FT5x06 touch performance")
        ;;
      min)
        nuttx_board_pins_ready || missing+=("nuttx/board pins+bringup")
        nuttx_pwm_tim15_ready || missing+=("nuttx/TIM15 PWM guard")
        nuttx_min_defconfig_ready || missing+=("nuttx/velaguard-min defconfig")
        ;;
      ai-probe)
        nuttx_board_pins_ready || missing+=("nuttx/board pins+bringup")
        nuttx_eth_mii_ready || missing+=("nuttx/Ethernet MII PHY poll")
        nuttx_ai_probe_defconfig_ready || missing+=("nuttx/velaguard-ai-probe defconfig")
        ;;
      emmc)
        nuttx_board_pins_ready || missing+=("nuttx/board pins+bringup")
        nuttx_eth_mii_ready || missing+=("nuttx/Ethernet MII PHY poll")
        nuttx_emmc_ready || missing+=("nuttx/velaguard-emmc SDMMC+defconfig")
        ;;
      lvgl)
        nuttx_display_ready || missing+=("nuttx/LTDC display acceleration")
        nuttx_touch_ready || missing+=("nuttx/FT5x06 touch performance")
        ;;
    esac
    ((${#missing[@]})) && fail "nuttx still missing after sync: ${missing[*]}"
  fi
  echo "[upstream] nuttx: VelaGuard changes present ($(git -C "$NUTTX_ROOT" branch --show-current))"
}

verify_apps_net() {
  local missing=()
  apps_netinit_ready || missing+=("apps/netinit carrier+IPv4 policy")
  apps_esp8266_ready || missing+=("apps/ESP8266 LESP compat")
  if ((${#missing[@]})); then
    maybe_checkout "$APPS_ROOT" "$APPS_BRANCH" "apps" || fail "apps missing: ${missing[*]}"
    missing=()
    apps_netinit_ready || missing+=("apps/netinit carrier+IPv4 policy")
    apps_esp8266_ready || missing+=("apps/ESP8266 LESP compat")
    ((${#missing[@]})) && fail "apps still missing after sync: ${missing[*]}"
  fi
  echo "[upstream] apps: VelaGuard changes present ($(git -C "$APPS_ROOT" branch --show-current))"
}

verify_mqttc() {
  mqttc_hook_ready || {
    maybe_checkout "$MQTTC_ROOT" "$MQTTC_BRANCH" "MQTT-C" || fail "MQTT-C missing: pal LESP hooks"
    mqttc_hook_ready || fail "MQTT-C still missing pal hooks after sync"
  }
  echo "[upstream] MQTT-C: VelaGuard changes present ($(git -C "$MQTTC_ROOT" branch --show-current))"
}

if [[ ! -d "$NUTTX_ROOT/.git" && ! -f "$NUTTX_ROOT/.git" ]]; then
  fail "NuttX git checkout not found: $NUTTX_ROOT"
fi

verify_nuttx
if [[ "$TARGET" == "velaguard-lvgl" || "$TARGET" == "ai-probe" || "$TARGET" == "emmc" ]]; then
  verify_apps_net
  verify_mqttc
fi
