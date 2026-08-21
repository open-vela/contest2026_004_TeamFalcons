#!/usr/bin/env bash
# Kill stale Windows-side OpenOCD instances before a Cortex-Debug attach.
#
# Launched from the Remote - WSL window; taskkill.exe runs through WSL
# interop. A previous failed F5 can leave openocd.exe holding the ST-LINK
# adapter, which makes the next OpenOCD exit immediately ("GDB Server Quit
# Unexpectedly"). This exits 0 whether or not a stale process existed.

/mnt/c/Windows/System32/taskkill.exe /IM openocd.exe /F >/dev/null 2>&1 || true
exit 0
