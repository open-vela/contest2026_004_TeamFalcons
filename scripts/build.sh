#!/usr/bin/env bash
# VelaGuard 固件构建 + 暂存烧录产物。
# 日常入口是本仓 scripts/build.sh，不是父目录 openvela 的 ./build.sh。
#
# 用法：
#   bash scripts/build.sh [TARGET] [--clean] [--debug]
#   作品主线（默认）：velaguard | velaguard-lvgl | net（后两个与 velaguard 相同）
#   仅 bring-up / 探针：min | lvgl | ai-probe | emmc
#
#   示例：
#     bash scripts/build.sh               # 增量发布构建（velaguard-lvgl）
#     bash scripts/build.sh --debug       # 增量调试构建（-g3 -Og，F5 走这条）
#     bash scripts/build.sh --clean       # Rebuild 复位到 velaguard-lvgl 发布配置
#     bash scripts/build.sh emmc --clean  # eMMC bring-up 预设
#     bash scripts/build.sh --clean --debug
#     bash scripts/build.sh --no-llm-health  # P0 计时矩阵：关 30s 健康探测（不进发布配置）
#     bash scripts/build.sh --tls-forensics  # 诊断镜像：TLS 握手失败取证（不进发布配置）
#     bash scripts/build.sh --tls-pool       # 诊断镜像：mbedTLS 静态池 A/B 实验（不进发布配置）
#     bash scripts/build.sh --ota-fixtures   # 诊断镜像：启用 OTA 测试 fixture（不进发布配置）
#     bash scripts/build.sh --config-fixture  # 诊断镜像：在线 Modbus + 本地确认自动验收（不进发布配置）
#
# 产物：
#   nuttx/nuttx.hex、nuttx/nuttx.bin（A 槽，默认烧录）
#   .debug/nuttx.hex、nuttx-a.hex、nuttx-b.hex 及对应 bin/elf
#   .debug/qspi_bootstub.hex
#   Download 固定烧 .debug 里的 bootstub 和 nuttx.hex（A 槽）。
#
# 每次构建重新链接 A 和 B 两份镜像。B 镜像使用从 A 链接脚本生成的
# 0x90800000/8MiB 脚本，并用 check_fw_images.py 校验版本、槽位标记和
# 向量表；本阶段 boot stub 仍只启动 A，B 镜像供后续 OTA 安装使用。
#
# Rebuild（--clean）会 configure.sh -E 复位到所选预设：未 savedefconfig
# 的本地 nuttx/.config / menuconfig 改动会被丢掉。
#
# 公共仓改动在 nuttx/apps/MQTT-C 的 feature 分支上，不再 apply patch。
# 若树未就绪：bash scripts/build.sh --sync-upstream
#
# 防呆：构建开始前先清空 .debug 里的旧主镜像，构建失败时 Download 会因
# 缺少 nuttx.hex 直接报错，而不是把上一次的旧固件烧到板子上。
set -euo pipefail

TARGET="velaguard-lvgl"
MODE="build"
DEBUG=0
SYNC_UPSTREAM=0
HMI_PERF=0
VOICE_FIXTURE=0
OTA_FIXTURES=0
CONFIG_ACCEPTANCE_FIXTURE=0
TLS_FORENSICS=0
TLS_POOL=0
NO_LLM_HEALTH=0

# shellcheck source=vg_mainline_target.sh
. "$(cd "$(dirname "$0")" && pwd)/vg_mainline_target.sh"

for arg in "$@"; do
  case "$arg" in
    --debug)
      DEBUG=1
      ;;
    --clean)
      MODE="--clean"
      ;;
    --sync-upstream)
      SYNC_UPSTREAM=1
      ;;
    --hmi-perf)
      HMI_PERF=1
      ;;
    --voice-fixture)
      VOICE_FIXTURE=1
      ;;
    --ota-fixtures)
      OTA_FIXTURES=1
      ;;
    --config-fixture)
      CONFIG_ACCEPTANCE_FIXTURE=1
      OTA_FIXTURES=1
      ;;
    --tls-forensics)
      TLS_FORENSICS=1
      ;;
    --no-llm-health)
      NO_LLM_HEALTH=1
      ;;
    --tls-pool)
      TLS_POOL=1
      ;;
    net|min|lvgl|velaguard|velaguard-lvgl|ai-probe|emmc)
      TARGET="$(vg_normalize_build_target "$arg")"
      ;;
    *)
      echo "error: 未知参数 '$arg'（用法: build.sh [velaguard|min|lvgl|ai-probe|emmc] [--clean] [--debug] [--ota-fixtures|--config-fixture]）" >&2
      exit 1
      ;;
  esac
