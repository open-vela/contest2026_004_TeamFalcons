#!/usr/bin/env python3
"""Run a local TLS Broker and exercise ticket 07 against a real VelaGuard.

This harness uses the same MQTT 3.1.1 framing as the host tests, but runs on
the host LAN so the board reaches it through RJ45. It authenticates the device,
checks the expected topic, and drives query -> open -> put -> cancel. The
candidate is deliberately cancelled before local confirmation; ticket 10 owns
the human-confirmed apply path.
"""

from __future__ import annotations

import argparse
import contextlib
import hashlib
import json
import queue
import socket
import ssl
import struct
import subprocess
import threading
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEBUG = ROOT / ".debug"
TEMPORARY_CREDENTIALS = DEBUG / "board-config-mqtt-credentials.json"
TEMPORARY_READY = DEBUG / "board-config-mqtt-ready"


def exact(conn: socket.socket, count: int) -> bytes:
    data = bytearray()
    while len(data) < count:
        part = conn.recv(count - len(data))
        if not part:
            raise EOFError("connection closed")
        data.extend(part)
    return bytes(data)


def packet(conn: socket.socket) -> tuple[int, bytes]:
    kind = exact(conn, 1)[0]
    length = 0
    multiplier = 1
    for _ in range(4):
        value = exact(conn, 1)[0]
        length += (value & 0x7F) * multiplier
        if not value & 0x80:
            break
        multiplier *= 128
    else:
        raise ValueError("invalid remaining length")
    if length > 20_000:
        raise ValueError("oversized fixture frame")
    return kind, exact(conn, length)


def frame(kind: int, payload: bytes) -> bytes:
    remaining = len(payload)
    encoded = bytearray()
    while True:
        value = remaining % 128
        remaining //= 128
        encoded.append(value | (0x80 if remaining else 0))
        if not remaining:
            return bytes([kind]) + bytes(encoded) + payload


def utf(value: str) -> bytes:
    encoded = value.encode("utf-8")
    return struct.pack(">H", len(encoded)) + encoded


def read_utf(data: bytes, position: int) -> tuple[str, int]:
    if position + 2 > len(data):
        raise ValueError("truncated UTF-8 length")
    length = struct.unpack_from(">H", data, position)[0]
    position += 2
    if position + length > len(data):
        raise ValueError("truncated UTF-8 value")
    return data[position : position + length].decode("utf-8"), position + length


def compact_json(value: object) -> bytes:
    return json.dumps(value, separators=(",", ":"), ensure_ascii=False).encode()


