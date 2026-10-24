#!/usr/bin/env python3
"""MiMo 三类服务的相位参考探测（P0 主机基线）。

镜像板端语音巡检的三类请求形状——ASR 的 base64 WAV 上传、TTS 的 pcm16
流式合成、LLM 的 voice 范围（思考关闭、限长输出）——从同一网络出口记录
dns/tcp/tls/send/ttfb/body 各阶段，TTS 记录逐块到达时间戳（与板端
tts_blocks 同定义：一条含音频的 data 行是一块），并探测服务端是否允许
同一 TLS 连接上顺序复用（keep-alive）。板端日志里的 dns_tcp 是融合值，
这里的拆分值给出 DNS 成本的参考上界。

密钥只在运行时从 connect_info 解析，只进 Authorization 头；任何输出、
异常文本和 JSON 都不包含密钥。

用法：
  python3 mimo_phase_probe.py asr --wav anomaly01.wav
  python3 mimo_phase_probe.py tts --text "当前没有异常点位"
  python3 mimo_phase_probe.py llm --q "现在有哪些异常？"
  python3 mimo_phase_probe.py keepalive
  python3 mimo_phase_probe.py all --wav anomaly01.wav --text "当前没有异常点位" \
      --q "查询点位 ups_load 的当前读数，请调用 vg_point_read。"
"""

import argparse
import base64
import json
import re
import socket
import ssl
import time
from datetime import datetime, timezone
from pathlib import Path

API_PATH = "/v1/chat/completions"
ASR_MODEL = "mimo-v2.5-asr"
TTS_MODEL = "mimo-v2.5-tts"
TTS_VOICE = "冰糖"
LLM_MODEL = "mimo-v2.6-flash"


def load_connect_info(path):
    """connect_info 是松散文本：MiMo 段有一行 https 端点和一行 tp- 密钥。"""
    text = Path(path).read_text(encoding="utf-8", errors="replace")
    endpoint = re.search(r"https://[A-Za-z0-9.-]+", text)
    key = re.search(r"(?m)^(tp-[A-Za-z0-9_-]+)$", text)
    if not endpoint or not key:
        raise SystemExit("connect_info 里找不到 MiMo 端点或密钥行")
    return endpoint.group(0), key.group(1)


