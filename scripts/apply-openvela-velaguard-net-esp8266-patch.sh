#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OPENVELA_ROOT="${1:-$(cd "$REPO_ROOT/.." && pwd)}"
NUTTX_ROOT="$OPENVELA_ROOT/nuttx"
PATCH_FILE="$SCRIPT_DIR/openvela-velaguard-net-esp8266.patch"
DEFCONFIG="$NUTTX_ROOT/boards/arm/stm32h7/stm32h750b-dk/configs/velaguard-net/defconfig"

if [[ ! -d "$NUTTX_ROOT/.git" && ! -f "$NUTTX_ROOT/.git" ]]; then
  echo "error: NuttX git checkout not found: $NUTTX_ROOT" >&2
  exit 1
fi

if [[ ! -f "$DEFCONFIG" ]]; then
  echo "error: apply velaguard-net defconfig first: $DEFCONFIG" >&2
  exit 1
fi

if grep -Fq 'CONFIG_NETUTILS_ESP8266=y' "$DEFCONFIG" &&
   grep -Fq 'CONFIG_VG_NET_FAILOVER=y' "$DEFCONFIG"; then
  echo "VelaGuard net ESP8266 failover symbols already present."
  exit 0
fi

if git -C "$NUTTX_ROOT" apply --reverse --check "$PATCH_FILE" >/dev/null 2>&1; then
  echo "VelaGuard net ESP8266 patch is already applied."
  exit 0
fi

if ! git -C "$NUTTX_ROOT" apply --check "$PATCH_FILE"; then
  echo "error: VelaGuard net ESP8266 patch does not apply cleanly." >&2
  exit 1
fi

git -C "$NUTTX_ROOT" apply "$PATCH_FILE"
echo "Applied velaguard-net ESP8266/failover defconfig patch."
