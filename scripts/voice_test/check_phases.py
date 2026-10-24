#!/usr/bin/env python3
"""P0 板端相位分析：从语音矩阵日志提取每轮各阶段耗时与传输相位。

输入是一个或多个矩阵会话日志（每个文件视为一个会话，文件名
p0-<组>-s<序号>.log 提供组标签），输出每轮一行的 CSV 和按组×阶段
汇总的 JSON。串口乱码防御沿用 check_matrix.py 的规则：非单调或与
已完成阶段矛盾的字段置 None，不进入成功统计。

只读日志，不重算内容正确性；30 条问句的语义判定仍由 check_matrix.py
负责，本脚本不改它。

用法：
  python3 check_phases.py --out-dir phases/ logs/p0-idle-s1.log logs/p0-critical-s*.log
  python3 check_phases.py --out-dir phases/ --host-probe host/mimo-probe.json logs/...
"""

import argparse
import csv
import json
import re
import statistics
from pathlib import Path

STAGE_RE = re.compile(r"\[vgvoice\] stage=(\w+) t=(\d+)")
PHASES_RE = re.compile(
    r"\[vgvoice\] (asr|tts) phases dns=(-?\d+) tls=(-?\d+) send=(-?\d+) "
    r"ttfb=(-?\d+) body=(-?\d+) http=(-?\d+)")
TRACE_RE = re.compile(r"\] http (\S.*)")
TTS_BLOCKS_RE = re.compile(
    r"\[vgvoice\] tts_blocks n=(\d+) first_line=(\S+) first_pcm=(\S+) "
    r"blocks=([\d,]*) last=(\S+) bytes=(\d+)")
CLIP_PREP_RE = re.compile(r"\[vgvoice\] clip_prep src=(\w+) bytes=(\d+) ms=(\d+)")
TTS_PLAYER_RE = re.compile(
    r"\[vgvoice\] tts_player played=(\d+) pushed=(\d+) underruns=(\d+) "
    r"stall_ms=(\d+) restarts=(\d+) complete=(\d+)"
    r"(?: failed=\d+ err=-?\d+)?(?: leaked=(\d+))? ms=(\d+)")
FIRST_PLAYABLE_RE = re.compile(r"first_playable gen=\d+ elapsed_ms=(\d+)")
SETTLED_RE = re.compile(
    r"round_settled gen=\d+ state=\d+ failed=\d+ elapsed_ms=(\d+)")
# 锚定 CMD 行：NSH 回显里也会出现同样的 CASE 文本，只认测试计划发出的命令。
CASE_RE = re.compile(r"CMD echo CASE_([a-z][a-z0-9_]*)")
TTS_RC_RE = re.compile(r"\[vgvoice\] tts rc=(\w+)")

# -1 是板端 AGENT_T_NA 的打印形式；na 同义（trace 行）。
NA = {None}


def parse_ms(value):
    if value is None or value in ("na", "NA"):
        return None
    number = int(value)
    return None if number < 0 else number


def split_kvs(text):
    out = {}
    for item in text.split():
        key, sep, value = item.partition("=")
        if sep:
            out[key] = value
    return out


def stage_span(stages, begin, end):
    if begin not in stages or end not in stages:
        return None
    span = stages[end] - stages[begin]
    return span if span >= 0 else None


