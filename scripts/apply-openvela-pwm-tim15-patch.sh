#!/usr/bin/env bash
# Apply the STM32 TIM15 CH2 output guard typo fix (idempotent).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OPENVELA_ROOT="${1:-$(cd "$REPO_ROOT/.." && pwd)}"
NUTTX_ROOT="$OPENVELA_ROOT/nuttx"
PATCH_FILE="$SCRIPT_DIR/openvela-pwm-tim15-fix.patch"
PWM_SRC="$NUTTX_ROOT/arch/arm/src/stm32h7/stm32_pwm.c"

if [[ ! -d "$NUTTX_ROOT/.git" && ! -f "$NUTTX_ROOT/.git" ]]; then
  echo "error: NuttX git checkout not found: $NUTTX_ROOT" >&2
  exit 1
fi

if [[ ! -f "$PATCH_FILE" ]]; then
  echo "error: TIM15 fix patch not found: $PATCH_FILE" >&2
  exit 1
fi

pwm_tim15_markers_present()
{
  test -f "$PWM_SRC" &&
    grep -Fq 'CONFIG_STM32H7_TIM15_CH2OUT' "$PWM_SRC"
}

if git -C "$NUTTX_ROOT" apply --reverse --check "$PATCH_FILE" >/dev/null 2>&1; then
  echo "STM32 PWM TIM15 fix patch is already applied."
  exit 0
fi

if pwm_tim15_markers_present; then
  echo "STM32 PWM TIM15 fix patch is already applied (with later overlapping edits)."
  exit 0
fi

if ! git -C "$NUTTX_ROOT" apply --check "$PATCH_FILE"; then
  echo "error: STM32 PWM TIM15 fix patch does not apply cleanly." >&2
  echo "Inspect overlapping changes in $NUTTX_ROOT before retrying." >&2
  exit 1
fi

git -C "$NUTTX_ROOT" apply "$PATCH_FILE"
echo "Applied STM32 PWM TIM15 fix patch."
