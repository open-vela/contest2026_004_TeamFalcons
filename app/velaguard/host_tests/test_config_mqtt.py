#!/usr/bin/env python3
"""Exercise candidate MQTT transport over strict TLS with a local fixture."""

from pathlib import Path
import contextlib
import json
import socket
import ssl
import struct
import subprocess
import threading
import time

ROOT = Path(__file__).resolve().parents[3]
CERT = ROOT / ".debug/ota-network-tests"
OUT = ROOT / ".debug/config-mqtt-tests"
OUT.mkdir(parents=True, exist_ok=True)
MQTT = ROOT.parent / "apps/netutils/mqttc/MQTT-C"
MBED = ROOT.parent / "apps/crypto/mbedtls/mbedtls"
LIB = ROOT / ".debug/ota-mbedtls-host/library"
HELPER = OUT / "helper"

def server_certificate_valid():
    path = CERT / "server.pem"
    if not path.exists():
        return False
    decoded = ssl._ssl._test_decode_cert(str(path))
    now = time.time()
    return (
        ssl.cert_time_to_seconds(decoded["notBefore"])
        <= now
        < ssl.cert_time_to_seconds(decoded["notAfter"])
    )


if not server_certificate_valid():
    subprocess.run(
        ["python3", str(ROOT / "app/velaguard/host_tests/test_ota_network.py")],
        check=True,
    )

SOURCES = [
    ROOT / "app/velaguard/host_tests/test_config_mqtt_helper.c",
    ROOT / "app/velaguard/vg_config_mqtt_transport.c",
    ROOT / "app/velaguard/vg_config_mqtt_protocol.c",
    ROOT / "app/velaguard/vg_ota_https.c",
    ROOT / "app/velaguard/vg_ota_http.c",
    ROOT / "app/velaguard/vg_ota_json.c",
    ROOT / "boot/ota_core/vg_ota_sha256.c",
    MQTT / "src/mqtt.c",
    MQTT / "src/mqtt_pal.c",
    ROOT.parent / "apps/netutils/cjson/cJSON/cJSON.c",
]

COMMAND = [
    "cc",
    "-Wall",
    "-Werror",
    "-pthread",
    f"-I{CERT / 'include'}",
    f"-I{ROOT / 'app/velaguard'}",
    f"-I{ROOT / 'boot/ota_core'}",
    f"-I{MQTT / 'include'}",
    f"-I{MBED / 'include'}",
    f"-I{ROOT.parent / 'apps/netutils/cjson/cJSON'}",
    *map(str, SOURCES),
    str(LIB / "libmbedtls.a"),
    str(LIB / "libmbedx509.a"),
    str(LIB / "libmbedcrypto.a"),
    "-lm",
    "-o",
    str(HELPER),
]

compiled = subprocess.run(COMMAND, capture_output=True, text=True)
if compiled.returncode:
    raise AssertionError(compiled.stderr)

records = []
subscriptions = []
failures = []
lock = threading.Lock()


def exact(conn, count):
    data = b""
    while len(data) < count:
        part = conn.recv(count - len(data))
        if not part:
            raise EOFError()
        data += part
    return data


def packet(conn):
    kind = exact(conn, 1)[0]
    length = 0
    multiplier = 1
    for _ in range(4):
        byte = exact(conn, 1)[0]
        length += (byte & 127) * multiplier
        if not byte & 128:
            break
        multiplier *= 128
    else:
        raise ValueError("invalid remaining length")
    if length > 20000:
        raise ValueError("oversized fixture frame")
    return kind, exact(conn, length)


def frame(kind, payload):
    length = len(payload)
    encoded = b""
    while True:
        byte = length % 128
        length //= 128
        encoded += bytes([byte | (128 if length else 0)])
        if not length:
            return bytes([kind]) + encoded + payload


def utf(value):
    encoded = value.encode()
    return struct.pack(">H", len(encoded)) + encoded


def read_utf(data, position):
    length = struct.unpack_from(">H", data, position)[0]
    position += 2
    if position + length > len(data):
        raise ValueError("truncated UTF-8 field")
    return data[position : position + length].decode(), position + length


def request(device="vg-test"):
    return json.dumps(
        {
            "schema_version": 1,
            "device_id": device,
            "req_id": "req-device-1",
            "op": "query",
            "scope": "device",
        },
        separators=(",", ":"),
    ).encode()


