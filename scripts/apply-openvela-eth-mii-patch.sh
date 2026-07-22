#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OPENVELA_ROOT="${1:-$(cd "$REPO_ROOT/.." && pwd)}"
NUTTX_ROOT="$OPENVELA_ROOT/nuttx"
APPS_ROOT="$OPENVELA_ROOT/apps"
NUTTX_PATCH="$SCRIPT_DIR/openvela-eth-mii-stm32h750b-dk.patch"
APPS_PATCH="$SCRIPT_DIR/openvela-netinit-carrier-poll.patch"

apply_patch_once() {
  local checkout="$1"
  local patch_file="$2"
  local label="$3"

  if [[ ! -d "$checkout/.git" && ! -f "$checkout/.git" ]]; then
    echo "error: $label git checkout not found: $checkout" >&2
    return 1
  fi

  if [[ ! -f "$patch_file" ]]; then
    echo "error: $label patch not found: $patch_file" >&2
    return 1
  fi

  if git -C "$checkout" apply --reverse --check "$patch_file" \
      >/dev/null 2>&1; then
    echo "$label patch is already applied."
    return 0
  fi

  if ! git -C "$checkout" apply --check "$patch_file"; then
    echo "error: $label patch does not apply cleanly." >&2
    echo "Inspect overlapping changes in $checkout before retrying." >&2
    return 1
  fi

  git -C "$checkout" apply "$patch_file"
  echo "Applied $label patch."
}

apply_patch_once "$NUTTX_ROOT" "$NUTTX_PATCH" \
  "STM32H750B-DK MII/QSPI carrier"
apply_patch_once "$APPS_ROOT" "$APPS_PATCH" \
  "netinit carrier/DHCP reconnect"
