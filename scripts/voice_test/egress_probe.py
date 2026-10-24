#!/usr/bin/env python3
"""400 归因隔离（出口路径）：同一请求，对比 fake-ip 域名连接与真实 IP 直连。

主机日常探针走本机代理（fake-ip 198.18/15 段），板端走 Wi-Fi 直连。
若真实 IP 直连复现 400 而域名走代理 200，则差异在出口路径/来源 IP，
板端请求字节与 TLS 属性被排除。不打印密钥。
"""
import json
import re
import socket
import ssl
import sys
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mimo_phase_probe import load_connect_info  # noqa: E402

DOMAIN, KEY = load_connect_info('connect_info')
DOMAIN = DOMAIN.replace('https://', '').split('/')[0]

BODY = json.dumps({
    "model": "mimo-v2.6-flash",
    "messages": [{"role": "user", "content": "回复ok两个字母即可"}],
    "stream": False,
}).encode()


def request_over(sock_factory, label):
    ctx = ssl.create_default_context()
    try:
        raw = sock_factory()
        s = ctx.wrap_socket(raw, server_hostname=DOMAIN)
        head = (
            f"POST /v1/chat/completions HTTP/1.1\r\n"
            f"Host: {DOMAIN}\r\n"
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
        mife = re.search(r"(?im)^X-MiFE-Upstream-Status:\s*(\d+)", raw_head)
        msg = re.search(r'(?i)"message"\s*:\s*"([^"]{0,50})',
                        buf.decode("latin-1", "replace"))
        print(f"{label:38s} -> {status}"
              + (f" MiFE={mife.group(1)}" if mife else "")
              + (f" body={msg.group(1)}" if msg else "")
              + f"  tls={s.version()}")
        s.close()
    except Exception as exc:
        print(f"{label:38s} -> EXC {type(exc).__name__}: {str(exc)[:70]}")


print(f"fake-ip 解析: {socket.getaddrinfo(DOMAIN, 443, proto=socket.IPPROTO_TCP)[0][4][0]}")
request_over(lambda: socket.create_connection((DOMAIN, 443), timeout=20),
             "域名(经代理/系统路由)")

# DoH 取真实 A 记录（DoH 本身可能经代理，但返回的是权威真实 IP）
real_ips = []
for provider in (f"https://dns.alidns.com/resolve?name={DOMAIN}&type=A",
                 f"https://doh.pub/dns-query?name={DOMAIN}&type=A"):
    try:
        req = urllib.request.Request(provider, headers={"accept": "application/dns-json"})
        data = json.loads(urllib.request.urlopen(req, timeout=15).read())
        real_ips = [a["data"] for a in data.get("Answer", []) if a.get("type") == 1]
        if real_ips:
            print(f"真实 A 记录({provider.split('/')[2]}): {', '.join(real_ips)}")
            break
    except Exception as exc:
        print(f"DoH {provider.split('/')[2]} 失败: {type(exc).__name__}")

for ip in real_ips[:3]:
    request_over(lambda ip=ip: socket.create_connection((ip, 443), timeout=20),
                 f"直连真实 IP {ip}")
