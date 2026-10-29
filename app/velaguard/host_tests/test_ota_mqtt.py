#!/usr/bin/env python3
"""Exercise MQTT-C over actual strict TLS using a bounded local protocol fixture."""
from pathlib import Path
import contextlib, json, socket, ssl, struct, subprocess, threading, time
ROOT=Path(__file__).resolve().parents[3]
CERT=ROOT/'.debug/ota-network-tests'; OUT=ROOT/'.debug/ota-mqtt-tests'; OUT.mkdir(parents=True,exist_ok=True)
if not (CERT/'server.pem').exists(): subprocess.run(['python3',str(ROOT/'app/velaguard/host_tests/test_ota_network.py')],check=True)
MQTT=ROOT.parent/'apps/netutils/mqttc/MQTT-C'; MBED=ROOT.parent/'apps/crypto/mbedtls/mbedtls'
LIB=ROOT/'.debug/ota-mbedtls-host/library'; helper=OUT/'helper'
sources=[ROOT/'app/velaguard/host_tests/test_ota_mqtt_helper.c']
sources += [ROOT/'app/velaguard'/n for n in ('vg_ota_mqtt.c','vg_ota_https.c','vg_ota_http.c','vg_ota_trust.c','vg_ota_protocol.c','vg_ota_json.c','vg_fw_identity.c')]
sources += [ROOT/'boot/ota_core'/n for n in ('vg_ota_manifest.c','vg_ota_sha256.c','vg_ota_bcb.c')]
sources += [MQTT/'src/mqtt.c',MQTT/'src/mqtt_pal.c',ROOT.parent/'apps/netutils/cjson/cJSON/cJSON.c']
cmd=['cc','-Wall','-Werror','-pthread','-DVG_OTA_CRYPTO_MBEDTLS','-DVG_FW_TEST_SLOT_MARKER=0x41','-DVG_FW_TEST_LINK_BASE=0x90000000u',
     '-I'+str(CERT/'include'),'-I'+str(ROOT/'app/velaguard'),'-I'+str(ROOT/'boot/ota_core'),'-I'+str(MQTT/'include'),'-I'+str(MBED/'include'),'-I'+str(ROOT.parent/'apps/netutils/cjson/cJSON'),
     *map(str,sources),str(LIB/'libmbedtls.a'),str(LIB/'libmbedx509.a'),str(LIB/'libmbedcrypto.a'),'-lm','-o',str(helper)]
compiled=subprocess.run(cmd,capture_output=True,text=True)
if compiled.returncode: raise AssertionError(compiled.stderr)
records=[]; failures=[]; subscriptions=[]; locks=threading.Lock()

def exact(c,n):
    out=b''
    while len(out)<n:
        got=c.recv(n-len(out))
        if not got: raise EOFError()
        out+=got
    return out

def packet(c):
    kind=exact(c,1)[0]; n=0; multiplier=1
    for _ in range(4):
        b=exact(c,1)[0]; n+=(b&127)*multiplier
        if not b&128: break
        multiplier*=128
    else: raise ValueError('remaining length')
    if n>4096: raise ValueError('oversized fixture frame')
    return kind,exact(c,n)

def frame(kind,payload):
    n=len(payload); encoded=b''
    while True:
        b=n%128; n//=128; encoded+=bytes([b|(128 if n else 0)])
        if not n: return bytes([kind])+encoded+payload

def utf(s):
    b=s.encode(); return struct.pack('>H',len(b))+b

def read_utf(data,pos):
    n=struct.unpack_from('>H',data,pos)[0]; pos+=2
    if pos+n>len(data): raise ValueError('truncated UTF')
    return data[pos:pos+n].decode(),pos+n

