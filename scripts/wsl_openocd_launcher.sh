#!/usr/bin/env bash
# Cortex-Debug (WSL) launcher for Windows xPack OpenOCD.
#
# Cortex-Debug allocates gdb/tcl/telnet ports from 50000. Windows Hyper-V/WSL
# excludes 50000-50059, so openocd.exe fails with:
#   Error: couldn't bind tcl to socket on port 50001: No error
#   OpenOCD: GDB Server Quit Unexpectedly
#
# This wrapper:
#   - disables tcl/telnet (Live Watch needs tcl; we do not use it)
#   - if gdb_port is in the excluded range, OpenOCD binds 3333 instead and a
#     localhost relay presents the original port to Linux GDB (mirrored WSL)
set -euo pipefail

OCD="${OPENVELA_OPENOCD:-/mnt/d/Develop/xpack-openocd-0.12.0-7/bin/openocd.exe}"
WIN_GDB_PORT="${OPENVELA_OPENOCD_GDB_PORT:-3333}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RELAY_PY="$SCRIPT_DIR/wsl_tcp_relay.py"

if [ ! -x "$OCD" ] && [ ! -f "$OCD" ]; then
  echo "error: OpenOCD 未找到: $OCD" >&2
  echo "请安装 xPack OpenOCD，或设置 OPENVELA_OPENOCD。" >&2
  exit 1
fi

EXCLUDED_MIN=50000
EXCLUDED_MAX=50059

host_gdb_port=""
new_args=()
while [ "$#" -gt 0 ]; do
  if [ "$1" = "-c" ] && [ "${2:-}" != "" ]; then
    case "$2" in
      gdb_port\ *)
        host_gdb_port="${2#gdb_port }"
        if [ "$host_gdb_port" -ge "$EXCLUDED_MIN" ] && [ "$host_gdb_port" -le "$EXCLUDED_MAX" ]; then
          new_args+=(-c "gdb_port ${WIN_GDB_PORT}")
        else
          new_args+=(-c "$2")
        fi
        shift 2
        continue
        ;;
      tcl_port\ *|telnet_port\ *)
        prefix="${2%% *}"
        new_args+=(-c "${prefix} disabled")
        shift 2
        continue
        ;;
    esac
  fi
  new_args+=("$1")
  shift
done

relay_pid=""
cleanup() {
  if [ -n "$relay_pid" ]; then
    kill "$relay_pid" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT INT TERM

need_relay=0
if [ -n "$host_gdb_port" ] && [ "$host_gdb_port" -ge "$EXCLUDED_MIN" ] && [ "$host_gdb_port" -le "$EXCLUDED_MAX" ]; then
  need_relay=1
fi

if [ "$need_relay" -eq 1 ]; then
  echo "[openocd] Hyper-V 排除 ${EXCLUDED_MIN}-${EXCLUDED_MAX}；中继 127.0.0.1:${host_gdb_port} -> OpenOCD :${WIN_GDB_PORT}" >&2
  python3 "$RELAY_PY" "$host_gdb_port" "$WIN_GDB_PORT" &
  relay_pid=$!
  # Give the listen socket a moment before OpenOCD/GDB race it.
  sleep 0.2
fi

set +e
"$OCD" "${new_args[@]}"
status=$?
set -e
cleanup
exit "$status"
