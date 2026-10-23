#!/usr/bin/env python3
"""有时间与请求上限的整轮语音 HTTPS 故障夹具，不记录请求头或音频。"""
import argparse
import base64
import concurrent.futures
import json
from pathlib import Path
import socket
import ssl
import threading
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--stop-file', type=Path)
p.add_argument('--cert', required=True)
p.add_argument('--key', required=True)
p.add_argument('--seconds', type=int, default=600)
p.add_argument('--requests', type=int, default=80)
a = p.parse_args()
if not 1 <= a.seconds <= 900 or not 1 <= a.requests <= 100:
    p.error('seconds 1..900; requests 1..100')
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(a.cert, a.key)
end = time.monotonic() + a.seconds
lock = threading.Lock()
count = 0
paths = {'/ok_asr', '/asr401', '/asr429', '/asrempty', '/asrbad',
         '/asrtruncate', '/asrstall', '/tts401', '/tts429', '/ttstruncate',
         '/ttsbad', '/ttsstall'}

def serve(raw):
    with raw:
        raw.settimeout(6)
        try:
            with ctx.wrap_socket(raw, server_side=True) as s:
                header = bytearray()
                while b'\r\n\r\n' not in header and len(header) < 8192:
                    b = s.recv(1024)
                    if not b:
                        return
                    header.extend(b)
                hdr, separator, first = header.partition(b'\r\n\r\n')
                if not separator:
                    return
                path = hdr.split(b' ', 2)[1].decode('ascii')
                auth = False
                length = 0
                for line in hdr.split(b'\r\n')[1:]:
                    name, _, value = line.partition(b':')
                    if name.lower() == b'authorization':
                        auth = value.strip() == b'Bearer velaguard-invalid-test-credential'
                    if name.lower() == b'content-length':
                        length = int(value)
                if length < 0 or length > 1000000:
                    return
                remaining = max(0, length - len(first))
                del header, hdr, first
                while remaining:
                    chunk = s.recv(min(8192, remaining))
                    if not chunk:
                        return
                    remaining -= len(chunk)
                print(json.dumps({'path': path if path in paths else '/unknown',
                                  'test_credential': auth}), flush=True)
                if path not in paths or not auth:
                    status, payload = 403, b'{}'
                elif path.endswith('stall'):
                    s.settimeout(0.1)
                    until = min(end, time.monotonic() + 40)
                    while time.monotonic() < until:
                        try:
                            if not s.recv(1024):
                                break
                        except socket.timeout:
                            pass
                    return
                elif path.endswith('401') or path.endswith('429'):
                    status, payload = int(path[-3:]), b'{"error":"fixture"}'
                elif path in ('/ok_asr', '/asrempty', '/asrtruncate'):
                    status = 200
                    payload = json.dumps({'choices': [{'message': {'content':
                        '' if path == '/asrempty' else '现在有哪些异常？'}}]},
                        ensure_ascii=False).encode()
                elif path == '/asrbad':
                    status, payload = 200, b'{'
                elif path == '/ttsbad':
                    status, payload = 200, b'data: {broken}\n\n'
                else:
                    pcm = base64.b64encode(bytes(4096)).decode()
                    payload = ('data: ' + json.dumps({'choices': [{'delta':
                        {'audio': {'data': pcm}}}]}) + '\n\n').encode()
                    status = 200
                declared = len(payload) + (100 if path.endswith('truncate') else 0)
                s.sendall((f'HTTP/1.1 {status} Fixture\r\nContent-Length: {declared}'
                           '\r\nConnection: close\r\n\r\n').encode() + payload)
        except (OSError, ValueError, IndexError, ssl.SSLError) as e:
            print(json.dumps({'connection_end': type(e).__name__}), flush=True)

with socket.socket() as server, concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('192.168.137.1', 8443))
    server.listen(4)
    server.settimeout(0.5)
    print('voice fault fixture ready', flush=True)
    while time.monotonic() < end and count < a.requests and not (a.stop_file and a.stop_file.exists()):
        try:
            raw, _ = server.accept()
        except socket.timeout:
            continue
        with lock:
            count += 1
        pool.submit(serve, raw)
print('voice fault fixture stopped', count, flush=True)
