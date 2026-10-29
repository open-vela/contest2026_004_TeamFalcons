#!/usr/bin/env python3
"""Create a device-bound OTA AES-GCM envelope; never print credential fields."""
import argparse, hashlib, json, os, pathlib, struct

def unique(pairs):
    result={}
    for name,value in pairs:
        if name in result: raise ValueError('duplicate configuration field')
        result[name]=value
    return result

def main():
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    p=argparse.ArgumentParser()
    p.add_argument('--config',required=True)
    p.add_argument('--uid',required=True)
    p.add_argument('--output',required=True)
    a=p.parse_args()
    uid=bytes.fromhex(a.uid)
    if len(uid)!=12: raise ValueError('device UID must be exactly 12 bytes')
    raw=pathlib.Path(a.config).read_bytes()
    profile=json.loads(raw,object_pairs_hook=unique)
    if set(profile)!={'broker','port','username','password','allowed_hosts'}:
        raise ValueError('unexpected configuration fields')
    limits={'broker':96,'port':6,'username':64,'password':128}
    for name,size in limits.items():
        value=profile[name]
        if not isinstance(value,str) or not value or len(value.encode())>=size or any(ord(c)<32 or ord(c)==127 for c in value):
            raise ValueError('invalid configuration field length or characters')
    hosts=profile['allowed_hosts']
    if not isinstance(hosts,list) or not 1<=len(hosts)<=2 or any(not isinstance(h,str) or not h or len(h.encode())>=102 for h in hosts):
        raise ValueError('invalid download host allowlist')
    plain=json.dumps(profile,separators=(',',':'),ensure_ascii=False).encode()
    if len(plain)>1024: raise ValueError('configuration too large')
    nonce=os.urandom(12)
    header=b'VGOC'+bytes([2,0,0,0])+uid+nonce+struct.pack('<I',len(plain))
    # Device binding follows the v1 physical-access boundary. UID is public;
    # this does not claim a hardware-protected root key or physical secrecy.
    key=hashlib.sha256(b'VelaGuard:OTA:credentials:v2\0'+uid).digest()
    blob=header+AESGCM(key).encrypt(nonce,plain,header)
    descriptor=os.open(a.output,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    with os.fdopen(descriptor,'wb') as f:
        f.write(blob); f.flush(); os.fsync(f.fileno())
    print('OTA credential envelope written: bytes='+str(len(blob)))
if __name__=='__main__':
    try: main()
    except ImportError: raise SystemExit('pack_credentials requires the cryptography package')
    except Exception as exc: raise SystemExit('OTA credential envelope rejected: '+type(exc).__name__)