done

TARGET="$(vg_normalize_build_target "$TARGET")"

if [ "$VOICE_FIXTURE" = 1 ] && [ "$TARGET" != "velaguard-lvgl" ]; then
  echo "error: --voice-fixture 仅适用于作品主线" >&2
  exit 1
fi

if [ "$OTA_FIXTURES" = 1 ] && [ "$TARGET" != "velaguard-lvgl" ]; then
  echo "error: --ota-fixtures 仅适用于作品主线" >&2
  exit 1
fi

if [ "$CONFIG_ACCEPTANCE_FIXTURE" = 1 ] &&
   [ "$TARGET" != "velaguard-lvgl" ]; then
  echo "error: --config-fixture 仅适用于作品主线" >&2
  exit 1
fi

if [ "$HMI_PERF" = 1 ] && [ "$TARGET" != "velaguard-lvgl" ]; then
  echo "error: --hmi-perf 仅适用于作品主线 velaguard-lvgl，不适用于 $TARGET" >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CONTEST_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OPENVELA_ROOT="$(cd "$CONTEST_ROOT/.." && pwd)"
NUTTX_ROOT="$OPENVELA_ROOT/nuttx"
STAGE_DIR="$CONTEST_ROOT/.debug"

if [ ! -f "$CONTEST_ROOT/VERSION" ]; then
  echo "error: 缺少 $CONTEST_ROOT/VERSION" >&2
  exit 1
fi

FW_VERSION="$(tr -d '[:space:]' < "$CONTEST_ROOT/VERSION")"
if [[ ! "$FW_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "error: VERSION 必须是 MAJOR.MINOR.PATCH，实际为 '$FW_VERSION'" >&2
  exit 1
fi

FW_BUILD_ID="$(git -C "$CONTEST_ROOT" describe --always --dirty --abbrev=12 2>/dev/null || true)"
if [ -z "$FW_BUILD_ID" ]; then
  FW_BUILD_ID="unknown"
fi

write_fw_build_header()
{
  local objdir="$CONTEST_ROOT/build/velaguard"
  mkdir -p "$objdir"
  printf '%s\n' \
    '#ifndef __VG_FW_BUILD_H' \
    '#define __VG_FW_BUILD_H' \
    "#define VG_FW_VERSION \"$FW_VERSION\"" \
    "#define VG_FW_BUILD_ID \"$FW_BUILD_ID\"" \
    '#endif' > "$objdir/vg_fw_build.h"
}

# LLM 凭据走 eMMC 加密 provision（secrets/ + scripts/provision-llm-from-secrets.sh）

export PATH="$OPENVELA_ROOT/prebuilts/tools/python/bin:$OPENVELA_ROOT/prebuilts/tools/linux/x86_64:$OPENVELA_ROOT/prebuilts/kconfig-frontends/bin:$OPENVELA_ROOT/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:$OPENVELA_ROOT/prebuilts/build-tools/linux-x86_64/bin:$PATH"
export PYTHONPATH="$OPENVELA_ROOT/prebuilts/tools/python/dist-packages/kconfiglib:$OPENVELA_ROOT/prebuilts/tools/python/dist-packages:${PYTHONPATH:-}"

case "$TARGET" in
  min)
    DEFCONFIG="velaguard-min"
    ;;
  lvgl)
    DEFCONFIG="lvgl"
    ;;
  velaguard-lvgl)
    DEFCONFIG="velaguard-lvgl"
    ;;
  ai-probe)
    DEFCONFIG="velaguard-ai-probe"
    ;;
  emmc)
    DEFCONFIG="velaguard-emmc"
    ;;
  *)
    echo "error: 未知目标 '$TARGET'（可选 velaguard|min|lvgl|ai-probe|emmc）" >&2
    exit 1
    ;;
