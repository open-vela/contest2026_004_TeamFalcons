#!/usr/bin/env python3
"""Forward 127.0.0.1:src -> 127.0.0.1:dst.

Cortex-Debug in WSL assigns gdb_port from 50000. Windows Hyper-V excludes
50000-50059, so openocd.exe cannot bind those ports. This relay lets Linux
GDB keep connecting to the Cortex-Debug port while OpenOCD listens on a
Windows-safe port (default 3333).
"""
from __future__ import annotations

import socket
import sys
import threading


def _pipe(src: socket.socket, dst: socket.socket) -> None:
    try:
        while True:
            data = src.recv(65536)
            if not data:
                break
            dst.sendall(data)
    except OSError:
        pass
    finally:
        for side in (src, dst):
            try:
                side.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass


def _handle(client: socket.socket, dst_port: int) -> None:
    try:
        remote = socket.create_connection(("127.0.0.1", dst_port), timeout=5)
    except OSError:
        client.close()
        return
    remote.settimeout(None)
    threading.Thread(target=_pipe, args=(client, remote), daemon=True).start()
    _pipe(remote, client)
    client.close()
    remote.close()


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <listen-port> <dest-port>", file=sys.stderr)
        return 2
    src_port = int(sys.argv[1])
    dst_port = int(sys.argv[2])
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", src_port))
    server.listen(8)
    while True:
        client, _ = server.accept()
        threading.Thread(target=_handle, args=(client, dst_port), daemon=True).start()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(0)