def analyse_block(block):
    """一个 CASE 块 → 一轮的相位记录。"""
    stages = {name: int(value)
              for name, value in STAGE_RE.findall(block)}
    row = {
        "recorder_wait_ms": stage_span(stages, "asr_wait_recorder", "asr_begin"),
        "asr_ms": stage_span(stages, "asr_begin", "asr_end"),
        "agent_ms": stage_span(stages, "agent_begin", "agent_reply"),
        "facts_ms": stage_span(stages, "agent_reply", "facts_end"),
        "tts_ms": stage_span(stages, "tts_begin", "tts_end"),
        # 旧路径：投递到播完。流式轮的 play_posted 在下载开始前记录，
        # 该跨度与下载重叠、不再与旧值同义，仅作参考。
        "play_ms": stage_span(stages, "play_posted", "play_done"),
        # 流式路径：驱动真正开始消费到播完。
        "play_stream_ms": stage_span(stages, "play_start", "play_done"),
        # 流式路径：TTS 开始到真实出声，即流式改动的核心收益指标。
        "first_sound_ms": stage_span(stages, "tts_begin", "play_start"),
        "clip_prep": None,
        "tts_blocks": None,
        "tts_first_pcm": None,
        "tts_last_block": None,
        "tts_block_offsets": None,
        "tts_underruns": None,
        "tts_stall_ms": None,
        "tts_restarts": None,
        "tts_player_complete": None,
        "tts_played_bytes": None,
        "tts_pushed_bytes": None,
        "tts_leaked_buffers": None,
        "llm_iterations": None,
        "llm_send_ms": None,
        "llm_ttfb_ms": None,
        "llm_body_ms": None,
        "llm_http_ms": None,
        "llm_first_data": None,
    }
    for service, dns, tls, send, ttfb, body, http in PHASES_RE.findall(block):
        for field, value in (("dns", dns), ("tls", tls), ("send", send),
                             ("ttfb", ttfb), ("body", body), ("http", http)):
            row[f"{service}_{field}_ms"] = parse_ms(value)

    # LLM 走 trace 行，一个 ReAct 轮次可能有多条；按迭代求和。
    llm = [split_kvs(text) for text in TRACE_RE.findall(block)]
    llm = [item for item in llm
           if "send" in item and int(item.get("attempt", 1)) >= 1]
    if llm:
        row["llm_iterations"] = len(llm)

        def total(field):
            values = [parse_ms(item.get(field)) for item in llm]
            values = [value for value in values if value is not None]
            return sum(values) if values else None

        row["llm_send_ms"] = total("send")
        row["llm_ttfb_ms"] = total("ttfb")
        row["llm_body_ms"] = total("body")
        row["llm_http_ms"] = total("http")
        first_data = [parse_ms(item.get("first_data")) for item in llm]
        first_data = [v for v in first_data if v is not None]
        row["llm_first_data"] = first_data[0] if first_data else None

    blocks = TTS_BLOCKS_RE.search(block)
    if blocks:
        row["tts_blocks"] = int(blocks.group(1))
        row["tts_first_pcm"] = parse_ms(blocks.group(3))
        row["tts_last_block"] = parse_ms(blocks.group(5))
        offsets = [int(x) for x in blocks.group(4).split(",") if x]
        row["tts_block_offsets"] = offsets

    prep = CLIP_PREP_RE.search(block)
    if prep:
        row["clip_prep"] = {"src": prep.group(1),
                            "bytes": int(prep.group(2)),
                            "ms": int(prep.group(3))}

    player = TTS_PLAYER_RE.search(block)
    if player:
        row["tts_underruns"] = int(player.group(3))
        row["tts_stall_ms"] = int(player.group(4))
        row["tts_restarts"] = int(player.group(5))
        row["tts_player_complete"] = int(player.group(6))
        row["tts_played_bytes"] = int(player.group(1))
        row["tts_pushed_bytes"] = int(player.group(2))
        row["tts_leaked_buffers"] = (int(player.group(7))
                                     if player.group(7) is not None else None)

    ready = FIRST_PLAYABLE_RE.search(block)
    fp = int(ready.group(1)) if ready else None
    # 串口截断防御：与已完成阶段矛盾的首播值不能算成功证据。流式轮的
    # first_playable 由播放线程在 play_start 时刻记录，必然早于 tts_end
    # （下载与播放重叠），以 play_start 为界；旧路径仍以 tts_end 为界。
    if fp is not None:
        bound = stages.get("play_start", stages.get("tts_end", 0))
        if fp < bound:
            fp = None
    row["first_playable_ms"] = fp

    settled = SETTLED_RE.search(block)
    sm = int(settled.group(1)) if settled else None
    if sm is not None and sm < max(stages.values(), default=0):
        sm = None
    row["settled_ms"] = sm

    tts_rc = TTS_RC_RE.search(block)
    row["tts_rc"] = tts_rc.group(1) if tts_rc else None
    row["complete"] = bool(fp is not None and row["play_ms"] is not None
                           and row["tts_rc"] == "ok")
    return row


def percentile(values, fraction):
    if not values:
        return None
    ordered = sorted(values)
    index = min(len(ordered) - 1, int(round(fraction * (len(ordered) - 1))))
    return ordered[index]