esac

echo "[build] TARGET=${TARGET} preset=stm32h750b-dk:${DEFCONFIG} MODE=${MODE} DEBUG=${DEBUG}"

# 按目标校验当前 .config 形态；不是就自动 distclean 复位。
# （menuconfig 改动会污染 .config：入口、网络、LVX demo 等会被改回去，
#   BASE_DEFCONFIG 字符串却不变，所以这里按关键符号逐一校验。）
expect_dev_config()
{
  case "$TARGET" in
    min)
      grep -q 'CONFIG_INIT_ENTRYPOINT="velaguard_app_main"' "$NUTTX_ROOT/.config" &&
        grep -q 'CONFIG_INIT_ENTRYNAME="velaguard_app_main"' "$NUTTX_ROOT/.config" &&
        ! grep -q '^CONFIG_GRAPHICS_LVGL=' "$NUTTX_ROOT/.config" &&
        ! grep -q '^CONFIG_NET=' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_VG_BRINGUP_TOOLS=y' "$NUTTX_ROOT/.config"
      ;;

    lvgl)
      grep -q '^CONFIG_GRAPHICS_LVGL=y' "$NUTTX_ROOT/.config" &&
        grep -q 'CONFIG_INIT_ENTRYPOINT="nsh_main"' "$NUTTX_ROOT/.config"
      ;;

    velaguard-lvgl)
      grep -q '^CONFIG_GRAPHICS_LVGL=y' "$NUTTX_ROOT/.config" &&
        grep -q 'CONFIG_INIT_ENTRYPOINT="velaguard_app_main"' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_INIT_STACKSIZE=8192$' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_STACK_COLORATION=y$' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_VG_OTA_VERIFIED_ENTROPY=y$' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_VG_OTA_NETWORK=y$' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_VG_CONFIG_MQTT=y$' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_NETDEV_PHY_IOCTL=y$' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_STM32H7_RNG=y$' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_DEV_RANDOM=y$' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_DEV_URANDOM_ARCH=y$' "$NUTTX_ROOT/.config" &&
        ! grep -q '^CONFIG_DEV_URANDOM_XORSHIFT128=y$' "$NUTTX_ROOT/.config" &&
        ! grep -q '^CONFIG_DEV_URANDOM_CONGRUENTIAL=y$' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_NET_STATISTICS=y$' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_NET_SOLINGER=y$' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_SCHED_CHILD_STATUS=y$' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_VG_HMI=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_VG_HMI_AUTOSTART=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_STM32H750B_DK_QSPI_BOOT=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_NET=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_VG_BRINGUP_TOOLS=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_VG_BUS_DISCOVER=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_EXAMPLES_AI_AGENT_VELA=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_STM32H7_LTDC=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_INPUT_FT5X06=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_STM32H750B_DK_BUTTONS=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_INPUT_BUTTONS_LOWER=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_VG_PTT=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_AUDIO_DRIVER_SPECIFIC_BUFFERS=y$' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_AUDIO_WM8994=y$' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_WM8994_INITVOLUME=10$' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_STM32H7_SAI2_A=y$' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_STM32H7_SAI_CAPTURE=y$' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_VG_VOICE_LOOPBACK=y$' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_VG_VOICE_TTS_STREAM=y$' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_VG_VOICE_TTS_PREBUF_MS=2500$' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_VG_VOICE_TTS_RING_BYTES=131072$' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_AI_AGENT_VOICE_CONN_SLOT=y$' "$NUTTX_ROOT/.config" &&
        ! grep -q '^CONFIG_VG_VOICE_DIRECT_TOOL=y$' "$NUTTX_ROOT/.config" &&
        ! grep -q '^CONFIG_VG_VOICE_TLS_CACHE=y$' "$NUTTX_ROOT/.config" &&
        { [ "$VOICE_FIXTURE" = 1 ] || ! grep -q '^CONFIG_VG_VOICE_TEST_FIXTURE=y$' "$NUTTX_ROOT/.config"; } &&
        grep -q '^CONFIG_VG_AUDIO_PROBE=y$' "$NUTTX_ROOT/.config" &&
        ! grep -q '^CONFIG_EXAMPLES_LVGLDEMO=y' "$NUTTX_ROOT/.config" &&
        ! grep -q '^CONFIG_VG_HMI_PERF=y' "$NUTTX_ROOT/.config" &&
        { [ "$OTA_FIXTURES" = 1 ] || ! grep -q '^CONFIG_VG_OTA_TEST_FIXTURES=y$' "$NUTTX_ROOT/.config"; } &&
        { [ "$CONFIG_ACCEPTANCE_FIXTURE" = 1 ] ||
          ! grep -q '^CONFIG_VG_CONFIG_ACCEPTANCE_FIXTURE=y$' "$NUTTX_ROOT/.config"; }
      ;;

    ai-probe)
      grep -q 'CONFIG_INIT_ENTRYPOINT="nsh_main"' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_EXAMPLES_AI_AGENT_VELA=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_CRYPTO_MBEDTLS=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_NET=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_RAMMTD=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_FS_LITTLEFS=y' "$NUTTX_ROOT/.config"
      ;;

    emmc)
      grep -q 'CONFIG_INIT_ENTRYPOINT="velaguard_app_main"' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_STM32H7_SDMMC1=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_MMCSD=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_MMCSD_MMCSUPPORT=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_FS_FAT=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_FAT_LFN=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_VG_CONFIG_STORE=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_VG_FRAME_STATS=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_NET=y' "$NUTTX_ROOT/.config" &&
        ! grep -q '^CONFIG_STM32H7_SDMMC_IDMA=y' "$NUTTX_ROOT/.config"
      ;;
  esac
}

