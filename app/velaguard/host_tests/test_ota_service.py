#!/usr/bin/env python3
"""Complete host service: signed Offer over real MQTTS -> HTTPS -> READY -> durable result."""
from pathlib import Path
import contextlib, hashlib, http.server, importlib.util, json, ssl, subprocess, threading, time
ROOT=Path(__file__).resolve().parents[3]; OUT=ROOT/'.debug/ota-service-tests'; OUT.mkdir(parents=True,exist_ok=True)
module_spec=importlib.util.spec_from_file_location('mqtt_fixture',ROOT/'app/velaguard/host_tests/test_ota_mqtt.py')
mqtt_fixture=importlib.util.module_from_spec(module_spec); module_spec.loader.exec_module(mqtt_fixture)
CERT=mqtt_fixture.CERT; MBED=mqtt_fixture.MBED; LIB=mqtt_fixture.LIB; MQTT=mqtt_fixture.MQTT
pack_spec=importlib.util.spec_from_file_location('pack',ROOT/'scripts/ota/pack_manifest.py'); pack=importlib.util.module_from_spec(pack_spec); pack_spec.loader.exec_module(pack)
image=bytes.fromhex('0000012401018090')+bytes(i%256 for i in range(16376))
expires=int(time.time())+3600
manifest=pack.make_manifest(image,CERT/'signing-key.pem',device_id='vg-'+'ab'*12,version='0.2.0',task_id='TASK-SERVICE-09',key_id='ota-net-test',issued_at=int(time.time())-10,expires_at=expires)
requests=[]
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self,*args): pass
    def do_GET(self):
        requests.append(self.path.split('?',1)[0])
        body=manifest if self.path.startswith('/manifest') else image
        self.send_response(200); self.send_header('Content-Length',str(len(body))); self.send_header('ETag','"service-image"'); self.end_headers()
        self.wfile.write(body)
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
tls=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); tls.load_cert_chain(CERT/'server.pem',CERT/'server-key.pem'); server.socket=tls.wrap_socket(server.socket,server_side=True)
threading.Thread(target=server.serve_forever,daemon=True).start()
authority='localhost:'+str(server.server_address[1]); base='https://'+authority
fields={'schema':1,'device_id':'vg-'+'ab'*12,'task_id':'TASK-SERVICE-09','version':'0.2.0','manifest_url':base+'/manifest?token=secret-query','image_url':base+'/image?token=secret-query','image_size':len(image),'sha256':hashlib.sha256(image).hexdigest(),'expires_at':expires}
sources=[ROOT/'app/velaguard/host_tests/test_ota_service_helper.c']
sources += [ROOT/'app/velaguard'/n for n in ('vg_ota_service.c','vg_ota_mqtt.c','vg_ota_download.c','vg_ota_https.c','vg_ota_http.c','vg_ota_trust.c','vg_ota_protocol.c','vg_ota_json.c','vg_ota_outbox.c','vg_ota_staging.c','vg_ota_self_check.c','vg_file_store.c','vg_device_id.c','vg_fw_identity.c')]
sources += list((ROOT/'boot/ota_core').glob('*.c'))+[MQTT/'src/mqtt.c',MQTT/'src/mqtt_pal.c',ROOT.parent/'apps/netutils/cjson/cJSON/cJSON.c']

def compile_helper(slot):
    target=OUT/('helper-'+slot)
    args=['cc','-Wall','-Werror','-pthread','-DVG_OTA_CRYPTO_MBEDTLS','-DVG_FW_VERSION="0.2.0"','-DVG_FW_TEST_SLOT_MARKER='+('0x41' if slot=='a' else '0x42'),'-DVG_FW_TEST_LINK_BASE='+('0x90000000u' if slot=='a' else '0x90800000u'),
          '-I'+str(CERT/'include'),'-I'+str(ROOT/'app/velaguard'),'-I'+str(ROOT/'boot/ota_core'),'-I'+str(ROOT/'scripts/qspi_boot_stub'),'-I'+str(MQTT/'include'),'-I'+str(MBED/'include'),'-I'+str(ROOT.parent/'apps/netutils/cjson/cJSON'),*map(str,sources),str(LIB/'libmbedtls.a'),str(LIB/'libmbedx509.a'),str(LIB/'libmbedcrypto.a'),'-lm','-o',str(target)]
    c=subprocess.run(args,capture_output=True,text=True)
    if c.returncode: raise AssertionError(c.stderr)
    return target

def run(helper,mode,directory,offer,task,broker_mode="tls"):
    directory.mkdir(exist_ok=True)
    broker=mqtt_fixture.Broker(mode=broker_mode,device=fields['device_id'],offer=offer,expected_task=task)
    try:
        result=subprocess.run([str(helper),str(broker.port),authority,mode,str(directory)],capture_output=True,text=True,timeout=40)
        if result.returncode or any(v in result.stdout+result.stderr for v in ('ota-synthetic-password','secret-query','https://')): raise AssertionError(result.stdout+result.stderr)
        print(result.stdout.strip())
        return list(broker.messages)
    finally: broker.close()
try:
    helper=compile_helper('a'); helper_b=compile_helper('b')
    import tempfile
    with tempfile.TemporaryDirectory(prefix='run-',dir=OUT) as temporary:
        directory=Path(temporary)
        first=run(helper,'normal',directory,fields,'TASK-SERVICE-09')
        assert any(t.endswith('/status') and d['state']=='READY_AWAITING_CONFIRM' for t,d in first)
        assert any(t.endswith('/result') and d['result']=='declined' for t,d in first)
        before=len(requests)
        replay=run(helper,'replay',directory,fields,'TASK-SERVICE-09')
        assert len(requests)==before, 'terminal task replay caused a new HTTPS request'
        assert any(t.endswith('/result') and d['result']=='declined' for t,d in replay)
    with tempfile.TemporaryDirectory(prefix='boot-',dir=OUT) as temporary:
        boot=run(helper_b,'boot',Path(temporary),False,'TASK-BOOT-09')
        assert any(t.endswith('/result') and d['result']=='installed' for t,d in boot)
        assert any(t.endswith('/confirm') and d['self_check']=='passed' for t,d in boot)
    for broker_mode in ('drop-result','no-puback'):
        with tempfile.TemporaryDirectory(prefix=broker_mode+'-',dir=OUT) as temporary:
            reports=run(helper,'normal',Path(temporary),fields,'TASK-SERVICE-09',broker_mode)
            assert sum(t.endswith('/result') and d['result']=='declined' for t,d in reports)>=2
        print('service reconnect: OK '+broker_mode+' (unacknowledged result retained and resent)')
    assert not mqtt_fixture.failures,mqtt_fixture.failures
    print('test_ota_service: OK (signed network pipeline, terminal replay suppression, synthetic boot result recovery)')
finally:
    server.shutdown(); server.server_close()