def summarise(rows):
    """组×阶段的中位数、p90 与失败数。"""
    fields = ["recorder_wait_ms", "asr_ms", "agent_ms", "facts_ms", "tts_ms",
              "play_ms", "first_playable_ms", "settled_ms",
              "asr_send_ms", "asr_ttfb_ms", "asr_body_ms", "asr_http_ms",
              "tts_send_ms", "tts_ttfb_ms", "tts_body_ms", "tts_http_ms",
              "tts_first_pcm", "llm_http_ms", "llm_first_data"]
    summary = {"rounds": len(rows),
               "complete": sum(1 for r in rows if r["complete"]),
               "stages": {}}
    for field in fields:
        values = [r[field] for r in rows if r.get(field) is not None]
        summary["stages"][field] = {
            "n": len(values),
            "median": statistics.median(values) if values else None,
            "p90": percentile(values, 0.9),
            "max": max(values) if values else None,
        }

    # TTS 块节奏：把所有轮的块偏移对齐到首块后取间隔分布。
    gaps = []
    first_pcm = []
    for row in rows:
        offsets = row.get("tts_block_offsets") or []
        if row.get("tts_first_pcm") is not None:
            first_pcm.append(row["tts_first_pcm"])
        gaps += [b - a for a, b in zip(offsets, offsets[1:]) if b >= a]
    summary["tts_blocks"] = {
        "first_pcm": {"n": len(first_pcm),
                      "median": statistics.median(first_pcm) if first_pcm else None,
                      "p90": percentile(first_pcm, 0.9)},
        "inter_block_gap": {"n": len(gaps),
                            "median": statistics.median(gaps) if gaps else None,
                            "p90": percentile(gaps, 0.9)},
    }
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("logs", nargs="+", type=Path, help="会话日志文件")
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--host-probe", type=Path,
                        help="mimo_phase_probe.py 的 JSON，输出板端−主机差值表")
    args = parser.parse_args()

    rows = []
    for log in args.logs:
        match = re.search(r"p0-([a-z]+)-s\d+", log.name)
        group = match.group(1) if match else log.stem
        text = log.read_text(errors="replace")
        text = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", text).replace("nsh> ", "")
        case_index = 0
        for marker in CASE_RE.finditer(text):
            block = text[marker.end():]
            nxt = CASE_RE.search(block)
            if nxt:
                block = block[:nxt.start()]
            row = analyse_block(block)
            row.update(case=marker.group(1), group=group,
                       session=log.stem, question_index=case_index,
                       cold="cold" if case_index == 0 else "hot")
            rows.append(row)
            case_index += 1

    if not rows:
        raise SystemExit("日志里没有 CASE 块")

    args.out_dir.mkdir(parents=True, exist_ok=True)
    fields = sorted({key for row in rows for key in row}
                    - {"clip_prep", "tts_block_offsets"})

    with (args.out_dir / "p0-results.csv").open("w", newline="",
                                                encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            flat = dict(row)
            flat["clip_prep_ms"] = (row["clip_prep"] or {}).get("ms")
            flat["clip_prep_src"] = (row["clip_prep"] or {}).get("src")
            flat["tts_block_offsets"] = ",".join(
                str(x) for x in row["tts_block_offsets"] or [])
            writer.writerow(flat)

    summary = {"sessions": sorted({row["session"] for row in rows}),
               "groups": {}}
    for group in sorted({row["group"] for row in rows}):
        selected = [r for r in rows if r["group"] == group]
        summary["groups"][group] = summarise(selected)
    for temperature in ("cold", "hot"):
        selected = [r for r in rows if r["cold"] == temperature]
        if selected:
            summary[temperature] = summarise(selected)

    if args.host_probe:
        probe = json.loads(args.host_probe.read_text(encoding="utf-8"))
        board_all = summarise(rows)["stages"]
        deltas = {}
        for stage, probe_key in (("asr_send_ms", ("asr", "send_ms")),
                                 ("asr_ttfb_ms", ("asr", "ttfb_ms")),
                                 ("asr_body_ms", ("asr", "body_ms")),
                                 ("tts_ttfb_ms", ("tts", "ttfb_ms")),
                                 ("tts_body_ms", ("tts", "body_ms")),
                                 ("tts_first_pcm", ("tts", "first_pcm_ms"))):
            service, key = probe_key
            host = (probe.get(service) or {}).get(key)
            board = board_all.get(stage, {}).get("median")
            if host is not None and board is not None:
                deltas[stage] = {"board_ms": board, "host_ms": host,
                                 "delta_ms": round(board - host, 1)}
        summary["board_minus_host"] = deltas

    (args.out_dir / "p0-summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(json.dumps({"rounds": len(rows),
                      "complete": sum(1 for r in rows if r["complete"]),
                      "groups": list(summary["groups"])},
                     ensure_ascii=False))


if __name__ == "__main__":
    main()