prepare_build_trees()
{
  echo "[build] ensure contest app link..."
  bash "$SCRIPT_DIR/ensure-openvela-links.sh" "$OPENVELA_ROOT"

  if [ -d "$OPENVELA_ROOT/packages/demos" ]; then
    echo "[build] regenerate packages/demos/Kconfig..."
    (
      cd "$OPENVELA_ROOT/packages/demos"
      "$OPENVELA_ROOT/apps/tools/mkkconfig.sh" -m Demos
    )
  fi

  echo "[build] verify upstream public trees (no patches)..."
  VG_SYNC_UPSTREAM="$SYNC_UPSTREAM" \
    bash "$SCRIPT_DIR/ensure-upstream-velaguard-trees.sh" "$TARGET" "$OPENVELA_ROOT"
}

ensure_bootstub()
{
  mkdir -p "$STAGE_DIR"
  # The stub is small and now carries the FAT/BCB boot path.  Rebuild it on
  # every invocation so a stale HEX can never hide a source change.
  echo "[build] 构建 QSPI boot stub..."
  OTA_FIXTURES="$OTA_FIXTURES" bash "$SCRIPT_DIR/qspi_boot_stub/build_bootstub.sh" "$STAGE_DIR"
  if [ ! -f "$STAGE_DIR/qspi_bootstub.hex" ]; then
    echo "error: 缺少 $STAGE_DIR/qspi_bootstub.hex（bootstub 构建未产出 HEX）" >&2
    exit 1
  fi
}

# F5 / --debug：-g3 + -Og。普通 Build/Download：无符号 + FULLOPT。
# 不写进 defconfig，所以 Rebuild 仍回到发布配置。
compiler_mode_ok()
{
  if [ ! -f "$NUTTX_ROOT/.config" ]; then
    return 1
  fi
  if [ "$DEBUG" = 1 ]; then
    grep -q '^CONFIG_DEBUG_SYMBOLS=y' "$NUTTX_ROOT/.config" &&
      grep -q '^CONFIG_DEBUG_CUSTOMOPT=y' "$NUTTX_ROOT/.config"
  else
    ! grep -q '^CONFIG_DEBUG_SYMBOLS=y' "$NUTTX_ROOT/.config" &&
      grep -q '^CONFIG_DEBUG_FULLOPT=y' "$NUTTX_ROOT/.config"
  fi
}

