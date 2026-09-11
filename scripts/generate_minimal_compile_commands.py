#!/usr/bin/env python3
"""Generate a lightweight compile_commands.json for clangd without bear.

NuttX incremental builds do not re-run the compiler, so bear needs a full
rebuild. This script writes compile commands for contest-tree sources using the
same flags as .vscode/c_cpp_properties.json so IntelliSense works immediately
after configure/build.

For full accuracy (every NuttX translation unit), install bear and run:
  ./scripts/refresh_compile_commands.sh
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_DIR = SCRIPT_DIR.parent
OPENVELA_ROOT = Path(
    __import__("os").environ.get("OPENVELA_ROOT", str(REPO_DIR.parent))
).resolve()
NUTTX_DIR = OPENVELA_ROOT / "nuttx"
GCC = (
    OPENVELA_ROOT
    / "prebuilts/gcc/linux-x86_64/arm-none-eabi/bin/arm-none-eabi-gcc"
)
CONFIG_H = NUTTX_DIR / "include/nuttx/config.h"
OUT_PATH = NUTTX_DIR / "compile_commands.json"

INCLUDE_DIRS = [
    REPO_DIR / "app",
    REPO_DIR / "app/velaguard",
    REPO_DIR / "app/velaguard/nanomodbus",
    REPO_DIR / "gui/main/ui",
    REPO_DIR / "gui/main/inc",
    REPO_DIR / "board",
    NUTTX_DIR / "include",
    NUTTX_DIR / "arch/arm/include",
    NUTTX_DIR / "arch/arm/src/common",
    NUTTX_DIR / "arch/arm/src/stm32h7",
    NUTTX_DIR / "boards/arm/stm32h7/stm32h750b-dk/include",
    OPENVELA_ROOT / "apps/include",
    OPENVELA_ROOT / "apps/netutils/mqttc/MQTT-C/include",
    OPENVELA_ROOT / "apps/graphics/lvgl",
    OPENVELA_ROOT / "apps/graphics/lvgl/lvgl",
    OPENVELA_ROOT / "frameworks",
    OPENVELA_ROOT / "packages",
]

COMMON_FLAGS = [
    "-mcpu=cortex-m7",
    "-mthumb",
    "-ffreestanding",
    "-std=gnu11",
    "-D__NuttX__",
    "-DNDEBUG",
    "-DNMBS_SERVER_DISABLED",
    f"-include{CONFIG_H}",
]


def compile_command(source: Path) -> dict[str, str]:
    args = [str(GCC), *COMMON_FLAGS]
    for include in INCLUDE_DIRS:
        args.extend(["-I", str(include)])
    args.extend(["-c", str(source)])
    return {
        "directory": str(source.parent),
        "file": str(source.resolve()),
        "command": " ".join(args),
    }


def main() -> int:
    if not GCC.is_file():
        print(f"error: ARM GCC not found at {GCC}", file=sys.stderr)
        return 1
    if not CONFIG_H.is_file():
        print(
            f"error: {CONFIG_H} missing; run scripts/build.sh or "
            "scripts/prepare_wsl_intellisense.sh first",
            file=sys.stderr,
        )
        return 1

    sources: list[Path] = []
    for pattern in ("app/**/*.c", "board/**/*.c", "gui/main/ui/**/*.c"):
        sources.extend(sorted(REPO_DIR.glob(pattern)))

    if not sources:
        print("error: no contest .c sources found under app/ or board/", file=sys.stderr)
        return 1

    entries = [compile_command(source) for source in sources]
    OUT_PATH.write_text(json.dumps(entries, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {OUT_PATH} ({len(entries)} entries)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
