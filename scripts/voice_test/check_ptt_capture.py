#!/usr/bin/env python3
"""检查合成按键的 RAM 录音交接与清理，不判定语音识别内容是否正确。

用法：python3 scripts/voice_test/check_ptt_capture.py regression.log short.log
测试计划见 docs/voice-evidence/2026-10-25/ptt-ram-fix/regression.json。
"""

import re
import sys
from pathlib import Path


def check(text):
    parts = re.split(r"CMD echo CASE_ptt_(\w+)[^\n]*\n", text)
    blocks = dict(zip(parts[1::2], parts[2::2]))
    errors = []
    for name in ("normal", "repeat", "cap", "cancel", "short"):
        block = blocks.get(name, "")

        def require(ok, reason):
            if not ok:
                errors.append(f"{name}: {reason}")

        require(bool(block), "缺少测试记录")
        require("NSH prompt timeout" not in block and
                "EXPECTED_MARKER_MISSING" not in block, "串口步骤未完成")
        require("vgvoice: state=idle retained_pcm=0" in block,
                "结束后必须空闲且无录音残留")
        require("dma dropped=0 overruns=0 clockerrors=0 errors=0" in block,
                "录音 DMA 出错或缺少统计")
        require(re.search(r"failures=0 timeouts=0", block),
                "录音失败或缺少统计")
        counts = [int(n) for n in re.findall(r"requests asr=(\d+)", block)]
        require(len(counts) == 2, "缺少前后识别请求计数")
        clips = re.findall(r"clip_prep src=(\w+) bytes=(\d+)", block)
        responses = re.findall(r"asr rc=(\w+) status=(\d+) req=(\d+)", block)
        if name in ("cancel", "short"):
            require(not clips and not responses and "stage=asr_begin" not in block,
                    "取消或过短录音不得进入识别")
            require(len(counts) == 2 and counts[0] == counts[1],
                    "取消或过短录音新增了识别请求")
            if name == "short":
                require("last result=too_short" in block, "未覆盖过短录音")
        else:
            require(len(counts) == 2 and counts[1] == counts[0] + 1,
                    "每轮应且只应提交一次识别")
            minimum = 440000 if name == "cap" else 64000
            require(len(clips) == 1 and clips[0][0] == "ram" and
                    int(clips[0][1]) >= minimum,
                    "未取到预期长度的 RAM 录音")
            require(len(responses) == 1 and responses[0][0] in ("ok", "empty")
                    and responses[0][1] == "200" and int(responses[0][2]) > 0,
                    "录音未完成识别请求并收到 HTTP 200")
            require("round_settled" in block, "识别未结束")
    return errors


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    failures = check("\n".join(Path(p).read_text(encoding="utf-8-sig")
                               for p in sys.argv[1:]))
    if failures:
        sys.exit("\n".join("FAIL: " + failure for failure in failures))
    print("PASS: 松手、连续录音、15 秒上限均提交 RAM 音频；取消和过短录音无请求、无残留")