apply_compiler_mode()
{
  if [ "$DEBUG" = 1 ]; then
    echo "[build] 调试构建：CONFIG_DEBUG_SYMBOLS + -Og"
    kconfig-tweak --file "$NUTTX_ROOT/.config" \
      --enable CONFIG_DEBUG_SYMBOLS \
      --set-str CONFIG_DEBUG_SYMBOLS_LEVEL '-g3' \
      --disable CONFIG_DEBUG_NOOPT \
      --enable CONFIG_DEBUG_CUSTOMOPT \
      --set-str CONFIG_DEBUG_OPTLEVEL '-Og' \
      --disable CONFIG_DEBUG_FULLOPT
  else
    echo "[build] 发布构建：无调试符号 + FULLOPT"
    kconfig-tweak --file "$NUTTX_ROOT/.config" \
      --disable CONFIG_DEBUG_SYMBOLS \
      --disable CONFIG_DEBUG_NOOPT \
      --disable CONFIG_DEBUG_CUSTOMOPT \
      --enable CONFIG_DEBUG_FULLOPT
  fi
  make -C "$NUTTX_ROOT" olddefconfig
}

prepare_build_trees
ensure_bootstub

# 防呆：清掉旧烧录产物，失败时 Download 报"找不到 nuttx.hex"而不是烧旧固件
rm -f "$STAGE_DIR/nuttx.hex" "$STAGE_DIR/nuttx.bin" "$STAGE_DIR/nuttx.elf"

if [ "$MODE" = "--clean" ] || ! expect_dev_config; then
  echo "[build] 重新配置 ${DEFCONFIG} 预设（distclean）..."
  # PREFIX objects live in the contest tree; nuttx distclean does not always
  # rebuild them when only .config changes (stale velaguard.o skips HMI autostart).
  rm -rf "$SCRIPT_DIR/../build/velaguard"
  "$NUTTX_ROOT/tools/configure.sh" -E -e "stm32h750b-dk:${DEFCONFIG}"
fi

if [ "$HMI_PERF" = 1 ]; then
  echo "[build] measurement build: enabling CONFIG_VG_HMI_PERF (not saved to the defconfig)"
  kconfig-tweak --file "$NUTTX_ROOT/.config" --enable CONFIG_VG_HMI_PERF
  make -C "$NUTTX_ROOT" olddefconfig
  # App objects compile with -DVG_HMI_PERF_ENABLED; drop stale objects so
  # the toggle cannot leave half-old binaries behind.
  rm -rf "$SCRIPT_DIR/../build/velaguard"
fi

if [ "$VOICE_FIXTURE" = 1 ]; then
  echo "[build] diagnostic image: enabling fixed local voice fault fixture"
  kconfig-tweak --file "$NUTTX_ROOT/.config" --enable CONFIG_VG_VOICE_TEST_FIXTURE
  make -C "$NUTTX_ROOT" olddefconfig
  grep -q '^CONFIG_VG_VOICE_TEST_FIXTURE=y$' "$NUTTX_ROOT/.config" || exit 1
  rm -rf "$SCRIPT_DIR/../build/velaguard"
fi

if [ "$OTA_FIXTURES" = 1 ]; then
  echo "[build] diagnostic image: enabling OTA test fixtures"
  kconfig-tweak --file "$NUTTX_ROOT/.config" --enable CONFIG_VG_OTA_TEST_FIXTURES
  make -C "$NUTTX_ROOT" olddefconfig
  grep -q '^CONFIG_VG_OTA_TEST_FIXTURES=y$' "$NUTTX_ROOT/.config" || exit 1
  rm -rf "$SCRIPT_DIR/../build/velaguard"
fi

