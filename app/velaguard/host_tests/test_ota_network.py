#!/usr/bin/env python3
"""Real mbedTLS + loopback HTTPS integration; no board, external network or production secrets."""
from pathlib import Path
import concurrent.futures, contextlib, hashlib, http.server, importlib.util, json, os
import socket, ssl, struct, subprocess, threading, time
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'.debug/ota-network-tests'
MBED=ROOT.parent/'apps/crypto/mbedtls/mbedtls'
LIB=ROOT/'.debug/ota-mbedtls-host/library'
OUT.mkdir(parents=True,exist_ok=True)

def run(args, **kw):
    return subprocess.run([str(x) for x in args],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,**kw)
def key(name):
    path=OUT/(name+'.pem')
    run(['openssl','genpkey','-algorithm','EC','-pkeyopt','ec_paramgen_curve:prime256v1','-out',path])
    path.chmod(0o600)
    return path
ca_key=key('ca-key'); server_key=key('server-key'); signing_key=key('signing-key'); other_key=key('other-ca-key')
ca=OUT/'ca.pem'; other_ca=OUT/'other-ca.pem'; server=OUT/'server.pem'; expired=OUT/'expired.pem'
for pem,k in ((ca,ca_key),(other_ca,other_key)):
    run(['openssl','req','-new','-x509','-days','2','-key',k,'-subj','/CN=VG-OTA-TEST-CA','-out',pem])
csr=OUT/'server.csr'
run(['openssl','req','-new','-key',server_key,'-subj','/CN=localhost','-out',csr])
ext=OUT/'extensions.conf'
ext.write_text('subjectAltName=DNS:localhost\nbasicConstraints=CA:FALSE\nkeyUsage=digitalSignature\nextendedKeyUsage=serverAuth\n')
run(['openssl','x509','-req','-in',csr,'-CA',ca,'-CAkey',ca_key,'-CAcreateserial','-days','1','-extfile',ext,'-out',server])
(OUT/'index').write_text(''); (OUT/'serial').write_text('1000\n')
config=OUT/'ca.conf'
config.write_text(f"[ca]\ndefault_ca=authority\n[authority]\ndatabase={OUT/'index'}\nnew_certs_dir={OUT}\nserial={OUT/'serial'}\ncertificate={ca}\nprivate_key={ca_key}\ndefault_md=sha256\npolicy=policy\n[policy]\ncommonName=supplied\n[server]\nsubjectAltName=DNS:localhost\nbasicConstraints=CA:FALSE\nkeyUsage=digitalSignature\nextendedKeyUsage=serverAuth\n")
run(['openssl','ca','-batch','-config',config,'-in',csr,'-out',expired,'-startdate','20200101000000Z','-enddate','20200102000000Z','-extensions','server'])
public=OUT/'signing-public.pem'
run(['openssl','pkey','-in',signing_key,'-pubout','-out',public])
include=OUT/'include'; include.mkdir(exist_ok=True)
(include/'vg_ota_trust_build.h').write_text('#define VG_OTA_CA_PEM '+json.dumps(ca.read_text())+'\n#define VG_OTA_SIGNING_PUBLIC_PEM '+json.dumps(public.read_text())+'\n#define VG_OTA_SIGNING_KEY_ID "ota-net-test"\n')
if not (LIB/'libmbedtls.a').exists():
    run(['cmake','-S',MBED,'-B',LIB.parent,'-DENABLE_TESTING=OFF','-DENABLE_PROGRAMS=OFF','-DMBEDTLS_FATAL_WARNINGS=OFF'])
    run(['cmake','--build',LIB.parent,'-j4'])
helper=OUT/'helper'
sources=[ROOT/'app/velaguard/host_tests/test_ota_network_helper.c']
sources += [ROOT/'app/velaguard'/name for name in ('vg_ota_download.c','vg_ota_http.c','vg_ota_https.c','vg_ota_staging.c','vg_ota_trust.c','vg_fw_identity.c','vg_device_id.c')]
sources += list((ROOT/'boot/ota_core').glob('*.c'))
run(['cc','-Wall','-Werror','-pthread','-DVG_OTA_CRYPTO_MBEDTLS','-DVG_FW_VERSION="0.1.0"','-DVG_FW_TEST_SLOT_MARKER=0x41','-DVG_FW_TEST_LINK_BASE=0x90000000u',
     '-I'+str(include),'-I'+str(ROOT/'app/velaguard'),'-I'+str(ROOT/'boot/ota_core'),'-I'+str(MBED/'include'),
     *sources,LIB/'libmbedtls.a',LIB/'libmbedx509.a',LIB/'libmbedcrypto.a','-o',helper])
spec=importlib.util.spec_from_file_location('pack',ROOT/'scripts/ota/pack_manifest.py'); pack=importlib.util.module_from_spec(spec); spec.loader.exec_module(pack)
image=struct.pack('<II',0x24010000,0x90800101)+bytes(i%256 for i in range(16376))
now=int(time.time()); expires=now+3600
manifests={}
for name,changes in [('good',{}),('bad-device',{'device_id':'vg-wrong'}),('bad-model',{'model':'wrong'}),('downgrade',{'version':'0.0.9'}),('expired',{'issued_at':now-3600,'expires_at':now-10})]:
    args=dict(device_id='vg-'+'ab'*12,version='0.2.0',task_id='T08-'+name,key_id='ota-net-test',issued_at=now-10,expires_at=expires)
    args.update(changes)
    manifests[name]=pack.make_manifest(image,signing_key,**args)
