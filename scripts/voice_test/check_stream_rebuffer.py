#!/usr/bin/env python3
"""核对流式播报板测原始日志；字段损坏或缺少场景时失败，不推测补齐。

python3 scripts/voice_test/check_stream_rebuffer.py accept.log final-accept.log
"""
import argparse
import json
import re
from pathlib import Path


def fields(line):
    return {k: int(v) for k, v in re.findall(r"(\w+)=(-?\d+)(?=\s|$)", line)}


def check(text):
    parts = re.split(r"CMD echo CASE_(\w+)[^\n]*\n", text)
    cases = dict(zip(parts[1::2], parts[2::2]))
    result = []
    errors = []
    if len(set(parts[1::2])) != len(parts[1::2]):
        errors.append("场景名称重复，不能用后一次记录覆盖前一次结果")
    for name in ([f"stream_short_{i}" for i in range(1, 4)] +
                 [f"final_long_{i}" for i in range(1, 4)] +
                 ["slow_complete", "cancel_before", "cancel_playing", "cancel_rebuffer"]):
        block = cases.get(name, "")
        row = {"case": name}

        def require(condition, why):
            if not condition:
                errors.append(f"{name}: {why}")

        def record(marker, suffix=""):
            lines = [line for line in block.splitlines() if marker in line and suffix in line]
            require(len(lines) == 1, f"应有一条完整的 {marker} {suffix}")
            return fields(lines[0]) if len(lines) == 1 else {}

        require(bool(block), "缺少场景")
        require("NSH prompt timeout" not in block, "串口命令未完成")
        done = record("tts_play event=done")
        require(done.get("leaked") == 0, "驱动缓冲未全部归还或缺少统计")
        if name.startswith(("stream_short_", "final_long_")):
            ready = record("tts_play event=buffer_ready", "initial=1")
            start = record("tts_play event=started")
            require(done.get("complete") == 1 and done.get("failed") == 0 and
                    done.get("err") == 0, "播放未成功完成")
            require(done.get("enqueued", 0) > 0 and
                    done.get("enqueued") == done.get("returned"), "入队与归还字节不一致")
            require(done.get("underruns") == 0 and done.get("stall_ms") == 0,
                    "成功样本发生欠载停顿")
            require(0 <= start.get("ready_delay_ms", 99999) <= 200, "缓冲达标后未在 200 ms 内启动")
            if name.startswith("stream_short_"):
                response = record("tts rc=ok status=200")
                require(response.get("stream") == 1 and 0 < response.get("pcm", 0) < 120000,
                        "短回答不是流式路径或音频超过 2.5 秒")
                require(ready.get("eof") == 1, "短回答没有在完整接收后启动")
            else:
                response = record("vgvoice: ttss rc=ok status=200")
                require(response.get("pcm", 0) > 120000, "未覆盖长回答")
                require(ready.get("eof") == 0, "长回答未提前起播")
                end = response.get("mono_ms", 0)
                lead = end - start.get("mono_ms", end)
                require(end - ready.get("mono_ms", end) >= 1000 and lead > 0,
                        "未证明缓冲达标早于接收结束至少一秒")
                row["start_before_tts_end_ms"] = lead
            expected = response.get("pcm", 0) // 6 * 8
            require(done.get("enqueued") == expected, "重采样后的字节数不完整")
            row.update(audio_ms=response.get("pcm", 0) // 48,
                       ready_delay_ms=start.get("ready_delay_ms"), underruns=done.get("underruns"))
        elif name == "slow_complete":
            stats = record("vgvoice: streamtest pushed=")
            require(stats.get("pushed") == 128000 and stats.get("played") == 256000 and
                    stats.get("returned") == 256000, "慢速供给音频不完整")
            require(stats.get("complete") == 1 and stats.get("failed") == 0 and
                    stats.get("err") == 0, "慢速供给未完成")
            require(stats.get("underruns", 0) >= 1 and stats.get("restarts", 0) >= 1,
                    "未覆盖欠载后的恢复")
            row.update(underruns=stats.get("underruns"), stall_ms=stats.get("stall_ms"))
        else:
            require(done.get("complete") == 0, "取消轮被标记为完整成功")
            if name == "cancel_rebuffer":
                require("tts_play event=underrun" in block, "未在重新缓冲时取消")
                stats = record("vgvoice: streamtest pushed=")
                require(stats.get("complete") == 0 and stats.get("leaked") == 0,
                        "诊断取消未清理")
            else:
                failure = record("tts_playback_failure")
                started = 0 if name == "cancel_before" else 1
                require(failure.get("started") == started and failure.get("partial") == started,
                        "取消阶段或部分播放标记不符")
                settled = record("cancel_settled")
                require(0 <= settled.get("elapsed_ms", 99999) <= 2000 and
                        settled.get("controlled_active") == 0, "取消回收未在两秒内完成")
                row["cancel_ms"] = settled.get("elapsed_ms")
            require("state=idle retained_pcm=0" in block, "取消后音频未空闲或录音残留")
        result.append(row)
    return result, errors


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+")
    parser.add_argument("--output")
    args = parser.parse_args()
    rows, failures = check("\n".join(Path(p).read_text(encoding="utf-8-sig") for p in args.logs))
    report = {"passed": not failures, "cases": rows, "errors": failures}
    if args.output:
        Path(args.output).write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    raise SystemExit(1 if failures else 0)