class Connection:
    """一条 TLS 连接，阶段打点；keep-alive 探测复用同一实例。

    body 接收支持 Content-Length、chunked 和读到对端关闭三种帧格式；
    sse_line_cb（可选）在每条完整 SSE 行到达时收到（自 send_done 起的
    毫秒偏移, 行字节），用于 TTS 的逐块节奏。
    """

    def __init__(self, host, api_key):
        self.host = host
        self.api_key = api_key
        self.phases = {}
        t0 = time.monotonic()
        socket.getaddrinfo(host, 443, proto=socket.IPPROTO_TCP)
        self.phases["dns_ms"] = round((time.monotonic() - t0) * 1000, 1)
        t0 = time.monotonic()
        raw = socket.create_connection((host, 443), timeout=30)
        self.phases["tcp_ms"] = round((time.monotonic() - t0) * 1000, 1)
        t0 = time.monotonic()
        self.tls = ssl.create_default_context().wrap_socket(
            raw, server_hostname=host)
        self.phases["tls_ms"] = round((time.monotonic() - t0) * 1000, 1)
        # P4 对照：主机侧协商到的协议版本与密码套件。板端 mbedTLS 3.4.0
        # 默认钉在 TLS1.2（MBEDTLS_SSL_PROTO_TLS1_3 未编译），这里记录
        # 服务端实际能谈到的版本，作为 TLS 1.3 收益评估的参考上界。
        self.phases["tls_version"] = self.tls.version()
        self.phases["tls_cipher"] = self.tls.cipher()[0]
        self.tls.settimeout(60)

    def close(self):
        try:
            self.tls.close()
        except OSError:
            pass

    def _recv(self):
        piece = self.tls.recv(65536)
        if not piece:
            raise IOError("连接在对端关闭")
        return piece

    def request(self, payload, connection_header="close", sse_line_cb=None):
        """发送一个 POST，读到完整响应。返回 (状态, 阶段, 响应体)。"""
        body = payload.encode()
        head = (
            f"POST {API_PATH} HTTP/1.1\r\n"
            f"Host: {self.host}\r\n"
            f"Connection: {connection_header}\r\n"
            "User-Agent: velaguard-probe/1.0\r\n"
            "Content-Type: application/json\r\n"
            f"Content-Length: {len(body)}\r\n"
            f"Authorization: Bearer {self.api_key}\r\n"
            "\r\n"
        ).encode()

        t0 = time.monotonic()
        self.tls.sendall(head + body)
        send_done = time.monotonic()
        phases = {"send_ms": round((send_done - t0) * 1000, 1)}

        buf = b""
        while b"\r\n\r\n" not in buf:
            chunk = self._recv()
            if "ttfb_ms" not in phases:
                phases["ttfb_ms"] = round((time.monotonic() - send_done) * 1000, 1)
            buf += chunk
        raw_head, _, rest = buf.partition(b"\r\n\r\n")
        header_text = raw_head.decode("latin-1")
        status = int(header_text.split(" ", 2)[1])
        chunked = "chunked" in header_text.lower()
        length = re.search(r"(?i)content-length:\s*(\d+)", header_text)
        length = int(length.group(1)) if length else None

        lines = _SseLines(sse_line_cb, send_done) if sse_line_cb else None

        if chunked:
            data = self._read_chunked(rest, lines)
        elif length is not None:
            data = bytearray(rest)
            if lines:
                lines.feed(rest)
            while len(data) < length:
                piece = self._recv()
                if lines:
                    lines.feed(piece)
                data += piece
            data = data[:length]
        else:
            data = bytearray(rest)
            if lines:
                lines.feed(rest)
            while True:
                try:
                    piece = self._recv()
                except (ssl.SSLError, OSError):
                    break
                if lines:
                    lines.feed(piece)
                data += piece
        if lines:
            lines.flush()
        phases["body_ms"] = round((time.monotonic() - send_done) * 1000, 1)
        return status, phases, bytes(data)

    def _read_chunked(self, initial, lines):
        out = bytearray(initial)
        data = bytearray()
        while True:
            while b"\r\n" not in out:
                out += self._recv()
            line, _, out = out.partition(b"\r\n")
            size = int(line.split(b";")[0], 16)
            if size == 0:
                return data
            while len(out) < size:
                out += self._recv()
            piece, out = out[:size], out[size:]
            if lines:
                lines.feed(piece)
            data += piece
            if len(out) >= 2:
                out = out[2:]


class _SseLines:
    """把到达的字节流切成完整 SSE 行并打时间戳。"""

    def __init__(self, cb, send_done):
        self.cb = cb
        self.t0 = send_done
        self.buf = b""

    def feed(self, piece):
        self.buf += piece
        while b"\n" in self.buf:
            line, _, self.buf = self.buf.partition(b"\n")
            self.cb(round((time.monotonic() - self.t0) * 1000, 1),
                    line)

    def flush(self):
        if self.buf:
            self.cb(-1.0, self.buf)
            self.buf = b""


