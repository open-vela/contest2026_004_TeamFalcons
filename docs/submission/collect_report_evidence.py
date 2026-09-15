#!/usr/bin/env python3
"""固定报告证据快照，不修改源码或原始日志。"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import urllib.request
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent / "evidence" / "report-evidence.json"
PRS = [
    ("nuttx", 350), ("nuttx", 351), ("nuttx", 352),
    ("nuttx", 353), ("nuttx", 354), ("nuttx-apps", 119),
    ("apps_netutils_mqttc_MQTT-C", 1), ("packages_ai_agent", 32),
]
SOURCES = [
    "AGENTS.md", "docs/submission/REQUIREMENTS.md",
    "app/velaguard/vgpoint.c", "app/velaguard/vg_point_table.c",
    "app/velaguard/vg_alarm_eval.c", "app/velaguard/vg_frame_stats.c",
    "app/velaguard/vg_runtime.c", "app/velaguard/vg_ui_backend_board.c",
    "app/velaguard/vg_agent_seed.c", "app/velaguard/vg_provision.c",
    "app/velaguard/vg_net_mgr.c", "app/velaguard/vg_mqtt_session.c",
    "app/velaguard/velaguard.c", "scripts/configs/velaguard-lvgl.defconfig",
    "gui/main/ui/model/vg_model.c", "gui/main/ui/pages/vg_page_alarm.c",
    "docs/velaguard-host-nsh-protocol.md", "docs/velaguard-mqtt-contract.md",
    "docs/velaguard-expansion-board.md",
    ".trellis/tasks/09-13-hmi-runtime-render/research/runtime-results.md",
    ".trellis/tasks/09-13-hmi-ux-performance/research/final-results.md",
    ".trellis/tasks/09-12-heartbeat-llm-round-system-wedge/prd.md",
    ".trellis/tasks/archive/2026-08/08-30-stage1-agent-ops/research/agent-ops-notes.md",
    "../packages/ai_agent/include/agent_config.h",
    "../packages/ai_agent/src/tools/tool_shell.c",
    "../packages/ai_agent/src/tools/tool_files.c",
]


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def file_record(path: Path) -> dict:
    stat = path.stat()
    return {
        "bytes": stat.st_size,
        "mtime": datetime.fromtimestamp(stat.st_mtime).astimezone().isoformat(),
        "sha256": digest(path),
    }


def perf_record(rel: str) -> dict:
    path = ROOT / rel
    keep = re.compile(
        r"^(vghmi perf: state=|render:|flush:|submit:|page:|nav:|heap:|NuttX )"
        r"|vgagent: ai_agent autostart|vghmi: live ok="
    )
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    selected = [
        {"line": i, "text": line}
        for i, line in enumerate(lines, 1) if keep.search(line)
    ]
    return {"path": rel, **file_record(path), "excerpts": selected}


def agent_record() -> dict:
    rel = "logs/Foleaf/2026-08-30/cursor__5c92cac5-619d-4584-8c7f-3a6ae56c3286.jsonl"
    path = ROOT / rel
    with path.open(encoding="utf-8") as stream:
        for line_no, line in enumerate(stream, 1):
            row = json.loads(line)
            text = row.get("text", "")
            if row.get("seq") != 867 or "elapsed=115s" not in text:
                continue
            excerpts = [
                {"message_line": i, "text": item}
                for i, item in enumerate(text.splitlines(), 1)
                if re.search(r"END status=ok.*elapsed=115s|daily-20260228\.md", item)
            ]
            return {
                "path": rel, **file_record(path), "jsonl_line": line_no,
                "seq": row["seq"], "ts": row["ts"], "role": row["role"],
                "provenance": "原开发会话中由用户粘贴的板端串口输出，原 JSONL 未改动。",
                "excerpts": excerpts,
            }
    raise RuntimeError("The original 115-second Agent record was not found")


def headless_record() -> dict:
    rel = ".debug/hmi-headless/Testing/Temporary/LastTest.log"
    path = ROOT / rel
    keep = re.compile(r"^(Start testing:|End testing:|\d+/\d+ Testing:|Test time =|Test Passed\.|Test Failed\.)")
    excerpts = [
        {"line": i, "text": line}
        for i, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1)
        if keep.search(line)
    ]
    return {"path": rel, **file_record(path), "excerpts": excerpts}


def pr_record(repo: str, number: int) -> dict:
    url = f"https://api.github.com/repos/open-vela/{repo}/pulls/{number}"
    req = urllib.request.Request(url, headers={"User-Agent": "VelaGuard-report-audit"})
    with urllib.request.urlopen(req, timeout=30) as response:
        data = json.load(response)
    if data["base"]["ref"] != "dev-ai-contest-2026":
        raise RuntimeError(f"Unexpected PR target: {url}")
    return {
        "repo": repo, "number": number, "url": data["html_url"],
        "title": data["title"], "state": data["state"],
        "merged_at": data["merged_at"], "base": data["base"]["ref"],
        "head_sha": data["head"]["sha"],
    }


def code_size() -> dict:
    counts = Counter()
    suffixes = {".c", ".h", ".py", ".sh", ".ps1"}
    for area in ("app/velaguard", "gui/main", "gui/headless", "scripts"):
        for path in sorted((ROOT / area).rglob("*")):
            if not path.is_file() or path.suffix.lower() not in suffixes:
                continue
            rel = path.relative_to(ROOT).as_posix()
            if any(part in rel.lower() for part in ("/fonts/", "/font/", "nanomodbus", "/lvgl/", "/vendor/")):
                continue
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
            counts["files"] += 1
            counts["physical_lines"] += len(lines)
            counts["nonempty_lines"] += sum(bool(line.strip()) for line in lines)
    return dict(counts)


def collect() -> dict:
    counts = Counter()
    for path in sorted((ROOT / "logs/Foleaf").rglob("*.jsonl")):
        with path.open(encoding="utf-8") as stream:
            first = stream.readline()
        if first.strip():
            counts[json.loads(first)["tool"]] += 1
    return {
        "captured_at": datetime.now(timezone.utc).isoformat(),
        "note": "Read-only collection. Historical results are not new test runs.",
        "sources": {rel: file_record(ROOT / rel) for rel in SOURCES},
        "performance": [
            perf_record(".debug/hmi-baseline/perf_transcript.txt"),
            perf_record(".debug/hmi-post-opt/perf_transcript.txt"),
        ],
        "agent_20260830": agent_record(),
        "headless_20260913": headless_record(),
        "firmware_files": {
            name: file_record(ROOT / ".debug" / name)
            for name in ("nuttx.bin", "nuttx.hex", "qspi_bootstub.hex")
        },
        "coding_sessions_by_tool": dict(sorted(counts.items())),
        "coding_sessions_total": sum(counts.values()),
        "source_line_inventory": code_size(),
        "pull_requests": [pr_record(repo, number) for repo, number in PRS],
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--complete-local", action="store_true",
                        help="补入本地原始测试记录，保留原快照中的 PR 和会话统计")
    args = parser.parse_args()
    if args.complete_local:
        data = json.loads(OUT.read_text(encoding="utf-8"))
        changed = [rel for rel, item in data["sources"].items()
                   if digest(ROOT / rel) != item["sha256"]]
        if changed:
            raise RuntimeError(f"Original evidence inputs changed: {changed}")
        for rel in SOURCES:
            if rel not in data["sources"]:
                data["sources"][rel] = file_record(ROOT / rel)
        data["headless_20260913"] = headless_record()
        data["local_verified_at"] = datetime.now(timezone.utc).isoformat()
    else:
        data = collect()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Evidence: {OUT}")
    print(f"PRs: {len(data['pull_requests'])}; coding sessions: {data['coding_sessions_total']}")
    print(f"Source inventory: {data['source_line_inventory']}")


if __name__ == "__main__":
    main()
