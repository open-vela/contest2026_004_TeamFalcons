#!/usr/bin/env bash
# Prepare NuttX headers so VS Code Remote - WSL IntelliSense can complete
# and jump into openvela/NuttX APIs before a full firmware build.
#
# This script only:
#   1) configures a board defconfig (default stm32h750b-dk:lvgl)
#   2) generates include/nuttx/config.h via mkconfig
#
# It does NOT compile firmware, flash hardware, or apply product patches.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
OPENVELA_ROOT="$(cd "${OPENVELA_ROOT:-${REPO_DIR}/..}" && pwd)"
BOARD_CONFIG="${BOARD_CONFIG:-stm32h750b-dk:lvgl}"
NUTTX_DIR="${OPENVELA_ROOT}/nuttx"
GCC_BIN="${OPENVELA_ROOT}/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin/arm-none-eabi-gcc"

if [[ ! -d "${NUTTX_DIR}" ]]; then
  echo "error: nuttx not found at ${NUTTX_DIR}" >&2
  echo "  Open the contest repo inside the openvela workspace (parent must contain nuttx/)." >&2
  exit 1
fi

if [[ ! -x "${GCC_BIN}" ]]; then
  echo "error: ARM GCC not found at ${GCC_BIN}" >&2
  echo "  Sync the openvela prebuilts tree before using IntelliSense." >&2
  exit 1
fi

echo "openvela root : ${OPENVELA_ROOT}"
echo "board config  : ${BOARD_CONFIG}"
echo "ARM GCC       : ${GCC_BIN}"

cd "${NUTTX_DIR}"

need_configure=0
if [[ ! -f .config ]]; then
  need_configure=1
elif ! grep -q "CONFIG_BASE_DEFCONFIG=\"${BOARD_CONFIG}\"" .config 2>/dev/null; then
  # Keep an existing product .config; only force-switch when empty/missing.
  current="$(grep '^CONFIG_BASE_DEFCONFIG=' .config 2>/dev/null || true)"
  echo "note: existing .config keeps ${current:-unknown}; not overwriting."
  echo "      set BOARD_CONFIG and re-run with FORCE_CONFIGURE=1 to switch."
fi

if [[ "${FORCE_CONFIGURE:-0}" == "1" ]]; then
  need_configure=1
fi

if [[ "${need_configure}" == "1" ]]; then
  echo "Configuring NuttX (${BOARD_CONFIG}) for IntelliSense headers..."
  ./tools/configure.sh -l "${BOARD_CONFIG}"
else
  echo "Reusing existing nuttx/.config"
fi

if [[ ! -L include/arch ]]; then
  echo "error: include/arch symlink missing after configure" >&2
  exit 1
fi

echo "Generating include/nuttx/config.h..."
make include/nuttx/config.h

if [[ ! -f include/nuttx/config.h ]]; then
  echo "error: failed to generate include/nuttx/config.h" >&2
  exit 1
fi

# Smoke-check the contest hello_app against the real NuttX headers.
HELLO_APP="${REPO_DIR}/app/hello_app/hello_app_main.c"
if [[ -f "${HELLO_APP}" ]]; then
  echo "Syntax-checking ${HELLO_APP}..."
  "${GCC_BIN}" -fsyntax-only \
    -mcpu=cortex-m7 -mthumb -ffreestanding -std=gnu11 -D__NuttX__ \
    -I"${NUTTX_DIR}/include" \
    -I"${OPENVELA_ROOT}/apps/include" \
    -I"${REPO_DIR}/app/hello_app" \
    -include "${NUTTX_DIR}/include/nuttx/config.h" \
    "${HELLO_APP}"
fi

cat <<EOF

IntelliSense prerequisites are ready.

Next in VS Code (Remote - WSL):
  1. Install ms-vscode.cpptools into the WSL side if prompted
  2. Command Palette → "C/C++: Reset IntelliSense Database"
  3. Command Palette → "Developer: Reload Window"
  4. Open app/hello_app/hello_app_main.c and try Go to Definition on printf

Architecture reminder:
  edit/IntelliSense → VS Code Remote - WSL
  compile          → WSL openvela toolchain (later)
  flash/debug      → Windows CubeProgrammer + OpenOCD (later)
EOF
