#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OPENVELA_ROOT="${1:-$(cd "$REPO_ROOT/.." && pwd)}"
MQTTC_ROOT="$OPENVELA_ROOT/apps/netutils/mqttc/MQTT-C"
PATCH_FILE="$SCRIPT_DIR/openvela-mqttc-pal-hook.patch"
PAL_SRC="$MQTTC_ROOT/src/mqtt_pal.c"

if [[ ! -d "$MQTTC_ROOT/.git" && ! -f "$MQTTC_ROOT/.git" ]]; then
  echo "error: MQTT-C git checkout not found: $MQTTC_ROOT" >&2
  exit 1
fi

if [[ ! -f "$PAL_SRC" ]]; then
  echo "error: mqtt_pal.c not found: $PAL_SRC" >&2
  exit 1
fi

# Content marker: survives comment-only drift in the patch text.
if grep -Fq 'vg_mqtt_pal_try_sendall' "$PAL_SRC" &&
   grep -Fq 'vg_mqtt_pal_try_recvall' "$PAL_SRC"; then
  echo "MQTT-C pal hook patch is already applied."
  exit 0
fi

if git -C "$MQTTC_ROOT" apply --reverse --check "$PATCH_FILE" >/dev/null 2>&1; then
  echo "MQTT-C pal hook patch is already applied."
  exit 0
fi

if ! git -C "$MQTTC_ROOT" apply --check "$PATCH_FILE"; then
  echo "error: MQTT-C pal hook patch does not apply cleanly." >&2
  exit 1
fi

git -C "$MQTTC_ROOT" apply "$PATCH_FILE"
echo "Applied MQTT-C pal hook patch."
