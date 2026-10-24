#!/usr/bin/env python3
"""把串口日志里 [dump:req]/[dump:sreq] 的请求字节重放到真实服务端。

板端 dump 的 Authorization 值被掩码；本工具用 connect_info 里的真实
密钥替换回去（板端与 connect_info 密钥一致性已单独验证），按原始
字节发送。正文若只有前缀（dumpreq 1），默认按实际长度修正
Content-Length 后发送（--keep-cl 则保留原始声明，用于复现
声明与实际不符的失败）。不打印密钥。
"""
import argparse
import re
import socket
import ssl
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mimo_phase_probe import load_connect_info  # noqa: E402

HEAD_RE = re.compile(r"\[dump:(req|sreq)\] (.*)")
BODY_RE = re.compile(r"\[dump:(reqbody|sreqbody)\] ([0-9a-f]*)")
UNESC_RE = re.compile(r"\\(r|n|x[0-9a-fA-F]{2})")


def unescape(text):
    return UNESC_RE.sub(
        lambda m: "\r" if m.group(1) == "r" else
                  "\n" if m.group(1) == "n" else
                  chr(int(m.group(1)[1:], 16)), text)


def parse_blocks(log_text):
    """把 (head, body_prefix) 请求对按出现顺序取出。"""
    pending = {}
    out = []
    for line in log_text.splitlines():
        m = HEAD_RE.search(line)
        if m:
            pending[m.group(1)] = [unescape(m.group(2)), b""]
            continue
        m = BODY_RE.search(line)
        if m and m.group(1) in pending:
            pending[m.group(1)][1] += bytes.fromhex(m.group(2))
            key = m.group(1)
            out.append((key, pending.pop(key)))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("log", type=Path)
    ap.add_argument("--connect-info", default="connect_info")
    ap.add_argument("--keep-cl", action="store_true",
                    help="保留板端声明的 Content-Length（默认按实际字节修正）")
    ap.add_argument("--index", type=int, default=0, help="重放第几个请求（默认 0）")
    a = ap.parse_args()

    url, key = load_connect_info(a.connect_info)
    host = url.replace("https://", "").split("/")[0]

    blocks = parse_blocks(a.log.read_text(errors="replace"))
    blocks = [b for b in blocks if b[1][0]]
    if not blocks:
        raise SystemExit("日志里没有 dump 请求块")
    tag, (head, body) = blocks[min(a.index, len(blocks) - 1)]

    head = head.replace("*" * 40, key).replace("*" * 20, key)
    if "Bearer *" in head:
        head = re.sub(r"Bearer \*+", f"Bearer {key}", head)
    if not a.keep_cl:
        head = re.sub(r"(?i)Content-Length: ?\d+", f"Content-Length: {len(body)}",
                      head)
    request_line = head.split("\r\n", 1)[0]
    declared = re.search(r"(?i)Content-Length: ?(\d+)", head)
    print(f"重放 {tag} #{a.index}/{len(blocks)-1}: {request_line}")
    print(f"  正文 {len(body)} 字节, 声明 Content-Length="
          f"{declared.group(1) if declared else '无'}")

    ctx = ssl.create_default_context()
    raw = socket.create_connection((host, 443), timeout=20)
    s = ctx.wrap_socket(raw, server_hostname=host)
    s.sendall(head.encode("latin-1") + body)
    s.settimeout(30)
    buf = b""
    while b"\r\n\r\n" not in buf:
        piece = s.recv(65536)
        if not piece:
            break
        buf += piece
    raw_head = buf.split(b"\r\n\r\n", 1)[0].decode("latin-1")
    status = raw_head.split(" ", 2)[1] if " " in raw_head else "?"
    mife = re.search(r"(?im)^X-MiFE-Upstream-Status:\s*(\d+)", raw_head)
    print(f"  -> {status}" + (f" MiFE={mife.group(1)}" if mife else "")
          + f" tls={s.version()}")


if __name__ == "__main__":
    main()
