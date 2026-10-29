#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Inspect production A/B images and effective OTA gates without printing secrets."""
import argparse, hashlib, json, pathlib, re, sys

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--config',required=True,type=pathlib.Path)
    parser.add_argument('--trust-header',required=True,type=pathlib.Path)
    parser.add_argument('--image',action='append',required=True,type=pathlib.Path)
    parser.add_argument('--forbid-material',action='append',default=[],type=pathlib.Path)
    parser.add_argument('--require-deploy-ready',action='store_true')
    args=parser.parse_args()
    config=args.config.read_text()
    enabled=lambda name: re.search(r'^'+re.escape(name)+r'=y$',config,re.M) is not None
    errors=[]
    if enabled('CONFIG_VG_OTA_TEST_FIXTURES'): errors.append('test fixtures enabled')
    for name in ('CONFIG_VG_OTA_NETWORK','CONFIG_NETDEV_PHY_IOCTL','CONFIG_STM32H7_RNG','CONFIG_DEV_RANDOM','CONFIG_DEV_URANDOM_ARCH'):
        if not enabled(name): errors.append('missing '+name)
    if enabled('CONFIG_DEV_URANDOM_XORSHIFT128') or enabled('CONFIG_DEV_URANDOM_CONGRUENTIAL'):
        errors.append('non-cryptographic random source enabled')
    trust={}
    for line in args.trust_header.read_text().splitlines():
        match=re.fullmatch(r'#define (VG_OTA_[A-Z_]+) (".*")',line)
        if match: trust[match[1]]=json.loads(match[2])
    configured=all(trust.get(n) for n in ('VG_OTA_CA_PEM','VG_OTA_SIGNING_PUBLIC_PEM','VG_OTA_SIGNING_KEY_ID'))
    verified=enabled('CONFIG_VG_OTA_VERIFIED_ENTROPY')
    if args.require_deploy_ready and not (configured and verified): errors.append('deployment trust or verified entropy is missing')
    sentinels=[b'vgota fixture',b'ota-net-test',b'VG-OTA-TEST-CA',b'ota-synthetic-password',b'secret-query',
               b'-----BEGIN PRIVATE KEY-----',b'-----BEGIN EC PRIVATE KEY-----',b'-----BEGIN RSA PRIVATE KEY-----']
    for path in args.forbid_material:
        raw=path.read_bytes().strip()
        if not raw: errors.append('empty forbidden-material file')
        else: sentinels.append(raw)
    for index,path in enumerate(args.image):
        data=path.read_bytes()
        if not data: errors.append('empty image '+str(index))
        if any(marker in data for marker in sentinels): errors.append('forbidden test/private material in image '+str(index))
        print('image='+str(index)+' bytes='+str(len(data))+' sha256='+hashlib.sha256(data).hexdigest())
    if errors:
        for error in errors: print('OTA release check: FAIL '+error,file=sys.stderr)
        return 1
    print('OTA release check: PASS fixtures/test material absent; trust='+('configured' if configured else 'unconfigured')+
          '; entropy='+('verified' if verified else 'unverified')+'; deploy_ready='+str(int(configured and verified)))
    return 0
if __name__=='__main__':
    try: sys.exit(main())
    except Exception as exc: raise SystemExit('OTA release check: FAIL '+type(exc).__name__)
