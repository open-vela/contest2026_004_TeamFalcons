#!/usr/bin/env bash
# Regenerate ../nuttx/compile_commands.json for clangd IntelliSense.
#
# NuttX incremental builds do not re-run the compiler, so bear only sees a
# handful of commands unless we clean first. This script runs `make clean`
# then `bear -- make` with the current .config (no distclean / reconfigure).
#
# Usage (from contest repo root):
#   ./scripts/refresh_compile_commands.sh
#
# After it finishes in Cursor:
#   Command Palette → clangd: Restart language server
#   Command Palette → Developer: Reload Window

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
OPENVELA_ROOT="$(cd "${OPENVELA_ROOT:-${REPO_DIR}/..}" && pwd)"
NUTTX_DIR="${OPENVELA_ROOT}/nuttx"
JOBS="${JOBS:-$(nproc)}"

if [[ ! -d "${NUTTX_DIR}" ]]; then
  echo "error: nuttx not found at ${NUTTX_DIR}" >&2
  exit 1
fi

if ! command -v bear >/dev/null 2>&1; then
  echo "error: bear not installed (apt install bear)" >&2
  exit 1
fi

if [[ ! -f "${NUTTX_DIR}/.config" ]]; then
  echo "error: ${NUTTX_DIR}/.config missing; run scripts/build.sh first" >&2
  exit 1
fi

export PATH="${OPENVELA_ROOT}/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:${PATH}"

echo "openvela root : ${OPENVELA_ROOT}"
echo "nuttx dir     : ${NUTTX_DIR}"
echo "jobs          : ${JOBS}"
echo "Cleaning object files (keeps .config)..."
make -C "${NUTTX_DIR}" clean

echo "Rebuilding with bear → compile_commands.json (this takes a few minutes)..."
rm -f "${NUTTX_DIR}/compile_commands.json"
bear -- make -C "${NUTTX_DIR}" -j"${JOBS}"

python3 - "${NUTTX_DIR}/compile_commands.json" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
data = json.loads(path.read_text())
needles = ("mqtt_pal.c", "vg_mqtt_session.c")
found = {n: any(n in e["file"] for e in data) for n in needles}
print(f"compile_commands.json: {len(data)} entries")
for n, ok in found.items():
    print(f"  {n}: {'ok' if ok else 'MISSING'}")
if not all(found.values()):
    raise SystemExit("error: compile_commands.json looks incomplete")
PY

cat <<EOF

Done. Restart clangd in Cursor:
  1. Command Palette → clangd: Restart language server
  2. Command Palette → Developer: Reload Window
EOF
