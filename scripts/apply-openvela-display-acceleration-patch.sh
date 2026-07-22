#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OPENVELA_ROOT="${1:-$(cd "$REPO_ROOT/.." && pwd)}"
NUTTX_ROOT="$OPENVELA_ROOT/nuttx"
PATCH_FILE="$SCRIPT_DIR/openvela-display-acceleration-stm32h750b-dk.patch"

if [[ ! -d "$NUTTX_ROOT/.git" && ! -f "$NUTTX_ROOT/.git" ]]; then
  echo "error: NuttX git checkout not found: $NUTTX_ROOT" >&2
  exit 1
fi

if git -C "$NUTTX_ROOT" apply --reverse --check "$PATCH_FILE" \
  >/dev/null 2>&1; then
  echo "Display acceleration patch is already applied."
  exit 0
fi

if grep -Fq 'BOARD_SDRAM2_HEAP_OFFSET' \
     "$NUTTX_ROOT/boards/arm/stm32h7/stm32h750b-dk/include/board.h" &&
   grep -Fq 'static volatile bool g_flip_pending' \
     "$NUTTX_ROOT/arch/arm/src/stm32h7/stm32_ltdc.c"; then
  echo "Display acceleration patch is already applied (with overlaps)."
  exit 0
fi

if ! git -C "$NUTTX_ROOT" apply --check "$PATCH_FILE"; then
  echo "error: display acceleration patch does not apply cleanly." >&2
  exit 1
fi

git -C "$NUTTX_ROOT" apply "$PATCH_FILE"
echo "Applied STM32H750B-DK display acceleration patch."
