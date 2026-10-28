#!/usr/bin/env python3
"""Encode and sign the fixed 512-byte VelaGuard manifest (P-256, raw r||s)."""
import argparse, hashlib, pathlib, struct, subprocess, time, zlib

def field(text, size):
    raw = text.encode('ascii')
    if not raw or len(raw) >= size or any(c < 32 or c > 126 for c in raw):
        raise ValueError('invalid manifest field')
    return raw.ljust(size, b'\0')

def raw_signature(der):
    if len(der) < 8 or der[0] != 0x30 or der[1] != len(der)-2:
        raise ValueError('invalid ECDSA signature encoding')
    pos, values = 2, []
    for _ in range(2):
        if der[pos] != 2: raise ValueError('invalid ECDSA integer')
        n = der[pos+1]
        value = int.from_bytes(der[pos+2:pos+2+n], 'big')
        if not value or value.bit_length() > 256: raise ValueError('invalid ECDSA integer width')
        values.append(value.to_bytes(32, 'big'))
        pos += 2+n
    if pos != len(der): raise ValueError('invalid ECDSA trailing bytes')
    return b''.join(values)

def make_manifest(image, key, *, device_id, version, task_id, key_id,
                  issued_at, expires_at, model='STM32H750B-DK', slot=1):
    if not 8 <= len(image) <= 8*1024*1024-8192 or expires_at <= issued_at:
        raise ValueError('invalid manifest size or time window')
    # Confirm the key's public curve without exposing private components.
    spki = subprocess.check_output(['openssl','pkey','-in',str(key),'-pubout','-outform','DER'], stderr=subprocess.DEVNULL)
    if bytes.fromhex('06082a8648ce3d030107') not in spki:
        raise ValueError('signing key must use prime256v1 / secp256r1')
    wire = bytearray(512)
    struct.pack_into('<I4BII',wire,0,0x464d4756,1,0,0,2,slot,len(image))
    wire[16:48]=field(version,32); wire[48:88]=field(task_id,40)
    wire[88:120]=hashlib.sha256(image).digest()
    wire[120:152]=field(device_id,32); wire[152:184]=field(model,32)
    struct.pack_into('<QQ',wire,184,issued_at,expires_at)
    wire[200:216]=field(key_id,16)
    signed = subprocess.run(['openssl','dgst','-sha256','-sign',str(key)],
                            input=bytes(wire[:220]),capture_output=True,check=True).stdout
    wire[220:284]=raw_signature(signed)
    struct.pack_into('<I',wire,508,zlib.crc32(wire[:508]))
    return bytes(wire)

def main():
    p=argparse.ArgumentParser()
    for name in ('image','private-key','device-id','version','task-id','key-id','output'):
        p.add_argument('--'+name,required=True)
    p.add_argument('--issued-at',type=int,default=None)
    p.add_argument('--expires-at',type=int,required=True)
    a=p.parse_args()
    image=pathlib.Path(a.image).read_bytes()
    wire=make_manifest(image,a.private_key,device_id=a.device_id,version=a.version,
                       task_id=a.task_id,key_id=a.key_id,issued_at=a.issued_at or int(time.time()),expires_at=a.expires_at)
    pathlib.Path(a.output).write_bytes(wire)
    print('manifest written: bytes=512 image_bytes='+str(len(image)))
if __name__=='__main__': main()
