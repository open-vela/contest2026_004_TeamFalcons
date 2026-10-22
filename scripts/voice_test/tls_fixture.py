#!/usr/bin/env python3
"""Bounded local TLS fault fixture. Never logs request headers or bodies.

Supply a disposable test certificate and key; no product credential is needed.
The listener closes after --seconds or --requests, whichever comes first.
Port + 1 accepts TCP but deliberately never completes a TLS handshake.
"""
import argparse
import concurrent.futures
import socket
import ssl
import threading
import time


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bind', default='192.168.137.1')
    p.add_argument('--port', type=int, default=8443)
    p.add_argument('--cert', required=True)
    p.add_argument('--key', required=True)
    p.add_argument('--seconds', type=int, default=600)
    p.add_argument('--requests', type=int, default=48)
    a = p.parse_args()
    if not 1 <= a.seconds <= 900 or not 1 <= a.requests <= 100:
        p.error('seconds must be 1..900 and requests 1..100')
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(a.cert, a.key)
    deadline = time.monotonic() + a.seconds
    stop = threading.Event()
    budget_lock = threading.Lock()
    remaining = a.requests

    def hold_until_closed(s):
        # Keep the fault active, but free a fixture slot when the client has
        # really closed its socket.  A cancelled request must not saturate
        # the test server's worker pool and delay the following recovery.
        until = min(deadline, time.monotonic() + 8)
        s.settimeout(0.1)
        while not stop.is_set() and time.monotonic() < until:
            try:
                if not s.recv(1024):
                    return
            except socket.timeout:
                continue
            except OSError:
                return

    def serve(raw, silent):
        with raw:
            raw.settimeout(5)
            if silent:
                hold_until_closed(raw)
                return
            try:
                with ctx.wrap_socket(raw, server_side=True) as s:
                    data = bytearray()
                    while b'\r\n\r\n' not in data and len(data) < 8192:
                        part = s.recv(1024)
                        if not part:
                            return
                        data.extend(part)
                    hdr, _, body = data.partition(b'\r\n\r\n')
                    path = bytes(hdr.split(b' ', 2)[1]) if b' ' in hdr else b'/'
                    length = 0
                    for line in hdr.split(b'\r\n')[1:]:
                        if line.lower().startswith(b'content-length:'):
                            length = min(1024, int(line.split(b':', 1)[1]))
                    while len(body) < length:
                        part = s.recv(length - len(body))
                        if not part:
                            return
                        body += part
                    # Do not print arbitrary request text or credentials.
                    known = {b'/ok', b'/unauthorized', b'/rate-limit',
                             b'/truncate', b'/stall'}
                    label = path.decode('ascii') if path in known else '/unknown'
                    print('request', label, flush=True)
                    if path == b'/stall':
                        hold_until_closed(s)
                        return
                    status = {b'/unauthorized': 401, b'/rate-limit': 429}.get(path, 200)
                    payload = b'{"test":true}'
                    declared = len(payload) + (100 if path == b'/truncate' else 0)
                    reply = (f'HTTP/1.1 {status} Test\r\nContent-Length: {declared}'
                             '\r\nConnection: close\r\nContent-Type: application/json'
                             '\r\n\r\n').encode() + payload
                    s.sendall(reply)
            except (OSError, ValueError, ssl.SSLError, IndexError) as e:
                print('connection ended', type(e).__name__, flush=True)

    def listen(port, silent, executor):
        nonlocal remaining
        with socket.socket() as server:
            server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server.bind((a.bind, port))
            server.listen(4)
            server.settimeout(0.5)
            print('listen', a.bind, port, 'silent' if silent else 'tls', flush=True)
            while not stop.is_set() and time.monotonic() < deadline:
                try:
                    raw, _ = server.accept()
                except socket.timeout:
                    continue
                with budget_lock:
                    if remaining <= 0:
                        raw.close()
                        stop.set()
                        break
                    remaining -= 1
                executor.submit(serve, raw, silent)

    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
        t = threading.Thread(target=listen, args=(a.port + 1, True, executor))
        t.start()
        try:
            listen(a.port, False, executor)
        finally:
            stop.set()
            t.join()
    print('fixture stopped', flush=True)


if __name__ == '__main__':
    main()
