#!/usr/bin/env python3
"""VelaGuard TLS handshake forensics verifier.

Consumes the evidence set written by CONFIG_AI_AGENT_TLS_FORENSICS
(/data/forensics/tls-<n>/ on the board) and answers, for one failed
handshake:

  1. Is the peer certificate byte-identical to the chain the server
     presents right now (SHA-256 against a freshly fetched reference)?
  2. Are the two independently captured receive streams identical?
  3. Does the ServerKeyExchange signature verify offline against the
     captured certificate, using the randoms and params captured from
     the byte streams themselves?

That maps onto the root-cause decision tree:

  cert mismatch            -> corruption before the capture point
                              (network / IOB / copy path)
  cert match, streams ok,
  offline verify PASSES    -> bytes were correct; the on-board verify
  but board reported fail     failed on corrupted heap objects
                              (RSA bignum / hash context corruption)
  cert match, captured
  bytes fail offline       -> the bytes delivered to the socket were
  verify                    already corrupt

Usage:
  python3 tls_forensics_verify.py DIR_OR_BUNDLE [options]
  python3 tls_forensics_verify.py --self-test            # live sanity check

  DIR_OR_BUNDLE  directory with cert.der/stream-rx-a.bin/... or a
                 single bundle.b64 file (or a serial `cat` capture of it).
  --reference FILE   reuse a saved reference json instead of fetching
  --save-reference FILE   fetch chain from --host and store it
  --host HOST        reference host (default token-plan-cn.xiaomimimo.com)

Secrets: this tool only ever touches public handshake data (certificate,
randoms, ECDHE params, signature). It never sees key material.
"""
import base64
import hashlib
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

DEFAULT_HOST = "token-plan-cn.xiaomimimo.com"


# ── reference chain ────────────────────────────────────────────────
def fetch_reference(host: str) -> dict:
    pem = subprocess.run(
        ["openssl", "s_client", "-showcerts", "-connect", f"{host}:443",
         "-servername", host],
        input=b"", capture_output=True, timeout=15, check=True).stdout
    blocks = re.findall(rb"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----",
                        pem, re.S)
    certs = [base64.b64decode(b"".join(c.splitlines()[1:-1])) for c in blocks]
    return {"host": host,
            "fingerprints": [hashlib.sha256(c).hexdigest() for c in certs],
            "pems": [c.decode() for c in blocks]}


# ── TLS stream parsing ─────────────────────────────────────────────
def split_records(stream: bytes):
    out, i = [], 0
    while i + 5 <= len(stream):
        ctype, ver, ln = stream[i], stream[i + 1:i + 3], int.from_bytes(stream[i + 3:i + 5], "big")
        if i + 5 + ln > len(stream):
            out.append((ctype, stream[i + 5:], True))
            break
        out.append((ctype, stream[i + 5:i + 5 + ln], False))
        i += 5 + ln
    return out


def split_handshake(payload: bytes):
    """Split concatenated handshake payload into (type, body) messages."""
    msgs, i = [], 0
    while i + 4 <= len(payload):
        mtype = payload[i]
        ln = int.from_bytes(payload[i + 1:i + 4], "big")
        if i + 4 + ln > len(payload):
            break  # truncated tail (capture overflow) — keep what parsed
        msgs.append((mtype, payload[i + 4:i + 4 + ln]))
        i += 4 + ln
    return msgs


def parse_streams(rx: bytes, tx: bytes) -> dict:
    hs_rx = b"".join(p for t, p, _ in split_records(rx) if t == 22)
    hs_tx = b"".join(p for t, p, _ in split_records(tx) if t == 22)
    info = {"client_random": None, "server_random": None,
            "certs": [], "ske": None, "truncated": any(tr for _, _, tr in split_records(rx))}
    for mtype, body in split_handshake(hs_tx):
        if mtype == 1 and len(body) >= 34:
            info["client_random"] = body[2:34]
    for mtype, body in split_handshake(hs_rx):
        if mtype == 2 and len(body) >= 34:
            info["server_random"] = body[2:34]
        elif mtype == 11 and len(body) >= 3:
            total = int.from_bytes(body[0:3], "big")
            j, end = 3, min(3 + total, len(body))
            while j + 3 <= end:
                cl = int.from_bytes(body[j:j + 3], "big")
                if j + 3 + cl > end:
                    break
                info["certs"].append(body[j + 3:j + 3 + cl])
                j += 3 + cl
        elif mtype == 12 and len(body) >= 4:
            if body[0] != 3:
                raise SystemExit(f"non-ECDHE key exchange (curve_type={body[0]}), unsupported")
            named = int.from_bytes(body[1:3], "big")
            plen = body[3]
            params = body[0:4 + plen]
            k = 4 + plen
            if k + 4 > len(body):
                raise SystemExit("SKE truncated in capture")
            scheme = (body[k], body[k + 1])   # hash, sig — or 0x08xx PSS scheme
            slen = int.from_bytes(body[k + 2:k + 4], "big")
            info["ske"] = {"named_curve": named, "params": params,
                           "scheme": scheme,
                           "signature": body[k + 4:k + 4 + slen]}
    return info


