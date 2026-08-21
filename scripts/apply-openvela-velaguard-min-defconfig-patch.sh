#!/usr/bin/env bash
# Apply the stm32h750b-dk:velaguard-min defconfig (idempotent).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OPENVELA_ROOT="${1:-$(cd "$REPO_ROOT/.." && pwd)}"
NUTTX_ROOT="$OPENVELA_ROOT/nuttx"
PATCH_FILE="$SCRIPT_DIR/openvela-velaguard-min-defconfig.patch"
DEFCONFIG="$NUTTX_ROOT/boards/arm/stm32h7/stm32h750b-dk/configs/velaguard-min/defconfig"

if [[ ! -d "$NUTTX_ROOT/.git" && ! -f "$NUTTX_ROOT/.git" ]]; then
  echo "error: NuttX git checkout not found: $NUTTX_ROOT" >&2
  exit 1
fi

if [[ ! -f "$PATCH_FILE" ]]; then
  echo "error: velaguard-min defconfig patch not found: $PATCH_FILE" >&2
  exit 1
fi

defconfig_markers_present()
{
  test -f "$DEFCONFIG" &&
    grep -Fq 'CONFIG_ARCH_BOARD_STM32H750B_DK' "$DEFCONFIG" &&
    grep -Fq 'CONFIG_INIT_ENTRYPOINT="velaguard_app_main"' "$DEFCONFIG" &&
    grep -Fq 'CONFIG_VG_BRINGUP_TOOLS=y' "$DEFCONFIG"
}

if git -C "$NUTTX_ROOT" apply --reverse --check "$PATCH_FILE" >/dev/null 2>&1; then
  echo "VelaGuard minimal defconfig patch is already applied."
  exit 0
fi

if defconfig_markers_present; then
  echo "VelaGuard minimal defconfig patch is already applied (with later overlapping edits)."
  exit 0
fi

if ! git -C "$NUTTX_ROOT" apply --check "$PATCH_FILE"; then
  echo "error: VelaGuard minimal defconfig patch does not apply cleanly." >&2
  echo "Inspect overlapping changes in $NUTTX_ROOT before retrying." >&2
  exit 1
fi

git -C "$NUTTX_ROOT" apply "$PATCH_FILE"
echo "Applied stm32h750b-dk:velaguard-min defconfig patch."
