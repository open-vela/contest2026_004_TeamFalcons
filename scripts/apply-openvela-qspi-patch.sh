#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OPENVELA_ROOT="${1:-$(cd "$REPO_ROOT/.." && pwd)}"
NUTTX_ROOT="$OPENVELA_ROOT/nuttx"
PATCH_FILE="$SCRIPT_DIR/openvela-qspi-boot-stm32h750b-dk.patch"

if [[ ! -d "$NUTTX_ROOT/.git" && ! -f "$NUTTX_ROOT/.git" ]]; then
  echo "error: NuttX git checkout not found: $NUTTX_ROOT" >&2
  exit 1
fi

if [[ ! -f "$PATCH_FILE" ]]; then
  echo "error: QSPI patch not found: $PATCH_FILE" >&2
  exit 1
fi

qspi_markers_present()
{
  grep -Fq 'if ARCH_BOARD_STM32H750B_DK' \
    "$NUTTX_ROOT/boards/Kconfig" &&
    grep -Fq 'STM32_QSPI_BOOT_BASE 0x90000000' \
      "$NUTTX_ROOT/arch/arm/src/stm32h7/stm32_mpuinit.c" &&
    grep -Fq 'config STM32H750B_DK_QSPI_BOOT' \
      "$NUTTX_ROOT/boards/arm/stm32h7/stm32h750b-dk/Kconfig" &&
    grep -Fq 'LDSCRIPT = qspi_flash.ld' \
      "$NUTTX_ROOT/boards/arm/stm32h7/stm32h750b-dk/scripts/Make.defs" &&
    grep -Fq 'qspi_flash.ld' \
      "$NUTTX_ROOT/boards/arm/stm32h7/stm32h750b-dk/src/CMakeLists.txt" &&
    test -f \
      "$NUTTX_ROOT/boards/arm/stm32h7/stm32h750b-dk/scripts/qspi_flash.ld" &&
    grep -Fq 'case TSIOC_GETMAXPOINTS' \
      "$NUTTX_ROOT/drivers/input/ft5x06.c"
}

if git -C "$NUTTX_ROOT" apply --reverse --check "$PATCH_FILE" >/dev/null 2>&1; then
  echo "QSPI support patch is already applied."
  exit 0
fi

if qspi_markers_present; then
  echo "QSPI support patch is already applied (with later overlapping edits)."
  exit 0
fi

if ! git -C "$NUTTX_ROOT" apply --check "$PATCH_FILE"; then
  echo "error: QSPI patch does not apply cleanly." >&2
  echo "Inspect overlapping changes in $NUTTX_ROOT before retrying." >&2
  exit 1
fi

git -C "$NUTTX_ROOT" apply "$PATCH_FILE"
echo "Applied STM32H750B-DK QSPI support patch."