def decode_scheme(scheme: tuple):
    """RFC5246 1+1 hash/sig, or the 0x08xx PSS scheme encoding."""
    b0, b1 = scheme
    if b0 == 0x08:
        table = {0x04: ("sha256", "rsa_pss"), 0x05: ("sha384", "rsa_pss"),
                 0x06: ("sha512", "rsa_pss")}
        return table.get(b1)
    classic_hash = {0x02: "sha1", 0x03: "sha224", 0x04: "sha256",
                    0x05: "sha384", 0x06: "sha512"}.get(b0)
    classic_sig = {0x01: "rsa_pkcs1", 0x03: "ecdsa"}.get(b1)
    if classic_hash and classic_sig:
        return classic_hash, classic_sig
    return None


def verify_signature(cert_der: bytes, signed_data: bytes, ske: dict) -> tuple:
    """openssl dgst based TLS1.2 SKE signature check. Returns (ok, detail)."""
    dec = decode_scheme(ske["scheme"])
    if not dec:
        return False, f"unsupported sig scheme {ske['scheme'][0]:#04x}/{ske['scheme'][1]:#04x}"
    halg, salg = dec
    if not salg.startswith("rsa"):
        return False, f"non-RSA signature {salg}, unsupported"
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        (td / "cert.der").write_bytes(cert_der)
        pub = subprocess.run(
            ["openssl", "x509", "-inform", "DER", "-in", str(td / "cert.der"),
             "-pubkey", "-noout"], capture_output=True, check=True).stdout
        (td / "pub.pem").write_bytes(pub)
        (td / "data.bin").write_bytes(signed_data)
        (td / "sig.bin").write_bytes(ske["signature"])
        cmd = ["openssl", "dgst", f"-{halg}", "-verify", str(td / "pub.pem"),
               "-signature", str(td / "sig.bin"), str(td / "data.bin")]
        if salg == "rsa_pss":
            digest_len = {"sha256": 32, "sha384": 48, "sha512": 64}[halg]
            cmd[-1:-1] = ["-sigopt", "rsa_padding_mode:pss",
                          "-sigopt", f"rsa_pss_saltlen:{digest_len}"]
        r = subprocess.run(cmd, capture_output=True)
        return r.returncode == 0, r.stdout.decode().strip()


# ── board dump loading ─────────────────────────────────────────────
CONTAINER_RE = re.compile(rb"file (\S+) (\d+)")


def load_bundle(raw: bytes) -> dict:
    """Parse the VGTLS1 container (raw or base64, with serial noise)."""
    if raw[:6] != b"VGTLS1":
        # base64: keep only the charset, tolerate serial interleaving
        lines = re.findall(rb"[A-Za-z0-9+/=]+", raw)
        raw = base64.b64decode(b"".join(lines))
    head, _, data = raw.partition(b"data\n")
    files = {}
    off = 0
    for name, ln in CONTAINER_RE.findall(head):
        ln = int(ln)
        files[name.decode()] = data[off:off + ln]
        off += ln
    return files


def load_dump(source: Path) -> dict:
    if source.is_dir():
        files = {f.name: f.read_bytes() for f in source.iterdir() if f.is_file()}
    else:
        files = load_bundle(source.read_bytes())
        sib = source.parent
        for extra in ("meta.txt",):
            if (sib / extra).exists():
                files.setdefault(extra, (sib / extra).read_bytes())
    if "stream-rx-a.bin" not in files or "cert.der" not in files:
        raise SystemExit(f"dump incomplete, found: {sorted(files)}")
    return files


