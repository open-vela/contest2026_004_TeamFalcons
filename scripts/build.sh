#!/usr/bin/env bash
# VelaGuard 固件构建 + 暂存烧录产物。
# 日常入口是本仓 scripts/build.sh，不是父目录 openvela 的 ./build.sh。
#
# 用法：
#   bash scripts/build.sh [TARGET] [--clean] [--debug]
#   TARGET: net | min | lvgl   （默认 net）
#
#   示例：
#     bash scripts/build.sh               # 增量发布构建（net，无调试符号）
#     bash scripts/build.sh --debug       # 增量调试构建（-g3 -Og，F5 走这条）
#     bash scripts/build.sh --clean       # Rebuild 复位到 velaguard-net 发布配置
#     bash scripts/build.sh --clean --debug
#
# 产物：
#   nuttx/nuttx.hex、nuttx/nuttx.bin
#   .debug/nuttx.{hex,bin,elf} + qspi_bootstub.hex
#   Download 固定烧 .debug 里的这两份 HEX。
#
# Rebuild（--clean）会 configure.sh -E 复位到所选预设：未 savedefconfig
# 的本地 nuttx/.config / menuconfig 改动会被丢掉；参赛仓 defconfig 补丁保留。
#
# 防呆：构建开始前先清空 .debug 里的旧主镜像，构建失败时 Download 会因
# 缺少 nuttx.hex 直接报错，而不是把上一次的旧固件烧到板子上。
set -euo pipefail

TARGET="net"
MODE="build"
DEBUG=0

for arg in "$@"; do
  case "$arg" in
    --debug)
      DEBUG=1
      ;;
    --clean)
      MODE="--clean"
      ;;
    net|min|lvgl)
      TARGET="$arg"
      ;;
    *)
      echo "error: 未知参数 '$arg'（用法: build.sh [net|min|lvgl] [--clean] [--debug]）" >&2
      exit 1
      ;;
  esac
done

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CONTEST_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OPENVELA_ROOT="$(cd "$CONTEST_ROOT/.." && pwd)"
NUTTX_ROOT="$OPENVELA_ROOT/nuttx"
STAGE_DIR="$CONTEST_ROOT/.debug"

export PATH="$OPENVELA_ROOT/prebuilts/tools/python/bin:$OPENVELA_ROOT/prebuilts/tools/linux/x86_64:$OPENVELA_ROOT/prebuilts/kconfig-frontends/bin:$OPENVELA_ROOT/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:$OPENVELA_ROOT/prebuilts/build-tools/linux-x86_64/bin:$PATH"
export PYTHONPATH="$OPENVELA_ROOT/prebuilts/tools/python/dist-packages/kconfiglib:$OPENVELA_ROOT/prebuilts/tools/python/dist-packages:${PYTHONPATH:-}"

case "$TARGET" in
  net)
    DEFCONFIG="velaguard-net"
    ;;
  min)
    DEFCONFIG="velaguard-min"
    ;;
  lvgl)
    DEFCONFIG="lvgl"
    ;;
  *)
    echo "error: 未知目标 '$TARGET'（可选 net|min|lvgl）" >&2
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

    net)
      grep -q 'CONFIG_INIT_ENTRYPOINT="velaguard_app_main"' "$NUTTX_ROOT/.config" &&
        grep -q 'CONFIG_INIT_ENTRYNAME="velaguard_app_main"' "$NUTTX_ROOT/.config" &&
        ! grep -q '^CONFIG_GRAPHICS_LVGL=' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_NET=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_NETUTILS_MQTTC=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_NETUTILS_ESP8266=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_VG_NET_FAILOVER=y' "$NUTTX_ROOT/.config" &&
        grep -q '^CONFIG_VG_BRINGUP_TOOLS=y' "$NUTTX_ROOT/.config"
      ;;

    lvgl)
      grep -q '^CONFIG_GRAPHICS_LVGL=y' "$NUTTX_ROOT/.config" &&
        grep -q 'CONFIG_INIT_ENTRYPOINT="nsh_main"' "$NUTTX_ROOT/.config"
      ;;
  esac
}

