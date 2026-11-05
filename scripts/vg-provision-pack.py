#!/usr/bin/env python3
"""Build encrypted llm_secrets.v1 blob (matches app/velaguard/vg_provision_crypto.c)."""

from __future__ import annotations

import hashlib
import hmac
import json
import struct
import sys
from pathlib import Path

VGPR_MAGIC = b"VGPR"
VGPR_VERSION = 1
VGPR_HDR_SIZE = 28
VGPR_TAG_SIZE = 32
VGPR_NONCE_SIZE = 16


def derive_keys(uid: bytes) -> tuple[bytes, bytes]:
    info = b"velaguard:provision:v1:"
    master = hashlib.sha256(info + uid).digest()
    mac_key = hashlib.sha256(master + b"mac").digest()
    return master, mac_key


def stream_xor(stream_key: bytes, nonce: bytes, data: bytes) -> bytes:
    out = bytearray(len(data))
    off = 0
    idx = 0
    while off < len(data):
        block = hashlib.sha256(stream_key + nonce + struct.pack("<I", idx)).digest()
        for i in range(32):
            if off >= len(data):
                break
            out[off] = data[off] ^ block[i]
            off += 1
        idx += 1
    return bytes(out)


def seal(uid: bytes, plain: bytes) -> bytes:
    stream_key, mac_key = derive_keys(uid)
    nonce = bytes(len(plain) ^ i ^ uid[i % len(uid)] for i in range(VGPR_NONCE_SIZE))
    hdr = bytearray(VGPR_HDR_SIZE)
    hdr[0:4] = VGPR_MAGIC
    hdr[4] = VGPR_VERSION
    hdr[8:24] = nonce
    struct.pack_into("<H", hdr, 24, len(plain))
    ct = stream_xor(stream_key, nonce, plain)
    body = bytes(hdr) + ct
    tag = hmac.new(mac_key, body, hashlib.sha256).digest()
    return body + tag


def cred_json(host: str, path: str, port: str, model: str, api_key: str) -> bytes:
    return json.dumps(
        {
            "host": host,
            "path": path,
            "port": port,
            "model": model,
            "api_key": api_key,
        },
        separators=(",", ":"),
    ).encode()


def parse_endpoint(endpoint: str) -> tuple[str, str, str]:
    port = "443"
    p = endpoint.strip()
    if p.startswith("https://"):
        p = p[8:]
    elif p.startswith("http://"):
        p = p[7:]
        port = "80"
    if "/" not in p:
        return p, "/v1/chat/completions", port
    host, slash = p.split("/", 1)
    path = "/" + slash
    if path in ("/v1", "/v1/"):
        path = "/v1/chat/completions"
    elif "chat/completions" not in path:
        if not path.endswith("/"):
            path += "/"
        path += "chat/completions"
    return host, path, port


def main() -> int:
    if len(sys.argv) not in (3, 5) or (
        len(sys.argv) == 5 and sys.argv[3] != "--output"
    ):
        print(
            f"usage: {sys.argv[0]} <uid-hex-24> "
            "<plain-json-file|-> [--output <file>]",
            file=sys.stderr,
        )
        return 2
    uid = bytes.fromhex(sys.argv[1])
    if len(uid) != 12:
        print("uid must be 12 bytes (24 hex chars)", file=sys.stderr)
        return 2
    if sys.argv[2] == "-":
        plain = sys.stdin.buffer.read()
    else:
        plain = Path(sys.argv[2]).read_bytes()
    blob = seal(uid, plain)
    if len(sys.argv) == 5:
        Path(sys.argv[4]).write_bytes(blob)
    else:
        sys.stdout.buffer.write(blob)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