# The helper reads the valid local copy; only server signature bytes are altered.
bad=bytearray(manifests['good']); bad[220]^=1
import zlib
struct.pack_into('<I',bad,508,zlib.crc32(bad[:508])); manifests['bad-signature']=bytes(bad)
requests=[]; counts={}; lock=threading.Lock()
class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version='HTTP/1.1'
    def log_message(self,*args): pass
    def do_GET(self):
        path=self.path.split('?',1)[0]
        if path.startswith('/manifest/'):
            payload=manifests[path.rsplit('/',1)[-1]]
            self.send_response(200); self.send_header('Content-Length',str(len(payload))); self.end_headers(); self.wfile.write(payload); return
        with lock:
            counts[path]=counts.get(path,0)+1; n=counts[path]
            requests.append((path,self.headers.get('Range'),self.headers.get('If-Range')))
        offset=int(self.headers.get('Range','bytes=0-')[6:].split('-')[0])
        status=206 if offset else 200
        etag='"image-v1"'
        if path=='/changed' and n>=2: etag='"image-v2"'
        if path=='/ignore': status=200; offset=0
        if path=='/redirect': self.send_response(302); self.send_header('Content-Length','0'); self.send_header('Location','https://untrusted.invalid/'); self.end_headers(); return
        body=image[offset:]
        if path=='/bad-digest': body=bytes([body[0]^1])+body[1:]
        self.send_response(status)
        if path!='/no-length': self.send_header('Content-Length',str(len(body)))
        else: self.send_header('Transfer-Encoding','chunked')
        if path!='/no-etag': self.send_header('ETag',etag)
        if status==206:
            start=offset+1 if path=='/bad-range' else offset
            self.send_header('Content-Range',f'bytes {start}-{len(image)-1}/{len(image)}')
        self.end_headers()
        if (path in ('/drop','/changed','/ignore','/no-etag','/bad-range') and n==1) or path=='/always-drop':
            self.wfile.write(body[:512 if path=='/always-drop' else 5000]); self.wfile.flush()
            with contextlib.suppress(OSError): self.connection.shutdown(socket.SHUT_RDWR)
            self.connection.close(); self.close_connection=True; return
        with contextlib.suppress(OSError): self.wfile.write(body)
class Server(http.server.ThreadingHTTPServer):
    daemon_threads=True
    def handle_error(self,*args): pass

def start(cert):
    srv=Server(('127.0.0.1',0),Handler)
    tls=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); tls.load_cert_chain(cert,server_key)
    srv.socket=tls.wrap_socket(srv.socket,server_side=True)
    threading.Thread(target=srv.serve_forever,daemon=True).start()
    return srv
srv=start(server); old=start(expired)
port=srv.server_address[1]

def case(name, path='/good', manifest='good', expected=0, *, trust=ca, host='localhost', server_port=port, allowed=None, egress=1):
    local=OUT/(name+'.bin'); local.write_bytes(manifests[manifest if manifest!='bad-signature' else 'good'])
    base=f'https://{host}:{server_port}'
    args=[helper,trust,local,base+'/manifest/'+manifest,base+path+'?token=secret-query',allowed or f'{host}:{server_port}',str(expected),str(egress)]
    result=subprocess.run([str(x) for x in args],capture_output=True,text=True,timeout=30)
    if result.returncode or 'secret-query' in result.stdout+result.stderr or 'https://' in result.stdout+result.stderr:
        raise AssertionError(name+': '+result.stdout+result.stderr)
    print(name+': '+result.stdout.strip())
try:
    case('valid')
    for path in ('drop','changed','ignore','no-etag'):
        case('resume-'+path,'/'+path)
    for name in ('bad-signature','bad-device','bad-model','downgrade','expired'):
        case(name,manifest=name,expected=1)
    for path in ('bad-range','bad-digest','always-drop','redirect','no-length'):
        case('reject-'+path,'/'+path,expected=1)
    case('unknown-ca',trust=other_ca,expected=1)
    case('hostname-mismatch',host='127.0.0.1',expected=1)
    case('expired-certificate',server_port=old.server_address[1],expected=1)
    case('forbidden-host',allowed='other.invalid:443',expected=1)
    case('wifi-rejected',egress=2,expected=1)
    case('no-rj45',egress=0,expected=1)
    assert any(path=='/drop' and rng=='bytes=5000-' and tag=='"image-v1"' for path,rng,tag in requests)
    for path in ('/changed','/ignore'):
        seen=[r for r in requests if r[0]==path]
        assert len(seen)==3 and seen[1][1]=='bytes=5000-' and seen[-1][1] is None, path
    assert all(rng is None for path,rng,_ in requests if path=='/no-etag')
    print('test_ota_network: OK (real TLS/signatures/range; no install authorization)')
finally:
    srv.shutdown(); old.shutdown(); srv.server_close(); old.server_close()
