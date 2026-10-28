#!/usr/bin/env python3
"""Generate a local build header from deployment public trust files."""
import argparse, json, os, pathlib, re, subprocess
p = argparse.ArgumentParser()
p.add_argument('--output', required=True)
a = p.parse_args()
ca_path = os.environ.get('VG_OTA_CA_FILE', '')
key_path = os.environ.get('VG_OTA_PUBLIC_KEY_FILE', '')
key_id = os.environ.get('VG_OTA_SIGNING_KEY_ID', '')
values = ('', '', '')
if any((ca_path, key_path, key_id)):
    if not all((ca_path, key_path, key_id)) or not re.fullmatch(r'[A-Za-z0-9_.-]{1,15}', key_id):
        raise SystemExit('OTA trust: supply CA file, public key file and a valid key id together')
    ca = pathlib.Path(ca_path).read_text()
    key = pathlib.Path(key_path).read_text()
    if len(ca) > 32768 or len(key) > 2048 or 'PRIVATE KEY' in ca + key:
        raise SystemExit('OTA trust: invalid public material')
    # OpenSSL parses the exact supplied materials before they reach a firmware build.
    subprocess.run(['openssl', 'x509', '-in', ca_path, '-noout'], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(['openssl', 'pkey', '-pubin', '-in', key_path, '-noout'], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    values = (ca, key, key_id)
names = ('VG_OTA_CA_PEM', 'VG_OTA_SIGNING_PUBLIC_PEM', 'VG_OTA_SIGNING_KEY_ID')
out = pathlib.Path(a.output)
out.parent.mkdir(parents=True, exist_ok=True)
text = '// Generated from local public trust material; do not commit.\n'
text += ''.join('#define ' + n + ' ' + json.dumps(v) + '\n' for n, v in zip(names, values))
# Preserve mtime when unchanged so normal incremental builds remain incremental.
if not out.exists() or out.read_text() != text:
    out.write_text(text)
print('OTA trust: configured' if all(values) else 'OTA trust: unconfigured (production OTA refuses)')
