#!/usr/bin/env bash
# Map LESP_* identifiers in apps/netutils/esp8266/esp8266.c to esp8266.h names.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OPENVELA_ROOT="${1:-$(cd "$REPO_ROOT/.." && pwd)}"
APPS_ROOT="$OPENVELA_ROOT/apps"
PATCH_FILE="$SCRIPT_DIR/openvela-esp8266-lesp-compat.patch"
SRC="$APPS_ROOT/netutils/esp8266/esp8266.c"

if [[ ! -d "$APPS_ROOT/.git" && ! -f "$APPS_ROOT/.git" ]]; then
  echo "error: apps git checkout not found: $APPS_ROOT" >&2
  exit 1
fi

if [[ ! -f "$SRC" ]]; then
  echo "error: ESP8266 source not found: $SRC" >&2
  exit 1
fi

if grep -Fq '#  define LESP_SSID_SIZE               lespSSID_SIZE' "$SRC"; then
  echo "ESP8266 LESP_* compat macros already present."
  exit 0
fi

if git -C "$APPS_ROOT" apply --reverse --check "$PATCH_FILE" >/dev/null 2>&1; then
  echo "ESP8266 LESP_* compat patch is already applied."
  exit 0
fi

if ! git -C "$APPS_ROOT" apply --check "$PATCH_FILE"; then
  echo "error: ESP8266 LESP_* compat patch does not apply cleanly." >&2
  exit 1
fi

git -C "$APPS_ROOT" apply "$PATCH_FILE"
echo "Applied ESP8266 LESP_* compat patch."
