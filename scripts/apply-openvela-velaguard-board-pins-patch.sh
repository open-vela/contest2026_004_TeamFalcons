#!/usr/bin/env bash
# Apply VelaGuard expansion-board pinmux + bringup (+ TIM15 PWM) to stm32h750b-dk.
# Idempotent: safe to re-run when already applied or when overlapping local edits exist.
#
# Covers:
#   board.h     — USART2 (ESP-01), UART7 RS485 DIR, ESP EN/RST, TIM15 CH2
#   bringup.c   — /dev/rs485 symlink, ESP GPIO init, PWM setup
#   Makefile    — build stm32_pwm.c when CONFIG_PWM=y
#   stm32_pwm.c — /dev/pwm0 registration (new file)
#   stm32h750b-dk.h — stm32_pwm_setup() prototype

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OPENVELA_ROOT="${1:-$(cd "$REPO_ROOT/.." && pwd)}"
NUTTX_ROOT="$OPENVELA_ROOT/nuttx"
PATCH_FILE="$SCRIPT_DIR/openvela-velaguard-board-pins.patch"
BOARD_H="$NUTTX_ROOT/boards/arm/stm32h7/stm32h750b-dk/include/board.h"
BRINGUP="$NUTTX_ROOT/boards/arm/stm32h7/stm32h750b-dk/src/stm32_bringup.c"
PWM_C="$NUTTX_ROOT/boards/arm/stm32h7/stm32h750b-dk/src/stm32_pwm.c"

if [[ ! -d "$NUTTX_ROOT/.git" && ! -f "$NUTTX_ROOT/.git" ]]; then
  echo "error: NuttX git checkout not found: $NUTTX_ROOT" >&2
  exit 1
fi

if [[ ! -f "$PATCH_FILE" ]]; then
  echo "error: VelaGuard board pins patch not found: $PATCH_FILE" >&2
  exit 1
fi

board_pins_markers_present()
{
  test -f "$BOARD_H" &&
    grep -Fq 'GPIO_USART2_TX' "$BOARD_H" &&
    grep -Fq 'GPIO_UART7_RS485_DIR' "$BOARD_H" &&
    grep -Fq 'GPIO_ESP_EN' "$BOARD_H" &&
    grep -Fq 'GPIO_TIM15_CH2OUT' "$BOARD_H" &&
    test -f "$BRINGUP" &&
    grep -Fq 'symlink("/dev/ttyS2", "/dev/rs485")' "$BRINGUP" &&
    test -f "$PWM_C" &&
    grep -Fq 'stm32_pwm.c' \
      "$NUTTX_ROOT/boards/arm/stm32h7/stm32h750b-dk/src/Makefile"
}

if git -C "$NUTTX_ROOT" apply --reverse --check "$PATCH_FILE" >/dev/null 2>&1; then
  echo "VelaGuard board pins patch is already applied."
  exit 0
fi

if board_pins_markers_present; then
  echo "VelaGuard board pins patch is already applied (with later overlapping edits)."
  exit 0
fi

if ! git -C "$NUTTX_ROOT" apply --check "$PATCH_FILE"; then
  echo "error: VelaGuard board pins patch does not apply cleanly." >&2
  echo "Inspect overlapping changes in $NUTTX_ROOT before retrying." >&2
  exit 1
fi

git -C "$NUTTX_ROOT" apply "$PATCH_FILE"
echo "Applied VelaGuard stm32h750b-dk board pins / bringup patch."