class Broker:
    def __init__(self, mode):
        self.mode = mode
        self.stop = False
        self.socket = socket.socket()
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.socket.bind(("127.0.0.1", 0))
        self.socket.listen()
        self.socket.settimeout(0.2)
        self.port = self.socket.getsockname()[1]
        self.tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.tls.load_cert_chain(CERT / "server.pem", CERT / "server-key.pem")
        self.thread = threading.Thread(target=self.accept, daemon=True)
        self.thread.start()

    def accept(self):
        while not self.stop:
            try:
                connection, _ = self.socket.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            threading.Thread(
                target=self.connection, args=(connection,), daemon=True
            ).start()

    def publish(self, conn, topic, payload, flags, packet_id):
        body = utf(topic)
        if flags & 0x06:
            body += struct.pack(">H", packet_id)
        body += payload
        conn.sendall(frame(0x30 | flags, body))

    def connection(self, raw):
        try:
            raw.settimeout(8)
            with self.tls.wrap_socket(raw, server_side=True) as conn:
                kind, payload = packet(conn)
                assert kind == 0x10 and payload[:7] == b"\x00\x04MQTT\x04"
                flags = payload[7]
                client_id, position = read_utf(payload, 10)
                username, position = (
                    read_utf(payload, position) if flags & 128 else ("", position)
                )
                password, position = (
                    read_utf(payload, position) if flags & 64 else ("", position)
                )
                assert client_id == "vg-test-config"
                accepted = (
                    username == "config-unit"
                    and password == "config-synthetic-password"
                )
                conn.sendall(b"\x20\x02\x00" + (b"\x00" if accepted else b"\x04"))
                if not accepted:
                    return

                while True:
                    kind, payload = packet(conn)
                    category = kind >> 4
                    if category == 8:
                        packet_id = payload[:2]
                        topic, position = read_utf(payload, 2)
                        assert topic == "vg/vg-test/config/request"
                        assert payload[position] == 1
                        with lock:
                            subscriptions.append(topic)
                        if self.mode != "deny-sub":
                            conn.sendall(frame(0x90, packet_id + b"\x01"))
                        else:
                            conn.sendall(frame(0x90, packet_id + b"\x80"))
                            return
                        time.sleep(0.1)
                        if self.mode == "oversize":
                            body = utf("vg/vg-test/config/request")
                            body += struct.pack(">H", 101)
                            body += b"x" * 16385
                            conn.sendall(frame(0x32, body))
                        else:
                            valid = request()
                            self.publish(
                                conn,
                                "vg/vg-test/config/request",
                                valid,
                                0x02,
                                101,
                            )
                    elif category == 3:
                        topic, position = read_utf(payload, 0)
                        qos = (kind >> 1) & 3
                        assert qos == 1 and not kind & 1
                        packet_id = payload[position : position + 2]
                        data = json.loads(payload[position + 2 :])
                        assert topic == "vg/vg-test/config/state"
                        assert data["kind"] == "device_snapshot"
                        with lock:
                            records.append(topic)
                        conn.sendall(frame(0x40, packet_id))
                        if self.mode == "normal":
                            valid = request()
                            for delay, publish_topic, flags, ident in (
                                (
                                    0.1,
                                    "vg/vg-other/config/request",
                                    0x02,
                                    102,
                                ),
                                (
                                    0.1,
                                    "vg/vg-test/config/request",
                                    0x03,
                                    103,
                                ),
                                (
                                    0.1,
                                    "vg/vg-test/config/request",
                                    0x00,
                                    104,
                                ),
                            ):
                                time.sleep(delay)
                                self.publish(
                                    conn,
                                    publish_topic,
                                    valid,
                                    flags,
                                    ident,
                                )
                    elif category == 12:
                        conn.sendall(b"\xd0\x00")
                    elif category == 4:
                        pass
                    elif category == 14:
                        return
                    else:
                        raise AssertionError("unexpected MQTT packet")
        except (EOFError, ConnectionError, OSError, ssl.SSLError, TimeoutError):
            pass
        except BaseException as error:
            with lock:
                failures.append(repr(error))
        finally:
            with contextlib.suppress(OSError):
                raw.close()

    def close(self):
        self.stop = True
        self.socket.close()
        self.thread.join(timeout=1)


def run_case(mode):
    broker = Broker(mode)
    try:
        result = subprocess.run(
            [str(HELPER), str(broker.port), mode],
            capture_output=True,
            text=True,
            timeout=25,
        )
        output = result.stdout + result.stderr
        if result.returncode or "config-synthetic-password" in output:
            with lock:
                fixture_failures = list(failures)
            raise AssertionError(
                f"{mode} failed: {output}; fixture failures={fixture_failures}"
            )
        print(f"{mode}: {result.stdout.strip()}")
    finally:
        broker.close()


if __name__ == "__main__":
    run_case("normal")
    with lock:
        assert subscriptions == ["vg/vg-test/config/request"]
        assert records == ["vg/vg-test/config/state"]
    run_case("wrong-auth")
    run_case("wrong-host")
    run_case("deny-sub")
    run_case("oversize")
    assert not failures, failures
    print(
        "test_config_mqtt: OK "
        "(strict TLS, credentials, SUBACK, QoS1, retained, device and size rejection)"
    )
