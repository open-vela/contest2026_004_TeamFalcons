#!/usr/bin/env python3
"""BVO-06 host-side probe for MiMo ASR and TTS.

Credentials come from the environment only:
    MIMO_API_KEY   required
    MIMO_BASE      optional, default https://api.xiaomimimo.com/v1

The script never prints, logs or stores the key. Response bodies are stored
only as bounded metadata plus, for ASR, the returned transcript.

Design limits, so that this stays a bounded verification rather than a load
test: a hard request cap per run, one attempt per request (no retry), a socket
timeout, and a response size cap.

Usage:
    verify_mimo_voice.py models
    verify_mimo_voice.py asr --file samples/asr-5s-16k-mono.wav [--language zh]
    verify_mimo_voice.py tts --text "测试内容" [--format wav] [--voice mimo_default]
"""
import argparse
import base64
import json
import os
import socket
import ssl
import sys
import time
from urllib.parse import urlsplit

DEFAULT_BASE = "https://api.xiaomimimo.com/v1"
MAX_RESPONSE_BYTES = 24 * 1024 * 1024
REQUEST_CAP = 40
_counter = {"n": 0}


class Result(dict):
    pass


def now_ms():
    return time.monotonic() * 1000.0


def scrub(text, secret):
    if not text:
        return text
    return text.replace(secret, "<redacted>") if secret else text


def read_env():
    key = os.environ.get("MIMO_API_KEY", "").strip()
    if not key:
        sys.exit("MIMO_API_KEY is not set; export it from a protected source")
    base = os.environ.get("MIMO_BASE", DEFAULT_BASE).strip().rstrip("/")
    return key, base


def http_json(base, path, key, payload=None, method="POST", timeout=30.0,
              want_stream=False, max_bytes=MAX_RESPONSE_BYTES):
    """One HTTP request with per-phase timing. No retries."""
    if _counter["n"] >= REQUEST_CAP:
        raise SystemExit(f"request cap of {REQUEST_CAP} reached; stop and inspect results")
    _counter["n"] += 1

    parts = urlsplit(base)
    host = parts.hostname
    port = parts.port or (443 if parts.scheme == "https" else 80)
    prefix = parts.path.rstrip("/")
    body = b"" if payload is None else json.dumps(payload, ensure_ascii=False).encode("utf-8")

    t_start = now_ms()
    phases = {}
    t = now_ms()
    infos = socket.getaddrinfo(host, port, proto=socket.IPPROTO_TCP)
    phases["dns_ms"] = round(now_ms() - t, 2)

    t = now_ms()
    sock = socket.create_connection((infos[0][4][0], port), timeout=timeout)
    phases["tcp_ms"] = round(now_ms() - t, 2)

    t = now_ms()
    ctx = ssl.create_default_context()
    sock = ctx.wrap_socket(sock, server_hostname=host)
    phases["tls_ms"] = round(now_ms() - t, 2)
    phases["tls_version"] = sock.version()

    head = [
        f"{method} {prefix}{path} HTTP/1.1",
        f"Host: {host}",
        "api-key: " + key,
        "Accept: application/json" + (", text/event-stream" if want_stream else ""),
        "Content-Length: %d" % len(body),
        "Connection: close",
        "User-Agent: velaguard-bvo06/1.0",
    ]
    if body:
        head.append("Content-Type: application/json")
    raw = ("\r\n".join(head) + "\r\n\r\n").encode("utf-8") + body

    t = now_ms()
    sock.sendall(raw)
    phases["upload_ms"] = round(now_ms() - t, 2)
    phases["request_bytes"] = len(raw)

    t = now_ms()
    sock.settimeout(timeout)
    buf = b""
    while b"\r\n\r\n" not in buf:
        chunk = sock.recv(65536)
        if not chunk:
            break
        buf += chunk
    phases["ttfb_ms"] = round(now_ms() - t, 2)

    head_raw, _, rest = buf.partition(b"\r\n\r\n")
    lines = head_raw.decode("iso-8859-1", "replace").split("\r\n")
    status = int(lines[0].split(" ")[1]) if len(lines[0].split(" ")) > 1 else 0
    headers = {}
    for line in lines[1:]:
        if ":" in line:
            k, v = line.split(":", 1)
            headers[k.strip().lower()] = v.strip()

    t = now_ms()
    payload_bytes = bytearray(rest)
    chunked = headers.get("transfer-encoding", "").lower() == "chunked"
    if chunked:
        data = rest
        decoded = bytearray()
        while True:
            while b"\r\n" not in data:
                data += sock.recv(65536)
            size_line, _, data = data.partition(b"\r\n")
            try:
                size = int(size_line.split(b";")[0], 16)
            except ValueError:
                break
            if size == 0:
                break
            while len(data) < size + 2:
                data += sock.recv(65536)
            decoded += data[:size]
            data = data[size + 2:]
            if len(decoded) > max_bytes:
                break
        payload_bytes = decoded
    else:
        length = headers.get("content-length")
        limit = int(length) if length else max_bytes
        while len(payload_bytes) < limit:
            chunk = sock.recv(65536)
            if not chunk:
                break
            payload_bytes += chunk
            if len(payload_bytes) > max_bytes:
                break
    phases["download_ms"] = round(now_ms() - t, 2)
    sock.close()

    phases["total_ms"] = round(now_ms() - t_start, 2)
    return status, headers, bytes(payload_bytes), phases