class Probe:
    def __init__(self, args):
        self.args = args
        self.base, self.key = load_connect_info(args.connect_info)
        self.host = self.base.split("//", 1)[1]

    def _conn(self):
        return Connection(self.host, self.key)

    def asr(self):
        wav = Path(self.args.wav).read_bytes()
        payload = json.dumps({
            "model": ASR_MODEL,
            "messages": [{"role": "user", "content": [{
                "type": "input_audio",
                "input_audio": {"data": "data:audio/wav;base64,"
                                         + base64.b64encode(wav).decode()},
            }]}],
            "asr_options": {"language": "zh"},
        }, ensure_ascii=False)
        conn = self._conn()
        try:
            status, phases, body = conn.request(payload)
            text = ""
            try:
                text = json.loads(body)["choices"][0]["message"]["content"]
            except (ValueError, KeyError, IndexError):
                pass
            return {"service": "asr", "status": status,
                    "wav_bytes": len(wav), "transcript": text[:80],
                    **conn.phases, **phases}
        finally:
            conn.close()

    def tts(self):
        payload = json.dumps({
            "model": TTS_MODEL,
            "messages": [{"role": "assistant", "content": self.args.text}],
            "audio": {"format": "pcm16", "voice": TTS_VOICE},
            "stream": True,
        }, ensure_ascii=False)
        blocks = []

        def on_line(offset_ms, line):
            if not line.startswith(b"data: "):
                return
            try:
                chunk = json.loads(line[6:])
                b64 = chunk["choices"][0]["delta"]["audio"]["data"]
            except (ValueError, KeyError, IndexError, TypeError):
                return
            pcm = base64.b64decode(b64)
            if pcm:
                blocks.append({"at_ms": offset_ms, "pcm_bytes": len(pcm)})

        conn = self._conn()
        try:
            status, phases, body = conn.request(payload, sse_line_cb=on_line)
            first_pcm = blocks[0]["at_ms"] if blocks else None
            return {"service": "tts", "status": status,
                    "blocks": len(blocks),
                    "first_pcm_ms": first_pcm,
                    "last_block_ms": blocks[-1]["at_ms"] if blocks else None,
                    "block_offsets_ms": [b["at_ms"] for b in blocks[:16]],
                    "pcm_bytes": sum(b["pcm_bytes"] for b in blocks),
                    **conn.phases, **phases}
        finally:
            conn.close()

    def llm(self):
        payload = json.dumps({
            "model": LLM_MODEL,
            "messages": [{"role": "user", "content": self.args.q}],
            "thinking": {"type": "disabled"},
            "max_completion_tokens": 256,
            "stream": True,
        }, ensure_ascii=False)
        conn = self._conn()
        try:
            status, phases, body = conn.request(payload)
            first_data = None
            for line in body.split(b"\n"):
                if line.startswith(b"data: "):
                    first_data = "seen"
                    break
            return {"service": "llm", "status": status,
                    "first_data": first_data,
                    **conn.phases, **phases}
        finally:
            conn.close()

    def keepalive(self):
        """同一 TLS 连接顺序两个请求：LLM 形状 + TTS 流式形状。"""
        result = {"service": "keepalive"}
        conn = self._conn()
        try:
            llm_payload = json.dumps({
                "model": LLM_MODEL,
                "messages": [{"role": "user", "content": "回复：正常"}],
                "thinking": {"type": "disabled"},
                "max_completion_tokens": 8,
            }, ensure_ascii=False)
            status1, phases1, _ = conn.request(
                llm_payload, connection_header="keep-alive")
            result["round1"] = {"status": status1, "shape": "llm", **phases1}
            tts_payload = json.dumps({
                "model": TTS_MODEL,
                "messages": [{"role": "assistant", "content": "正常"}],
                "audio": {"format": "pcm16", "voice": TTS_VOICE},
                "stream": True,
            }, ensure_ascii=False)
            status2, phases2, _ = conn.request(
                tts_payload, connection_header="keep-alive")
            result["round2"] = {"status": status2, "shape": "tts", **phases2}
            result["reused"] = status1 == 200 and status2 == 200
            return result
        except (OSError, ssl.SSLError) as exc:
            result["reused"] = False
            result["error"] = re.sub(r"tp-[A-Za-z0-9_-]+", "tp-<redacted>",
                                     str(exc))
            return result
        finally:
            conn.close()


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("mode", choices=["asr", "tts", "llm", "keepalive", "all"])
    parser.add_argument("--connect-info", default="connect_info",
                        help="connect_info 路径（默认仓库根目录）")
    parser.add_argument("--wav", help="ASR 输入 WAV（16 kHz 16 bit 单声道）")
    parser.add_argument("--text", default="当前没有异常点位", help="TTS 文本")
    parser.add_argument("--q", default="现在有哪些异常？", help="LLM 问题")
    parser.add_argument("--out", type=Path, help="结果 JSON 输出路径")
    args = parser.parse_args()

    if args.mode in ("asr", "all") and not args.wav:
        raise SystemExit("asr 模式需要 --wav")

    probe = Probe(args)
    results = {
        "utc_window": [datetime.now(timezone.utc).isoformat(), None],
        "endpoint_host": probe.host,
    }
    order = {"asr": [probe.asr], "tts": [probe.tts], "llm": [probe.llm],
             "keepalive": [probe.keepalive],
             "all": [probe.asr, probe.tts, probe.llm, probe.keepalive]}
    for fn in order[args.mode]:
        results[fn.__name__] = fn()
    results["utc_window"][1] = datetime.now(timezone.utc).isoformat()

    text = json.dumps(results, ensure_ascii=False, indent=2)
    print(text)
    if args.out:
        args.out.write_text(text + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
