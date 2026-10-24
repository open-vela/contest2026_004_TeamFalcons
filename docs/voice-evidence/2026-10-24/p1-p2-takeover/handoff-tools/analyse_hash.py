#!/usr/bin/env python3
"""Compare diagnostic public signature inputs against the captured TLS flight."""
import hashlib
import json
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(root / "scripts/voice_test"))
from tls_forensics_verify import load_bundle, parse_streams, decode_scheme, verify_signature

log = Path(sys.argv[1])
text = log.read_text(errors="replace")
tail = text.split("CMD cat /data/forensics/tls-0/bundle.b64", 1)[1]
pieces = []
for line in tail.splitlines():
    line = line.strip()
    if line.startswith("nsh>") and pieces:
        break
    if re.fullmatch(r"[A-Za-z0-9+/=]+", line) and (pieces or line.startswith("VkdUTFMx")):
        pieces.append(line)
files = load_bundle("".join(pieces).encode())
outdir = log.parent / "hash-dump"
outdir.mkdir(exist_ok=True)
for name, data in files.items():
    assert Path(name).name == name
    (outdir / name).write_bytes(data)
info = parse_streams(files["stream-rx-a.bin"], files["stream-tx.bin"])
alg, kind = decode_scheme(info["ske"]["scheme"])
signed = info["client_random"] + info["server_random"] + info["ske"]["params"]
expected = hashlib.new(alg, signed).hexdigest()
failure_text = text.split("[vela_tls] FORENSIC seq=0 ", 1)[0]
labels = dict(re.findall(r"\[tls_hash\] (\w+) off=0 ([0-9a-f]+)", failure_text))
verified, detail = verify_signature(info["certs"][0], signed, info["ske"])
result = {
    "scheme": kind, "hash_algorithm": alg,
    "offline_signature_valid": verified, "offline_detail": detail,
    "receive_copies_match": files["stream-rx-a.bin"] == files["stream-rx-b.bin"],
    "client_random_matches_wire": labels.get("client_random") == info["client_random"].hex(),
    "server_random_matches_wire": labels.get("server_random") == info["server_random"].hex(),
    "ske_hash_matches_wire": labels.get("ske_hash") == expected,
    "expected_ske_hash": expected, "board_ske_hash": labels.get("ske_hash"),
    "wire_server_random": info["server_random"].hex(),
    "board_server_random": labels.get("server_random"),
    "server_random_differences": [i for i, (a, b) in enumerate(zip(
        info["server_random"], bytes.fromhex(labels.get("server_random", "")))) if a != b],
    "hash_matches_board_randoms": labels.get("ske_hash") == hashlib.new(alg,
        bytes.fromhex(labels["client_random"] + labels["server_random"]) +
        info["ske"]["params"]).hexdigest(),
}
print(json.dumps(result, indent=2))
(log.parent / "hash-analysis.json").write_text(json.dumps(result, indent=2) + "\n")