def parse_sse(raw):
    """Return (joined content, joined audio base64, finish_reason, chunk_count)."""
    text, audio, finish, n = [], [], None, 0
    for line in raw.decode("utf-8", "replace").splitlines():
        if not line.startswith("data:"):
            continue
        payload = line[5:].strip()
        if payload == "[DONE]":
            break
        try:
            obj = json.loads(payload)
        except json.JSONDecodeError:
            continue
        n += 1
        for ch in obj.get("choices", []):
            delta = ch.get("delta") or {}
            if delta.get("content"):
                text.append(delta["content"])
            a = delta.get("audio") or {}
            if a.get("data"):
                audio.append(a["data"])
            if ch.get("finish_reason"):
                finish = ch["finish_reason"]
    return "".join(text), "".join(audio), finish, n


def cmd_models(args):
    key, base = read_env()
    status, headers, body, phases = http_json(base, "/models", key, None, "GET", args.timeout)
    ids = []
    if status == 200:
        try:
            ids = [m.get("id") for m in json.loads(body).get("data", [])]
        except Exception:
            pass
    return {
        "op": "models",
        "base_host": urlsplit(base).hostname,
        "status": status,
        "models": ids,
        "phases": phases,
        "error": None if status == 200 else scrub(body.decode("utf-8", "replace")[:400], key),
    }


def cmd_asr(args):
    key, base = read_env()
    with open(args.file, "rb") as f:
        audio = f.read()
    b64 = base64.b64encode(audio).decode("ascii")
    payload = {
        "model": args.model,
        "messages": [{
            "role": "user",
            "content": [{
                "type": "input_audio",
                "input_audio": {"data": "data:audio/wav;base64," + b64},
            }],
        }],
        "asr_options": {"language": args.language},
    }
    if args.stream:
        payload["stream"] = True
    status, headers, body, phases = http_json(
        base, "/chat/completions", key, payload, "POST", args.timeout, want_stream=args.stream
    )
    out = {
        "op": "asr",
        "file": os.path.basename(args.file),
        "input_bytes": len(audio),
        "input_seconds": round(args.seconds, 3) if args.seconds else None,
        "request_b64_bytes": len(b64),
        "model": args.model,
        "language": args.language,
        "stream": bool(args.stream),
        "status": status,
        "phases": phases,
        "response_bytes": len(body),
    }
    if status == 200:
        if args.stream:
            text, _, finish, n = parse_sse(body)
            out["transcript"] = text
            out["sse_chunks"] = n
            out["finish_reason"] = finish
            out["usage"] = None
        else:
            obj = json.loads(body)
            ch = (obj.get("choices") or [{}])[0]
            out["transcript"] = (ch.get("message") or {}).get("content", "")
            out["finish_reason"] = ch.get("finish_reason")
            out["usage"] = obj.get("usage")
    else:
        out["error"] = scrub(body.decode("utf-8", "replace")[:400], key)
        out["headers_ctype"] = headers.get("content-type")
    return out