if [ "$CONFIG_ACCEPTANCE_FIXTURE" = 1 ]; then
  echo "[build] diagnostic image: enabling configuration acceptance fixture"
  kconfig-tweak --file "$NUTTX_ROOT/.config" \
    --enable CONFIG_VG_CONFIG_ACCEPTANCE_FIXTURE
  make -C "$NUTTX_ROOT" olddefconfig
  grep -q '^CONFIG_VG_CONFIG_ACCEPTANCE_FIXTURE=y$' "$NUTTX_ROOT/.config" || exit 1
  rm -rf "$SCRIPT_DIR/../build/velaguard"
fi

if [ "$NO_LLM_HEALTH" = 1 ]; then
  # P0 计时矩阵固件：30s 健康探测只与 agent 轮互斥，不覆盖语音轮的
  # 录音/ASR/TTS/播放阶段，探测可能在被测阶段并发握手。测量期关闭，
  # 保持与 2026-10-23 基线可比；发布配置不受影响。
  echo "[build] timing-matrix image: disabling CONFIG_VG_LLM_HEALTH"
  kconfig-tweak --file "$NUTTX_ROOT/.config" --disable CONFIG_VG_LLM_HEALTH
  make -C "$NUTTX_ROOT" olddefconfig
  grep -q '^# CONFIG_VG_LLM_HEALTH is not set$' "$NUTTX_ROOT/.config" || exit 1
  rm -rf "$SCRIPT_DIR/../build/velaguard"
fi

if [ "$TLS_FORENSICS" = 1 ]; then
  echo "[build] diagnostic image: enabling TLS handshake forensics capture"
  kconfig-tweak --file "$NUTTX_ROOT/.config" --enable CONFIG_AI_AGENT_TLS_FORENSICS
  # 健康探测每 30s 产生一次后台 TLS，会与被测握手争用唯一捕获槽；取证
  # 镜像期间关闭，避免失败样本静默丢失。
  kconfig-tweak --file "$NUTTX_ROOT/.config" --disable CONFIG_VG_LLM_HEALTH
  make -C "$NUTTX_ROOT" olddefconfig
  grep -q '^CONFIG_AI_AGENT_TLS_FORENSICS=y$' "$NUTTX_ROOT/.config" || exit 1
  rm -rf "$SCRIPT_DIR/../build/velaguard"
fi

if [ "$TLS_POOL" = 1 ]; then
  echo "[build] diagnostic image: enabling dedicated TLS allocation pool"
  kconfig-tweak --file "$NUTTX_ROOT/.config" --enable CONFIG_AI_AGENT_TLS_POOL
  # 16KB content buffers need ~73KB per concurrent pair and overflow the
  # 100KB static pool during advice+probe concurrency; 8KB keeps the full
  # MiMo chain (4.3KB) and HTTP chunked streaming intact for the A/B.
  kconfig-tweak --file "$NUTTX_ROOT/.config" --set-val CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN 8192
  kconfig-tweak --file "$NUTTX_ROOT/.config" --set-val CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN 8192
  make -C "$NUTTX_ROOT" olddefconfig
  grep -q '^CONFIG_AI_AGENT_TLS_POOL=y$' "$NUTTX_ROOT/.config" || exit 1
  grep -q '^CONFIG_MBEDTLS_PLATFORM_MEMORY=y$' "$NUTTX_ROOT/.config" || exit 1
  # MBEDTLS_PLATFORM_MEMORY 改变 mbedtls 库本身的编译内容；不清旧对象会
  # 链接不到 mbedtls_platform_set_calloc_free。ai_agent 的对象同样依赖
  # Kconfig 宏（平铺构建不自动重编包对象），一并清理。
  find "$OPENVELA_ROOT/apps/crypto/mbedtls/mbedtls/library" -name '*.o' -delete 2>/dev/null
  find "$OPENVELA_ROOT/packages/ai_agent" -name '*.o' -delete 2>/dev/null
  rm -rf "$SCRIPT_DIR/../build/velaguard"
fi

need_clean=0
if ! compiler_mode_ok; then
  apply_compiler_mode
  need_clean=1
fi

if ! expect_dev_config; then
  echo "[build] ERROR: Kconfig 未保留本预设的必需配置，停止构建。" >&2
  exit 1
fi

