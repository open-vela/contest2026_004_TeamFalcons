#!/usr/bin/env bash
# Push VelaGuard public-repo branches and open PRs via GitHub CLI.
# Prerequisite: gh auth login (use the contest GitHub account FoLeaf)
set -euo pipefail

if ! gh auth status >/dev/null 2>&1; then
  echo "error: run 'gh auth login' first" >&2
  exit 1
fi

OPENVELA_ROOT="${OPENVELA_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}"
GITHUB_USER="${GITHUB_USER:-FoLeaf}"
BASE_BRANCH="dev-ai-contest-2026"

ensure_fork() {
  local upstream="$1"
  gh repo fork "$upstream" --clone=false 2>/dev/null || true
}

ensure_remote() {
  local dir="$1"
  local name="$2"
  local url="https://github.com/${GITHUB_USER}/${name}.git"
  if ! git -C "$dir" remote get-url foleaf >/dev/null 2>&1; then
    git -C "$dir" remote add foleaf "$url"
  fi
}

push_branch() {
  local dir="$1"
  local branch="$2"
  git -C "$dir" push -u foleaf "$branch"
}

create_pr() {
  local upstream="$1"
  local branch="$2"
  local title="$3"
  local body="$4"
  gh pr create \
    --repo "$upstream" \
    --base "$BASE_BRANCH" \
    --head "${GITHUB_USER}:${branch}" \
    --title "$title" \
    --body "$body"
}

# --- nuttx (5 PRs) ---
NUTTX="$OPENVELA_ROOT/nuttx"
ensure_fork open-vela/nuttx
ensure_remote "$NUTTX" nuttx

declare -a NUTTX_BRANCHES=(
  "velaguard/qspi-boot-stm32h750b-dk"
  "velaguard/board-and-defconfigs"
  "velaguard/eth-mii-stm32h750b-dk"
  "velaguard/display-acceleration-stm32h750b-dk"
  "velaguard/ui-performance-stm32h750b-dk"
)

declare -a NUTTX_TITLES=(
  "boards/stm32h750b-dk: add QSPI XIP boot support"
  "boards/stm32h750b-dk: add VelaGuard extension board and defconfigs"
  "arch/stm32h7: improve Ethernet MII PHY polling on STM32H750B-DK"
  "arch/stm32h7: LTDC display acceleration for STM32H750B-DK"
  "drivers/input: improve ft5x06 touch polling performance"
)

for i in "${!NUTTX_BRANCHES[@]}"; do
  b="${NUTTX_BRANCHES[$i]}"
  echo "[nuttx] push $b"
  push_branch "$NUTTX" "$b"
  echo "[nuttx] create PR for $b"
  create_pr open-vela/nuttx "$b" "${NUTTX_TITLES[$i]}" "Contest: contest2026_004 Team Falcons / VelaGuard. See FoLeaf/contest2026_004_TeamFalcons."
done

# --- apps ---
APPS="$OPENVELA_ROOT/apps"
ensure_fork open-vela/nuttx-apps
ensure_remote "$APPS" nuttx-apps
push_branch "$APPS" velaguard/netinit-esp8266
create_pr open-vela/nuttx-apps velaguard/netinit-esp8266 \
  "netutils: carrier poll DHCP renew and ESP8266 LESP compat" \
  "Contest: contest2026_004 Team Falcons / VelaGuard."

# --- MQTT-C ---
MQTTC="$OPENVELA_ROOT/apps/netutils/mqttc/MQTT-C"
ensure_fork open-vela/apps_netutils_mqttc_MQTT-C
ensure_remote "$MQTTC" apps_netutils_mqttc_MQTT-C
push_branch "$MQTTC" velaguard/mqtt-pal-hook
create_pr open-vela/apps_netutils_mqttc_MQTT-C velaguard/mqtt-pal-hook \
  "mqtt_pal: add weak hooks for tagged LESP send/recv" \
  "Contest: contest2026_004 Team Falcons / VelaGuard."

echo "Done. Record PR URLs in .trellis/tasks/08-28-nuttx-upstream-pr-submission/submission-checklist.md"
