#!/usr/bin/env python3
"""400 归因隔离：同一 LLM 请求，仅改变 TLS 握手属性（版本/SNI/ALPN）。

不打印密钥；只输出每个变体的状态码、协商版本与密码套件。
"""
import json
import re
import socket
import ssl
import sys
import time
from pathlib import Path

sys.path.insert(0, '/home/hello19y/openvela/contest2026_004_TeamFalcons/scripts/voice_test')
from mimo_phase_probe import load_connect_info  # noqa: E402

HOST, KEY = load_connect_info('connect_info')
HOST = HOST.replace('https://', '').replace('http://', '').split('/')[0]

BODY = json.dumps({
    "model": "mimo-v2.6-flash",
    "messages": [{"role": "user", "content": "回复ok两个字母即可"}],
    "stream": False,
}).encode()


def one(label, tls_min=None, tls_max=None, sni=True, alpn=None, ciphers=None):
    ctx = ssl.create_default_context()
    if tls_min:
        ctx.minimum_version = tls_min
    if tls_max:
        ctx.maximum_version = tls_max
    if ciphers:
        ctx.set_ciphers(ciphers)
    if alpn:
        ctx.set_alpn_protocols(alpn)
    name = HOST if sni else None
    if not sni:
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
    try:
        raw = socket.create_connection((HOST, 443), timeout=20)
        s = ctx.wrap_socket(raw, server_hostname=name)
        head = (
            f"POST /v1/chat/completions HTTP/1.1\r\n"
            f"Host: {HOST}\r\n"
            f"Connection: close\r\n"
            f"User-Agent: velaguard-probe/1.0\r\n"
            f"Content-Type: application/json\r\n"
            f"Content-Length: {len(BODY)}\r\n"
            f"Authorization: Bearer {KEY}\r\n\r\n"
        ).encode()
        s.sendall(head + BODY)
        buf = b""
        s.settimeout(30)
        while b"\r\n\r\n" not in buf:
            piece = s.recv(65536)
            if not piece:
                break
            buf += piece
        raw_head = buf.split(b"\r\n\r\n", 1)[0].decode("latin-1")
        status = raw_head.split(" ", 2)[1] if " " in raw_head else "?"
        body_hint = ""
        m = re.search(r'(?i)"message"\s*:\s*"([^"]{0,60})', buf.decode("latin-1", "replace"))
        if m:
            body_hint = " body=" + m.group(1)
        mife = re.search(r"(?im)^X-MiFE-Upstream-Status:\s*(\d+)", raw_head)
        print(f"{label:34s} -> {status}  tls={s.version()} cipher={s.cipher()[0]}"
              + (f" MiFE={mife.group(1)}" if mife else "") + body_hint)
        s.close()
    except Exception as exc:
        print(f"{label:34s} -> EXC {type(exc).__name__}: {str(exc)[:80]}")


one("default(1.3 allowed)")
one("tls1.2-only", tls_min=ssl.TLSVersion.TLSv1_2, tls_max=ssl.TLSVersion.TLSv1_2)
one("board-like 1.2+SNI+alpn", tls_min=ssl.TLSVersion.TLSv1_2,
    tls_max=ssl.TLSVersion.TLSv1_2, alpn=["http/1.1"])
one("tls1.2-only noSNI", tls_min=ssl.TLSVersion.TLSv1_2,
    tls_max=ssl.TLSVersion.TLSv1_2, sni=False)
one("1.3-allowed noSNI", sni=False)
one("tls1.2 + alpn http/1.1", tls_min=ssl.TLSVersion.TLSv1_2,
    tls_max=ssl.TLSVersion.TLSv1_2, alpn=["http/1.1"])
one("default + alpn http/1.1", alpn=["http/1.1"])