class Broker:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.stop = threading.Event()
        self.done = threading.Event()
        self.failure: BaseException | None = None
        self.ready_file = TEMPORARY_READY
        self.run_id = f"{time.time_ns() & 0xFFFFFFFFFFFF:x}"
        self.task_id = f"task-{self.run_id}"
        self.socket = socket.socket()
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.socket.bind((args.bind, args.port))
        self.socket.listen()
        self.socket.settimeout(0.2)
        self.port = self.socket.getsockname()[1]
        self.tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.tls.load_cert_chain(args.certificate, args.private_key)
        self.thread = threading.Thread(target=self.accept, daemon=True)

    def start(self) -> None:
        self.thread.start()

    def accept(self) -> None:
        while not self.stop.is_set():
            try:
                raw, _ = self.socket.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            if (
                not self.args.skip_provision
                and not self.ready_file.exists()
            ):
                raw.close()
                continue
            threading.Thread(
                target=self.connection, args=(raw,), daemon=True
            ).start()

    def publish(
        self,
        conn: ssl.SSLSocket,
        topic: str,
        payload: bytes,
        *,
        flags: int = 0x02,
        packet_id: int,
    ) -> None:
        body = utf(topic)
        if flags & 0x06:
            body += struct.pack(">H", packet_id)
        body += payload
        conn.sendall(frame(0x30 | flags, body))

    def validate_connect(self, payload: bytes) -> tuple[str, int]:
        if len(payload) < 10 or payload[:7] != b"\x00\x04MQTT\x04":
            raise ValueError("invalid CONNECT")
        flags = payload[7]
        position = 10
        client_id, position = read_utf(payload, position)
        username = ""
        password = ""
        if flags & 0x04:
            _, position = read_utf(payload, position)
        if flags & 0x80:
            username, position = read_utf(payload, position)
        if flags & 0x40:
            password, position = read_utf(payload, position)
        if position != len(payload):
            raise ValueError("unexpected CONNECT fields")
        if not client_id.endswith("-config"):
            raise ValueError("unexpected client id")
        if username != self.args.username or password != self.args.password:
            raise PermissionError("MQTT credentials rejected")
        if not flags & 0x02:
            raise ValueError("CleanSession was not requested")
        keepalive = struct.unpack(">H", payload[8:10])[0]
        return client_id[: -len("-config")], keepalive

    def query(self, request_id: str) -> dict:
        return {
            "schema_version": 1,
            "device_id": self.device_id,
            "req_id": request_id,
            "op": "query",
            "scope": "device",
        }

    def candidate(self, snapshot: dict) -> tuple[str, str, str]:
        used_addresses = set(snapshot.get("slave_addresses", []))
        used_ids = set(snapshot.get("point_ids", []))
        address = next(
            (value for value in range(1, 248) if value not in used_addresses),
            None,
        )
        if address is None or len(used_ids) >= int(
            snapshot["capabilities"]["max_points"]
        ):
            raise ValueError("no capacity for the acceptance candidate")
        point_id = f"s{address}_f3_r0"
        if point_id in used_ids:
            raise ValueError("acceptance point id already exists")
        candidate = {
            "candidate_schema_version": 1,
            "mode": "add_sensor",
            "bus": {"baud": 9600},
            "sensor": {"slave_address": address, "model": "CFG-ACCEPT"},
            "manual": {
                "manual_id": "manual-board-accept",
                "sha256": "a" * 64,
            },
            "evidence": [
                {
                    "id": "ev1",
                    "page": 1,
                    "quote": "Register 0x0000 is int16 with scale 0.1.",
                }
            ],
            "points": [
                {
                    "id": point_id,
                    "name": f"AcceptTemp{address}",
                    "addr": address,
                    "fc": 3,
                    "reg": 0,
                    "qty": 1,
                    "dtype": "int16",
                    "scale": 0.1,
                    "offset": 0,
                    "unit": "C",
                    "source": {
                        "evidence_ids": ["ev1"],
                        "original_address": "0x0000",
                        "address_base": 0,
                    },
                }
            ],
        }
        raw = compact_json(candidate)
        return raw.decode("utf-8"), hashlib.sha256(raw).hexdigest(), point_id

    def connection(self, raw: socket.socket) -> None:
        current_stage = "connect"
        snapshot: dict | None = None
        session_id = ""
        candidate_json = ""
        payload_hash = ""
        next_packet_id = 200
        put_request: dict | None = None
        confirm_state_seq = 0
        confirmation_id = ""
        candidate_point_id = ""

        try:
            raw.settimeout(
                self.args.confirm_timeout + 20
                if self.args.await_confirm
                else 20
            )
            with self.tls.wrap_socket(raw, server_side=True) as conn:
                kind, payload = packet(conn)
                if kind != 0x10:
                    raise ValueError("expected CONNECT")
                self.device_id, keepalive = self.validate_connect(payload)
                topic = f"vg/{self.device_id}/config/request"
                state_topic = f"vg/{self.device_id}/config/state"
                conn.sendall(b"\x20\x02\x00\x00")

                while not self.done.is_set():
                    kind, payload = packet(conn)
                    category = kind >> 4
                    if category == 8:
                        packet_id = payload[:2]
                        subscribed, position = read_utf(payload, 2)
                        if (
                            subscribed != topic
                            or position >= len(payload)
                            or payload[position] != 1
                        ):
                            raise ValueError("unexpected subscription")
                        conn.sendall(frame(0x90, packet_id + b"\x01"))
                        current_stage = "negative"
                        wrong = {
                            "schema_version": 1,
                            "device_id": "vg-wrong-device",
                            "req_id": "rq-wrong-device",
                            "op": "query",
                            "scope": "device",
                        }
                        self.publish(
                            conn,
                            "vg/vg-wrong-device/config/request",
                            compact_json(wrong),
                            packet_id=next_packet_id,
                        )
                        next_packet_id += 1
                        time.sleep(0.3)
                        self.publish(
                            conn,
                            topic,
                            compact_json(
                                self.query(f"rq-retained-{self.run_id}")
                            ),
                            flags=0x03,
                            packet_id=next_packet_id,
                        )
                        next_packet_id += 1
                        time.sleep(0.3)
                        self.publish(
                            conn,
                            state_topic,
                            compact_json(
                                self.query(
                                    f"rq-wrong-direction-{self.run_id}"
                                )
                            ),
                            packet_id=next_packet_id,
                        )
                        next_packet_id += 1
                        time.sleep(0.3)
                        current_stage = "query"
                        self.publish(
                            conn,
                            topic,
                            compact_json(
                                self.query(f"rq-device-1-{self.run_id}")
                            ),
                            packet_id=next_packet_id,
                        )
                        next_packet_id += 1
                    elif category == 3:
                        published_topic, position = read_utf(payload, 0)
                        qos = (kind >> 1) & 0x03
                        if published_topic != state_topic or qos != 1 or kind & 0x01:
                            raise ValueError("unexpected state publish")
                        packet_id = payload[position : position + 2]
                        position += 2
                        conn.sendall(frame(0x40, packet_id))
                        state = json.loads(payload[position:])
                        kind_name = state.get("kind")

                        if current_stage == "query":
                            if (
                                kind_name != "device_snapshot"
                                or state.get("req_id")
                                != f"rq-device-1-{self.run_id}"
                            ):
                                raise ValueError("query did not return device_snapshot")
                            snapshot = state
                            (
                                candidate_json,
                                payload_hash,
                                candidate_point_id,
                            ) = self.candidate(snapshot)
                            current_stage = "open"
                            request = {
                                "schema_version": 1,
                                "device_id": self.device_id,
                                "req_id": f"rq-open-1-{self.run_id}",
                                "task_id": self.task_id,
                                "op": "open",
                                "boot_id": snapshot["boot_id"],
                                "link_id": snapshot["link_id"],
                                "expected_open_seq": snapshot["next_open_seq"],
                                "base_hash": snapshot["base_hash"],
                            }
                            self.publish(
                                conn,
                                topic,
                                compact_json(request),
                                packet_id=next_packet_id,
                            )
                            next_packet_id += 1
                        elif current_stage == "open":
                            if (
                                kind_name != "state"
                                or state.get("state") != "opened"
                                or state.get("req_id")
                                != f"rq-open-1-{self.run_id}"
                            ):
                                raise ValueError("open was not accepted")
                            session_id = state["session_id"]
                            current_stage = "put"
                            request = {
                                "schema_version": 1,
                                "device_id": self.device_id,
                                "req_id": f"rq-put-1-{self.run_id}",
                                "task_id": self.task_id,
                                "op": "put",
                                "boot_id": snapshot["boot_id"],
                                "link_id": snapshot["link_id"],
                                "session_id": session_id,
                                "expected_revision": 0,
                                "revision": 1,
                                "base_hash": snapshot["base_hash"],
                                "payload_sha256": payload_hash,
                                "candidate_json": candidate_json,
                            }
                            put_request = request
                            self.publish(
                                conn,
                                topic,
                                compact_json(request),
                                packet_id=next_packet_id,
                            )
                            next_packet_id += 1
                        elif current_stage == "put":
                            if (
                                kind_name != "state"
                                or state.get("state") != "awaiting_local_confirm"
                                or state.get("payload_sha256") != payload_hash
                            ):
                                raise ValueError(
                                    "put did not reach awaiting_local_confirm"
                                )
                            confirm_state_seq = state["state_seq"]
                            confirmation_id = state["confirmation_id"]
                            current_stage = "put_replay"
                            self.publish(
                                conn,
                                topic,
                                compact_json(put_request),
                                packet_id=next_packet_id,
                            )
                            next_packet_id += 1
                        elif current_stage == "put_replay":
                            if (
                                kind_name != "state"
                                or state.get("state") != "awaiting_local_confirm"
                                or state.get("state_seq") != confirm_state_seq
                                or state.get("confirmation_id") != confirmation_id
                            ):
                                raise ValueError(
                                    "duplicate put changed the confirmation"
                                )
                            if self.args.await_confirm:
                                current_stage = "await_local_confirm"
                                print(
                                    "READY config MQTT board: candidate is "
                                    "awaiting local confirmation; click "
                                    "Confirm Add on the board display"
                                )
                                continue
                            current_stage = "cancel"
                            run_resource_probe(self.args.com_port)
                            time.sleep(2)
                            request = {
                                "schema_version": 1,
                                "device_id": self.device_id,
                                "req_id": f"rq-cancel-1-{self.run_id}",
                                "task_id": self.task_id,
                                "op": "cancel",
                                "boot_id": snapshot["boot_id"],
                                "link_id": snapshot["link_id"],
                                "session_id": session_id,
                            }
                            self.publish(
                                conn,
                                topic,
                                compact_json(request),
                                packet_id=next_packet_id,
                            )
                            next_packet_id += 1
                        elif current_stage == "cancel":
                            if (
                                kind_name != "state"
                                or state.get("state") != "cancelled"
                                or state.get("req_id")
                                != f"rq-cancel-1-{self.run_id}"
                            ):
                                raise ValueError("cancel was not accepted")
                            current_stage = "postcheck"
                            self.publish(
                                conn,
                                topic,
                                compact_json(
                                    self.query(
                                        f"rq-device-2-{self.run_id}"
                                    )
                                ),
                                packet_id=next_packet_id,
                            )
                            next_packet_id += 1
                        elif current_stage == "await_local_confirm":
                            if kind_name != "state":
                                raise ValueError(
                                    "invalid response while awaiting local confirmation"
                                )
                            if state.get("state") == "applied":
                                current_stage = "postcheck_applied"
                                self.publish(
                                    conn,
                                    topic,
                                    compact_json(
                                        self.query(
                                            f"rq-device-applied-{self.run_id}"
                                        )
                                    ),
                                    packet_id=next_packet_id,
                                )
                                next_packet_id += 1
                            elif state.get("state") in {
                                "cancelled",
                                "expired",
                                "failed",
                                "recovery_unknown",
                            }:
                                raise ValueError(
                                    f"local confirmation ended as {state.get('state')}"
                                )
                        elif current_stage == "postcheck":
                            if kind_name != "device_snapshot":
                                raise ValueError("post-cancel query failed")
                            if (
                                state["point_count"] != snapshot["point_count"]
                                or state["point_ids"] != snapshot["point_ids"]
                                or state["base_hash"] != snapshot["base_hash"]
                            ):
                                raise ValueError("candidate changed the table")
                            print(
                                "PASS config MQTT board: "
                                f"mode=local-tls device={self.device_id} "
                                f"points={snapshot['point_count']} "
                                "query=ok open=ok put=awaiting_local_confirm "
                                "cancel=ok unchanged=ok"
                            )
                            self.done.set()
                            conn.sendall(frame(0xE0, b""))
                            return
                        elif current_stage == "postcheck_applied":
                            if kind_name != "device_snapshot":
                                raise ValueError("post-apply query failed")
                            expected_count = int(snapshot["point_count"]) + 1
                            if (
                                state["point_count"] != expected_count
                                or candidate_point_id
                                not in state["point_ids"]
                                or state["base_hash"] == snapshot["base_hash"]
                            ):
                                raise ValueError(
                                    "confirmed candidate was not persisted"
                                )
                            print(
                                "PASS config MQTT board apply: "
                                f"mode=local-tls device={self.device_id} "
                                f"points={state['point_count']} "
                                "query=ok open=ok put=awaiting_local_confirm "
                                "local_confirm=applied persisted=ok"
                            )
                            self.done.set()
                            conn.sendall(frame(0xE0, b""))
                            return
                        else:
                            raise ValueError("unexpected response stage")
                    elif category == 12:
                        conn.sendall(b"\xD0\x00")
                    elif category == 4:
                        pass
                    elif category == 14:
                        return
                    else:
                        raise ValueError(f"unexpected MQTT packet {category}")
        except BaseException as error:
            if not self.done.is_set():
                self.failure = error
                detail = ""
                if "state" in locals() and isinstance(state, dict):
                    detail = (
                        f" response_kind={state.get('kind')}"
                        f" response_state={state.get('state')}"
                        f" response_req={state.get('req_id')}"
                        f" response_error={state.get('error')}"
                    )
                print(
                    "FAIL config MQTT board: "
                    f"stage={current_stage} error={type(error).__name__}: "
                    f"{error}{detail}"
                )
                self.done.set()
        finally:
            with contextlib.suppress(OSError):
                raw.close()

    def close(self) -> None:
        self.stop.set()
        self.socket.close()
        self.thread.join(timeout=1)


