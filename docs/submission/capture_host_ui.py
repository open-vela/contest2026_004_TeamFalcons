#!/usr/bin/env python3
"""渲染点表上位机界面并存图，供报告插图使用。

上位机是独立仓库 `velaguard_host`（本地 checkout `F:\\Project\\uppercomputer`），
依赖 PyQt5 与 pyserial，本目录的构建解释器没有这两个包，因此单独用上位机自己的
虚拟环境运行：

    F:/Project/uppercomputer/.venv/Scripts/python.exe docs/submission/capture_host_ui.py

界面在未连接板端时截图，顶栏显示 NSH 未连接；点表卡片来自仓内示例
`examples/vgpoint_demo_points.json`。输出写入 `stills/host-ui.png`。
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

DEFAULT_REPO = Path("F:/Project/uppercomputer")
SUB = Path(__file__).resolve().parent


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=DEFAULT_REPO)
    parser.add_argument("--output", type=Path, default=SUB / "stills/host-ui.png")
    args = parser.parse_args()

    repo, output = args.repo.resolve(), args.output.resolve()
    if not (repo / "main.py").is_file():
        print(f"缺少上位机工程：{repo}", file=sys.stderr)
        return 1
    sys.path.insert(0, str(repo))
    os.chdir(repo)

    from PyQt5.QtWidgets import QApplication

    from theme import apply_theme
    import main as app_main

    app = QApplication([])
    apply_theme(app)
    window = app_main.mainUI()

    sample = repo / "examples" / "vgpoint_demo_points.json"
    for point in json.loads(sample.read_text(encoding="utf-8"))["points"]:
        window.page_points.upsert(
            point["id"],
            unit=point.get("unit", ""),
            candidate=False,
            name=point.get("name", point["id"]),
        )

    window.resize(1280, 800)
    window.move(-4000, -4000)  # 移到屏幕外，不打断桌面
    window.show()
    for _ in range(12):
        app.processEvents()
    output.parent.mkdir(parents=True, exist_ok=True)
    window.grab().save(str(output))
    print(f"{output}  {output.stat().st_size} bytes  示例点表 {sample.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