apply_target_patches()
{
  echo "[build] ensure contest app link..."
  bash "$SCRIPT_DIR/ensure-openvela-links.sh" "$OPENVELA_ROOT"

  # packages/demos/Kconfig 是生成文件，可能早于新创建的 manifest link。
  # configure 前按 apps 惯例重生一次，干净树才能看到 VelaGuard。
  if [ -d "$OPENVELA_ROOT/packages/demos" ]; then
    echo "[build] regenerate packages/demos/Kconfig..."
    (
      cd "$OPENVELA_ROOT/packages/demos"
      "$OPENVELA_ROOT/apps/tools/mkkconfig.sh" -m Demos
    )
  fi

  echo "[build] apply QSPI patch..."
  bash "$SCRIPT_DIR/apply-openvela-qspi-patch.sh" "$OPENVELA_ROOT"

  case "$TARGET" in
    net)
      echo "[build] apply net patches (eth-mii + velaguard-net defconfig)..."
      bash "$SCRIPT_DIR/apply-openvela-eth-mii-patch.sh" "$OPENVELA_ROOT"
      bash "$SCRIPT_DIR/apply-openvela-velaguard-net-defconfig-patch.sh" "$OPENVELA_ROOT"
      bash "$SCRIPT_DIR/apply-openvela-velaguard-net-esp8266-patch.sh" "$OPENVELA_ROOT"
      bash "$SCRIPT_DIR/apply-openvela-esp8266-lesp-compat-patch.sh" "$OPENVELA_ROOT"
      bash "$SCRIPT_DIR/apply-openvela-mqttc-pal-hook-patch.sh" "$OPENVELA_ROOT"
      ;;
    min)
      echo "[build] apply min patches (pwm-tim15 + velaguard-min defconfig)..."
      bash "$SCRIPT_DIR/apply-openvela-pwm-tim15-patch.sh" "$OPENVELA_ROOT"
      bash "$SCRIPT_DIR/apply-openvela-velaguard-min-defconfig-patch.sh" "$OPENVELA_ROOT"
      ;;
    lvgl)
      echo "[build] apply lvgl patches (ui-performance + display-acceleration)..."
      bash "$SCRIPT_DIR/apply-openvela-ui-performance-patch.sh" "$OPENVELA_ROOT"
      bash "$SCRIPT_DIR/apply-openvela-display-acceleration-patch.sh" "$OPENVELA_ROOT"
      ;;
  esac
}

ensure_bootstub()
{
  mkdir -p "$STAGE_DIR"
  if [ "$MODE" = "--clean" ] || [ ! -f "$STAGE_DIR/qspi_bootstub.hex" ]; then
    echo "[build] 构建 QSPI boot stub..."
    bash "$SCRIPT_DIR/qspi_boot_stub/build_bootstub.sh" "$STAGE_DIR"
  fi
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

apply_target_patches
ensure_bootstub

# 防呆：清掉旧烧录产物，失败时 Download 报"找不到 nuttx.hex"而不是烧旧固件
rm -f "$STAGE_DIR/nuttx.hex" "$STAGE_DIR/nuttx.bin" "$STAGE_DIR/nuttx.elf"

if [ "$MODE" = "--clean" ] || ! expect_dev_config; then
  echo "[build] 重新配置 ${DEFCONFIG} 预设（distclean）..."
  "$NUTTX_ROOT/tools/configure.sh" -E -e "stm32h750b-dk:${DEFCONFIG}"
fi

need_clean=0
if ! compiler_mode_ok; then
  apply_compiler_mode
  need_clean=1
fi

cd "$NUTTX_ROOT"
if [ "$need_clean" = 1 ] && [ "$MODE" != "--clean" ]; then
  echo "[build] 编译器模式变化，make clean..."
  make clean
fi
make -j"$(nproc)"

mkdir -p "$STAGE_DIR"
cp nuttx.hex nuttx.bin "$STAGE_DIR/"
cp nuttx "$STAGE_DIR/nuttx.elf"
if [ "$DEBUG" = 1 ]; then
  echo "staged: $STAGE_DIR/nuttx.hex（调试构建 -g3 -Og，TARGET=${TARGET}）"
else
  echo "staged: $STAGE_DIR/nuttx.hex（发布构建，TARGET=${TARGET}）"
fi
