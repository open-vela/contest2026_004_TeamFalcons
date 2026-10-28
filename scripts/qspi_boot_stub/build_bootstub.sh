#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
OPENVELA_ROOT="$(cd "$REPO_ROOT/.." && pwd)"
OUT_DIR="${1:-$SCRIPT_DIR/build}"

export PATH="$OPENVELA_ROOT/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:$PATH"

CC="${CC:-arm-none-eabi-gcc}"
OBJCOPY="${OBJCOPY:-arm-none-eabi-objcopy}"
SIZE="${SIZE:-arm-none-eabi-size}"

mkdir -p "$OUT_DIR"

BOOT_CORE="$REPO_ROOT/boot/ota_core"

"$CC" \
  -mcpu=cortex-m7 \
  -mthumb \
  -mfpu=fpv5-d16 \
  -mfloat-abi=hard \
  -Os \
  -ffreestanding \
  -fno-common \
  -fno-builtin \
  -fdata-sections \
  -ffunction-sections \
  -Wall \
  -Wextra \
  -Werror \
  -DVG_OTA_STUB_FIXTURES="${OTA_FIXTURES:-0}" \
  -nostartfiles \
  -nostdlib \
  -I"$SCRIPT_DIR" \
  -I"$BOOT_CORE" \
  -Wl,--build-id=none \
  -Wl,--gc-sections \
  -Wl,-Map="$OUT_DIR/qspi_bootstub.map" \
  -T "$SCRIPT_DIR/stm32h750b_qspi_bootstub.ld" \
  "$SCRIPT_DIR/stm32h750b_qspi_bootstub.c" \
  "$SCRIPT_DIR/stm32h750b_emmc.c" \
  "$BOOT_CORE/vg_ota_fat.c" \
  "$BOOT_CORE/vg_ota_bcb.c" \
  "$BOOT_CORE/vg_ota_boot.c" \
  "$BOOT_CORE/vg_ota_install.c" \
  "$BOOT_CORE/vg_ota_manifest.c" \
  "$BOOT_CORE/vg_ota_sha256.c" \
  -o "$OUT_DIR/qspi_bootstub.elf"

"$OBJCOPY" -O ihex "$OUT_DIR/qspi_bootstub.elf" "$OUT_DIR/qspi_bootstub.hex"
"$OBJCOPY" -O binary "$OUT_DIR/qspi_bootstub.elf" "$OUT_DIR/qspi_bootstub.bin"
"$SIZE" "$OUT_DIR/qspi_bootstub.elf"
