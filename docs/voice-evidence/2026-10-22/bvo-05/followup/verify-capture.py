#!/usr/bin/env python3
"""核对原始串口快照的字节/样本守恒；不判别人声或代替听音。"""
import re
from pathlib import Path

root = Path(__file__).resolve().parent
pattern = re.compile(
    r"vgvoice: last result=(\w+) ms=(\d+) samples=(\d+) bytes=(\d+)"
    r".*?vgvoice: capture halves=(\d+) drained=(\d+) overwritten=(\d+)"
    r".*?vgvoice: tail_bytes=(\d+)", re.S
)
count = 0
for name in ["13-fifteen-recovery.log", "14-ten-cancels.log",
             "15-error-interrupt-fast.log", "16-cancels-stable-resources.log",
             "18-final-1-5-15.log"]:
    text = (root / name).read_text()
    for result, ms, samples, size, halves, drained, overwritten, tail in pattern.findall(text):
        ms, samples, size, halves, drained, overwritten, tail = map(
            int, [ms, samples, size, halves, drained, overwritten, tail])
        # 上电的空快照没有录音，不能当成有效样本。
        if samples == 0:
            continue
        expected = (halves * 8192 + tail) // 8
        assert samples == expected, (name, samples, expected)
        assert size == samples * 2, (name, size, samples)
        assert drained == halves + bool(tail), (name, drained, halves, tail)
        assert overwritten == 0, (name, overwritten)
        # 系统 tick 为 10 ms。取消可以在同一 tick 内完成；允许两 tick 的
        # 时间量化误差，不把此核对当作物理麦克风时钟测量。
        assert abs(samples / 16 - ms) <= 20, (name, ms, samples)
        count += 1
print(f"PASS: {count} capture snapshots conserve bytes and samples; no speech verdict")
