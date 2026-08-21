#!/usr/bin/env bash
# Launch the extracted gdb-multiarch with its bundled shared libraries.
#
# The tree under ~/tools/gdb-multiarch was unpacked from Debian packages
# (dpkg -x) without installing into /usr, so the dynamic loader cannot find
# libbabeltrace/libipt/... on its own. Point LD_LIBRARY_PATH at the bundled
# lib dir, then exec gdb so Cortex-Debug talks to the real gdb process.

GDB_BIN="${GDB_BIN:-/home/debian19y/tools/gdb-multiarch/usr/bin/gdb-multiarch}"
GDB_LIBS="${GDB_LIBS:-/home/debian19y/tools/gdb-multiarch/usr/lib/x86_64-linux-gnu}"

if [ -x "$GDB_BIN" ]; then
  export LD_LIBRARY_PATH="${GDB_LIBS}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
  exec "$GDB_BIN" "$@"
fi

if command -v gdb-multiarch >/dev/null 2>&1; then
  exec gdb-multiarch "$@"
fi

echo "error: gdb-multiarch 未找到。请安装 gdb-multiarch，或设置 GDB_BIN 指向可执行文件（当前 $GDB_BIN）。" >&2
exit 1
