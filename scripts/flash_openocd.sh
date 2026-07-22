#!/usr/bin/env bash
set -euo pipefail

HEX_PATH="/home/debian19y/openvela/nuttx/nuttx.hex"
NUTTX_DIR="/home/debian19y/openvela/nuttx"
OPENVELA_DIR="/home/debian19y/openvela"
WIN_HEX_PATH="/mnt/c/Users/19y/AppData/Local/Temp/openvela_nuttx.hex"
OPENOCD_BIN="/mnt/d/Develop/xpack-openocd-0.12.0-7/bin/openocd.exe"
OPENOCD_SCRIPTS="/mnt/d/Develop/xpack-openocd-0.12.0-7/openocd/scripts"

setup_build_env() {
  export PATH="$OPENVELA_DIR/prebuilts/tools/python/bin:$OPENVELA_DIR/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:$OPENVELA_DIR/prebuilts/build-tools/linux-x86_64/bin:$PATH"
  export PYTHONPATH="$OPENVELA_DIR/prebuilts/tools/python/dist-packages/kconfiglib:$OPENVELA_DIR/prebuilts/tools/python/dist-packages:${PYTHONPATH:-}"
}

build_firmware() {
  if [[ ! -d "$NUTTX_DIR" ]]; then
    echo "error: NuttX directory not found: $NUTTX_DIR" >&2
    exit 1
  fi

  echo "==> Building openvela firmware"
  setup_build_env
  make -C "$NUTTX_DIR" -j"$(nproc)"
}

flash_firmware() {
  if [[ ! -f "$HEX_PATH" ]]; then
    echo "error: firmware not found after build: $HEX_PATH" >&2
    exit 1
  fi

  if [[ ! -x "$OPENOCD_BIN" ]]; then
    echo "error: OpenOCD not found or not executable: $OPENOCD_BIN" >&2
    exit 1
  fi

  echo "==> Flashing $HEX_PATH"
  cp "$HEX_PATH" "$WIN_HEX_PATH"

  SCRIPT_WIN="$(wslpath -w "$OPENOCD_SCRIPTS")"

  "$OPENOCD_BIN" \
    -s "$SCRIPT_WIN" \
    -f interface/stlink.cfg \
    -f target/stm32h7x.cfg \
    -c "transport select swd" \
    -c "program C:/Users/19y/AppData/Local/Temp/openvela_nuttx.hex verify reset exit"
}

build_firmware
flash_firmware