class Broker:
    def __init__(self,mode='tls',device='vg-test',offer=None,expected_task='TASK-09'):
        self.mode=mode; self.stop=False; self.dropped=False
        self.device=device; self.offer=offer; self.expected_task=expected_task; self.messages=[]
        self.socket=socket.socket(); self.socket.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
        self.socket.bind(('127.0.0.1',0)); self.socket.listen(); self.socket.settimeout(.2)
        self.port=self.socket.getsockname()[1]
        self.context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.context.load_cert_chain(CERT/'server.pem',CERT/'server-key.pem')
        self.thread=threading.Thread(target=self.accept,daemon=True); self.thread.start()
    def accept(self):
        while not self.stop:
            try: conn,_=self.socket.accept()
            except socket.timeout: continue
            except OSError: break
            threading.Thread(target=self.connection,args=(conn,),daemon=True).start()
    def connection(self,raw):
        try:
            raw.settimeout(30 if self.mode=="no-puback" else 5)
            if self.mode=='plain': raw.sendall(b'\x20\x02\x00\x00'); return
            with self.context.wrap_socket(raw,server_side=True) as c:
                kind,p=packet(c); assert kind==0x10 and p[:7]==b'\x00\x04MQTT\x04'
                suppress_ack=False
                flags=p[7]; client,pos=read_utf(p,10)
                username,pos=read_utf(p,pos) if flags&128 else ('',pos)
                password,pos=read_utf(p,pos) if flags&64 else ('',pos)
                ok=username=='ota-unit' and password=='ota-synthetic-password'
                c.sendall(b'\x20\x02\x00'+(b'\x00' if ok else b'\x04'))
                if not ok: return
                while True:
                    kind,p=packet(c); category=kind>>4
                    if category==8:
                        pid=p[:2]; topic,pos=read_utf(p,2)
                        assert topic==f'vg/{self.device}/ota/offer' and p[pos]==1
                        with locks: subscriptions.append(topic)
                        c.sendall(frame(0x90,pid+(b'\x80' if self.mode=='deny-sub' else b'\x01')))
                        if self.mode=='deny-sub': return
                        offer={'schema':1,'device_id':self.device,'task_id':'TASK-09','version':'0.2.0','manifest_url':'https://updates.example/m?token=secret-query','image_url':'https://updates.example/i?token=secret-query','image_size':16384,'sha256':'00'*32,'expires_at':1900000000}
                        if self.offer is False: continue
                        offer=self.offer or offer
                        body=json.dumps(offer,separators=(',',':')).encode()
                        for ident,(topic,flag,data) in enumerate([
                            (f'vg/{self.device}/ota/offer',0x32,body),
                            (f'vg/{self.device}/ota/confirm',0x32,b'{"confirm":true}'),
                            (f'vg/{self.device}/ota/offer',0x33,body)],101):
                            c.sendall(frame(flag,utf(topic)+struct.pack('>H',ident)+data))
                    elif category==3:
                        topic,pos=read_utf(p,0); qos=(kind>>1)&3
                        assert qos==1 and not kind&1
                        pid=p[pos:pos+2]; payload=p[pos+2:]
                        assert b'secret-query' not in payload and b'password' not in payload and b'https://' not in payload
                        data=json.loads(payload)
                        assert isinstance(data.get('task_id'),str) and isinstance(data.get('version'),str)
                        if data['task_id']: assert data['task_id']==self.expected_task
                        self.messages.append((topic,data))
                        with locks: records.append(topic)
                        if topic.endswith('/result') and not self.dropped and self.mode in ('drop-result','no-puback'):
                            self.dropped=True
                            if self.mode=='drop-result': return
                            suppress_ack=True
                        if not suppress_ack: c.sendall(frame(0x40,pid))
                    elif category==12: c.sendall(b'\xd0\x00')
                    elif category==14: return
                    elif category!=4: raise AssertionError('unexpected MQTT packet')
        except (EOFError,ssl.SSLError,TimeoutError,ConnectionError,OSError): pass
        except BaseException as exc:
            with locks: failures.append(type(exc).__name__)
        finally:
            with contextlib.suppress(OSError): raw.close()
    def close(self):
        self.stop=True; self.socket.close(); self.thread.join(timeout=1)

def run_case(mode,client_mode='normal',reject=False):
    broker=Broker(mode)
    try:
        result=subprocess.run([str(helper),str(broker.port),client_mode,str(int(reject))],capture_output=True,text=True,timeout=20)
        if result.returncode or any(x in result.stdout+result.stderr for x in ('ota-synthetic-password','secret-query','https://')):
            raise AssertionError('MQTTS fixture failed: '+result.stdout+result.stderr)
        print(mode+'/'+client_mode+': '+result.stdout.strip())
    finally: broker.close()
if __name__=='__main__':
    run_case('tls')
    run_case('tls','wrong-auth',True)
    run_case('tls','wrong-host',True)
    run_case('deny-sub',reject=True)
    run_case('plain',reject=True)
    assert not failures,failures
    assert len(subscriptions)>=3
    for name in ('status','result','confirm'): assert 'vg/vg-test/ota/'+name in records
    print('test_ota_mqtt: OK (TLS, credential rejection, subscription, QoS1, reconnect and safe metadata)')