write_fw_build_header
python3 "$SCRIPT_DIR/ota/build_trust.py" --output "$CONTEST_ROOT/build/velaguard/vg_ota_trust_build.h"

cd "$NUTTX_ROOT"
if [ "$need_clean" = 1 ] && [ "$MODE" != "--clean" ]; then
  echo "[build] 编译器模式变化，make clean..."
  make clean
fi

echo "[build] 链接 A 槽镜像 version=${FW_VERSION} build=${FW_BUILD_ID}"
make -j"$(nproc)"

mkdir -p "$STAGE_DIR"
cp nuttx.hex "$STAGE_DIR/nuttx-a.hex"
cp nuttx.bin "$STAGE_DIR/nuttx-a.bin"
cp nuttx "$STAGE_DIR/nuttx-a.elf"

SLOT_B_LD="$STAGE_DIR/qspi_flash_slotb.ld"
python3 "$SCRIPT_DIR/gen_slot_b_ld.py" \
  "$NUTTX_ROOT/boards/arm/stm32h7/stm32h750b-dk/scripts/qspi_flash.ld" \
  "$SLOT_B_LD"

echo "[build] 链接 B 槽镜像 0x90800000"
rm -f nuttx nuttx.hex nuttx.bin
make -j"$(nproc)" ARCHSCRIPT="$SLOT_B_LD"
cp nuttx.hex "$STAGE_DIR/nuttx-b.hex"
cp nuttx.bin "$STAGE_DIR/nuttx-b.bin"
cp nuttx "$STAGE_DIR/nuttx-b.elf"

echo "[build] 重新链接 A 槽镜像"
rm -f nuttx nuttx.hex nuttx.bin
make -j"$(nproc)"
cp nuttx.hex "$STAGE_DIR/nuttx.hex"
cp nuttx.bin "$STAGE_DIR/nuttx.bin"
cp nuttx "$STAGE_DIR/nuttx.elf"
cp nuttx.hex "$STAGE_DIR/nuttx-a.hex"
cp nuttx.bin "$STAGE_DIR/nuttx-a.bin"
cp nuttx "$STAGE_DIR/nuttx-a.elf"

python3 "$SCRIPT_DIR/check_fw_images.py" \
  --version "$FW_VERSION" --build-id "$FW_BUILD_ID" \
  --a-elf "$STAGE_DIR/nuttx-a.elf" --a-bin "$STAGE_DIR/nuttx-a.bin" \
  --b-elf "$STAGE_DIR/nuttx-b.elf" --b-bin "$STAGE_DIR/nuttx-b.bin" ||
  { rm -f "$STAGE_DIR/nuttx.hex"; exit 1; }

if [ "$TARGET" = velaguard-lvgl ] && [ "$OTA_FIXTURES" = 0 ]; then
  forbid_material=()
  for name in ca.pem signing-public.pem; do
    material="$STAGE_DIR/ota-network-tests/$name"
    if [ -f "$material" ]; then forbid_material+=(--forbid-material "$material"); fi
  done
  python3 "$SCRIPT_DIR/ota/check_release.py" --config "$NUTTX_ROOT/.config" \
    --trust-header "$CONTEST_ROOT/build/velaguard/vg_ota_trust_build.h" \
    --image "$STAGE_DIR/nuttx-a.bin" --image "$STAGE_DIR/nuttx-b.bin" \
    "${forbid_material[@]}" || { rm -f "$STAGE_DIR/nuttx.hex"; exit 1; }
fi

printf 'version=%s\nbuild_id=%s\n' "$FW_VERSION" "$FW_BUILD_ID" \
  > "$STAGE_DIR/fw-identity.txt"

if [ "$DEBUG" = 1 ]; then
  echo "staged: $STAGE_DIR/nuttx.hex（调试构建 -g3 -Og，TARGET=${TARGET}）"
else
  echo "staged: $STAGE_DIR/nuttx.hex（发布构建，TARGET=${TARGET}）"
fi
echo "staged: A=$STAGE_DIR/nuttx-a.hex B=$STAGE_DIR/nuttx-b.hex"