def cmd_tts(args):
    key, base = read_env()
    payload = {
        "model": args.model,
        "messages": [
            {"role": "user", "content": args.style} if args.style else None,
            {"role": "assistant", "content": args.text},
        ],
        "audio": {"format": args.format, "voice": args.voice},
    }
    payload["messages"] = [m for m in payload["messages"] if m]
    if args.stream:
        payload["stream"] = True
    status, headers, body, phases = http_json(
        base, "/chat/completions", key, payload, "POST", args.timeout, want_stream=args.stream
    )
    out = {
        "op": "tts",
        "text": args.text,
        "text_chars": len(args.text),
        "model": args.model,
        "voice": args.voice,
        "format": args.format,
        "stream": bool(args.stream),
        "status": status,
        "phases": phases,
        "response_bytes": len(body),
    }
    audio_bytes = b""
    if status == 200:
        if args.stream:
            _, audio_b64, finish, n = parse_sse(body)
            audio_bytes = base64.b64decode(audio_b64) if audio_b64 else b""
            out["sse_chunks"] = n
            out["finish_reason"] = finish
        else:
            obj = json.loads(body)
            ch = (obj.get("choices") or [{}])[0]
            a = (ch.get("message") or {}).get("audio") or {}
            audio_bytes = base64.b64decode(a.get("data", "")) if a.get("data") else b""
            out["usage"] = obj.get("usage")
            out["audio_id"] = a.get("id")
            out["audio_expires_at"] = a.get("expires_at")
            out["audio_transcript"] = a.get("transcript")
        out["audio_bytes"] = len(audio_bytes)
        if args.save_audio and audio_bytes:
            with open(args.save_audio, "wb") as f:
                f.write(audio_bytes)
            out["saved"] = os.path.basename(args.save_audio)
    else:
        out["error"] = scrub(body.decode("utf-8", "replace")[:400], key)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("op", choices=("models", "asr", "tts"))
    ap.add_argument("--file")
    ap.add_argument("--seconds", type=float)
    ap.add_argument("--text")
    ap.add_argument("--language", default="zh", choices=("auto", "zh", "en"))
    ap.add_argument("--model")
    ap.add_argument("--voice", default="mimo_default")
    ap.add_argument("--format", default="wav", choices=("wav", "mp3", "pcm", "pcm16"))
    ap.add_argument("--style", default=None)
    ap.add_argument("--stream", action="store_true")
    ap.add_argument("--save-audio")
    ap.add_argument("--timeout", type=float, default=45.0)
    ap.add_argument("--out", default=None, help="append one JSON line here")
    args = ap.parse_args()

    if args.op == "asr":
        if not args.file:
            sys.exit("--file is required for asr")
        args.model = args.model or "mimo-v2.5-asr"
        res = cmd_asr(args)
    elif args.op == "tts":
        if not args.text:
            sys.exit("--text is required for tts")
        args.model = args.model or "mimo-v2.5-tts"
        res = cmd_tts(args)
    else:
        res = cmd_models(args)

    line = json.dumps(res, ensure_ascii=False)
    print(line)
    if args.out:
        with open(args.out, "a", encoding="utf-8") as f:
            f.write(line + "\n")


if __name__ == "__main__":
    main()