# ── main ───────────────────────────────────────────────────────────
def analyze(files: dict, reference: dict) -> int:
    meta = files.get("meta.txt", b"").decode(errors="replace")
    cert = files["cert.der"]
    rx_a, rx_b = files["stream-rx-a.bin"], files.get("stream-rx-b.bin", b"")
    tx = files.get("stream-tx.bin", b"")
    cert_fp = hashlib.sha256(cert).hexdigest()

    print(f"meta ret line : {[l for l in meta.splitlines() if l.startswith('ret ')]}")
    print(f"cert len/sha  : {len(cert)} {cert_fp[:32]}...")
    print(f"reference     : host={reference['host']} chain={[f[:16] for f in reference['fingerprints']]}")
    cert_match = cert_fp in reference["fingerprints"]
    print(f"cert vs ref   : {'MATCH' if cert_match else 'MISMATCH'}")
    streams_eq = (not rx_b) or rx_a == rx_b
    print(f"rx a==b       : {'match' if streams_eq else 'DIFFER'} ({len(rx_a)}/{len(rx_b)} bytes)")
    for line in meta.splitlines():
        if line.startswith("ptr_"):
            print(f"  {line}")

    info = parse_streams(rx_a, tx)
    print(f"parsed        : client_random={'ok' if info['client_random'] else 'MISSING'} "
          f"server_random={'ok' if info['server_random'] else 'MISSING'} "
          f"certs={len(info['certs'])} ske={'ok' if info['ske'] else 'MISSING'} "
          f"truncated={info['truncated']}")

    if not (info["ske"] and info["client_random"] and info["server_random"]):
        print("VERDICT: INCONCLUSIVE — capture incomplete, enlarge FORENSIC_RX_CAP")
        return 2

    # Cross-check: certificate carried in the stream vs cert.der file.  When
    # the board failed before finishing the Certificate message the file is
    # empty; the stream copy is then the authoritative certificate.
    stream_fp = [hashlib.sha256(c).hexdigest() for c in info["certs"]]
    print(f"stream certs  : {[f[:16] for f in stream_fp]} "
          f"({'contains file cert' if cert_fp in stream_fp else 'file cert NOT in stream'})")
    if not cert and info["certs"]:
        print("cert.der empty on board — falling back to stream leaf certificate")
        cert = info["certs"][0]
        cert_fp = hashlib.sha256(cert).hexdigest()
        cert_match = cert_fp in reference["fingerprints"]
        print(f"cert vs ref   : {'MATCH' if cert_match else 'MISMATCH'} (via stream copy)")

    signed = info["client_random"] + info["server_random"] + info["ske"]["params"]
    ok, detail = verify_signature(cert, signed, info["ske"])
    print(f"offline verify: {'PASS' if ok else 'FAIL'} ({detail})")

    if not cert_match:
        print("VERDICT: corruption BEFORE capture point (network/IOB/copy path) → 阶段2B 网络支")
        return 3
    if ok and streams_eq:
        print("VERDICT: bytes correct but on-board verify failed → heap-side corruption (阶段2A 堆支)")
        return 4
    print("VERDICT: captured bytes themselves corrupt → in_msg/input-buffer residency (阶段2A 堆支, 缩小到输入缓冲)")
    return 5


def self_test() -> int:
    reference = fetch_reference(DEFAULT_HOST)
    p = subprocess.run(
        ["openssl", "s_client", "-msg", "-connect", f"{DEFAULT_HOST}:443",
         "-servername", DEFAULT_HOST, "-tls1_2"],
        input=b"", capture_output=True, timeout=15, check=True)
    # -msg prints hex byte lines under <<< (received) / >>> (sent) markers.
    streams = {"rx": b"", "tx": b""}
    key = None
    for line in p.stdout.decode(errors="replace").splitlines():
        if line.startswith("<<<"):
            key = "rx"
        elif line.startswith(">>>"):
            key = "tx"
        elif key is not None:
            hs = line.strip()
            if re.fullmatch(r"([0-9a-f]{2} )+[0-9a-f]{2}", hs):
                streams[key] += bytes.fromhex(hs.replace(" ", ""))
    info = parse_streams(streams["rx"], streams["tx"])
    assert info["ske"] and info["client_random"] and info["server_random"], \
        f"parse failed: {info}"
    cert = info["certs"][0]
    assert hashlib.sha256(cert).hexdigest() in reference["fingerprints"], \
        "self-test cert does not match reference"
    signed = info["client_random"] + info["server_random"] + info["ske"]["params"]
    ok, detail = verify_signature(cert, signed, info["ske"])
    print(f"self-test     : stream parse ok, cert matches reference, "
          f"offline verify {'PASS' if ok else 'FAIL'} ({detail})")
    return 0 if ok else 1


def main():
    args = sys.argv[1:]
    if args and args[0] == "--self-test":
        sys.exit(self_test())
    if not args:
        sys.exit(__doc__)
    source = Path(args[0])
    opts = dict(zip(args[1::2], args[2::2]))
    host = opts.get("--host", DEFAULT_HOST)
    ref_path = opts.get("--reference")
    if opts.get("--save-reference"):
        ref = fetch_reference(host)
        Path(opts["--save-reference"]).write_text(json.dumps(ref))
        print(f"saved reference: {len(ref['fingerprints'])} certs")
        return
    if ref_path:
        reference = json.loads(Path(ref_path).read_text())
    else:
        reference = fetch_reference(host)
    sys.exit(analyze(load_dump(source), reference))


if __name__ == "__main__":
    main()