def run_provision(args: argparse.Namespace, port: int) -> None:
    DEBUG.mkdir(parents=True, exist_ok=True)
    TEMPORARY_READY.unlink(missing_ok=True)
    credentials = {
        "broker": args.advertise_host,
        "port": str(port),
        "username": args.username,
        "password": args.password,
    }
    TEMPORARY_CREDENTIALS.write_text(
        json.dumps(credentials, separators=(",", ":")),
        encoding="utf-8",
    )
    try:
        command = [
            "powershell.exe",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(ROOT / "scripts" / "provision-config-mqtt.ps1"),
            "-CredentialsJson",
            str(TEMPORARY_CREDENTIALS),
            "-ComPort",
            args.com_port,
            "-ContestRoot",
            str(ROOT),
            "-ReadyFile",
            str(TEMPORARY_READY),
            "-OnlineWaitSec",
            "25",
        ]
        result = subprocess.run(command, capture_output=True, text=True, timeout=90)
        output = (result.stdout + result.stderr).strip()
        if output:
            print(output)
        if result.returncode:
            raise RuntimeError("credential provisioning failed")
    finally:
        TEMPORARY_CREDENTIALS.unlink(missing_ok=True)
        TEMPORARY_READY.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8883)
    parser.add_argument("--advertise-host", required=True)
    parser.add_argument("--certificate", type=Path, required=True)
    parser.add_argument("--private-key", type=Path, required=True)
    parser.add_argument("--username", default="config-board")
    parser.add_argument("--password", default="config-board-local-only")
    parser.add_argument("--com-port", default="COM3")
    parser.add_argument("--skip-provision", action="store_true")
    parser.add_argument("--await-confirm", action="store_true")
    parser.add_argument("--confirm-timeout", type=int, default=660)
    args = parser.parse_args()

    broker = Broker(args)
    broker.start()
    print(
        "config MQTT board acceptance: listening on "
        f"{args.bind}:{broker.port} advertise={args.advertise_host}"
    )
    errors: queue.Queue[BaseException] = queue.Queue()
    try:
        if not args.skip_provision:
            provision_thread = threading.Thread(
                target=lambda: (
                    errors.put(error)
                    if (error := run_provision_error(args, broker.port))
                    else None
                ),
                daemon=True,
            )
            provision_thread.start()
        wait_timeout = (
            args.confirm_timeout + 20
            if args.await_confirm
            else 75
        )
        if not broker.done.wait(timeout=wait_timeout):
            raise TimeoutError("board acceptance timed out")
        if broker.failure is not None:
            return 1
        if not errors.empty():
            raise errors.get()
        return 0
    finally:
        broker.close()


def run_provision_error(
    args: argparse.Namespace, port: int
) -> BaseException | None:
    try:
        run_provision(args, port)
        return None
    except BaseException as error:
        return error


def run_resource_probe(com_port: str) -> None:
    command = [
        "powershell.exe",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(ROOT / "scripts" / "config_mqtt_board_probe.ps1"),
        "-ComPort",
        com_port,
    ]
    result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    output = (result.stdout + result.stderr).strip()
    if output:
        print(output)
    if result.returncode:
        raise RuntimeError("board resource probe failed")


if __name__ == "__main__":
    raise SystemExit(main())
