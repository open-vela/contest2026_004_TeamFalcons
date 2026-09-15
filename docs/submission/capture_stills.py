#!/usr/bin/env python3
"""把界面渲染帧转成报告插图，并打印溯源哈希。

源帧由无头界面用例产出（`ctest` 的 hmi-headless 套件），落在 `.debug/` 下，
而 `.debug/` 不随仓库提交。本脚本把选定的帧逐字转存到 `stills/`，之后
`render_figures.py` 只读 `stills/`，构建不再依赖 `.debug/` 的存在。

用法：
    python3 docs/submission/capture_stills.py
"""

from __future__ import annotations

import hashlib
import sys
from pathlib import Path

from PIL import Image

SUB = Path(__file__).resolve().parent
ROOT = SUB.parents[1]
STILLS = SUB / "stills"

# 输出名 -> (源帧, 放大倍数, 用途)
SOURCES = {
    "home-32pt": ("hmi-headless/frames/fixture_check/02_fixture_home_32pt.ppm", 2,
                  "首页：32 个点位的实时快照与状态栏"),
    "alarm-warn3": ("hmi-headless/frames/fixture_check/05_fixture_alarm_warn3.ppm", 2,
                    "告警页：三行活动告警与逐点静音、标记处理"),
    "trend-threshold": ("hmi-headless/frames/fixture_check/04_fixture_trend_full_history.ppm", 2,
                        "趋势页：阈值线与历史窗口"),
}


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main() -> int:
    debug = ROOT / ".debug"
    if not debug.is_dir():
        print(f"缺少源目录：{debug}", file=sys.stderr)
        return 1
    STILLS.mkdir(parents=True, exist_ok=True)
    for name, (relative, scale, purpose) in SOURCES.items():
        source = debug / relative
        if not source.is_file():
            print(f"缺少源帧：{source}", file=sys.stderr)
            return 1
        image = Image.open(source).convert("RGB")
        if scale != 1:
            image = image.resize((image.width * scale, image.height * scale), Image.LANCZOS)
        target = STILLS / f"{name}.png"
        image.save(target, optimize=True)
        print(f"{name}\t{image.width}x{image.height}\t{purpose}")
        print(f"  源 {relative}  sha256 {digest(source)}")
        print(f"  输出 stills/{name}.png  sha256 {digest(target)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
