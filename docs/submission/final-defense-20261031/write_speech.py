#!/usr/bin/env python3
"""从已确认逐页内容生成带时间分配的独立口播稿。"""

import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
data = json.loads((HERE / "content.json").read_text(encoding="utf-8"))
main_pages = data["pages"][:12]
total = sum(page["seconds"] for page in main_pages)
assert total == 440


def mmss(value):
    return f"{value // 60:02}:{value % 60:02}"


lines = [
    "# VelaGuard 决赛答辩逐页口播稿", "",
    "日期：2026-10-31。Team Falcons，叶培林。对应 12 页主讲与 4 页问答附录。", "",
    "净内容 7 分 20 秒，含第 10 页 80 秒视频。另留 40 秒切页、停顿和播放切换，8 分钟内结束。", "",
    "第 10 页先放原型实拍，录制完成后替换为视频。备注保存资料来源和测试条件，主讲使用下文正文。", "",
    "## 时间分配", "",
    "| 页码 | 标题 | 净时长 | 累计结束 |", "|---|---|---:|---:|",
]
elapsed = 0
for i, page in enumerate(main_pages, 1):
    elapsed += page["seconds"]
    lines.append(f"| {i} | {page['title']} | {page['seconds']} 秒 | {mmss(elapsed)} |")

lines.extend(["", "## 主讲正文", ""])
for i, page in enumerate(main_pages, 1):
    lines.extend([f"### 第 {i} 页　{page['title']}（{page['seconds']} 秒）", ""])
    if i == 10:
        lines.extend([
            "播放 80 秒视频。视频已有旁白时直接播放；无片头旁白时使用以下介绍语，计入这 80 秒。", "",
        ])
    lines.extend([page["speech"], ""])

lines.extend([
    "## 静帧讲解备用稿", "",
    "这是 VelaGuard 原型实拍。屏幕集中显示点位读数、在线状态和告警。采集、主动解释和语音查询的板测结果分别见前面几页。", "",
    "## 问答附录", "",
    "按评委提问打开对应附录。回答先给结论，再补一项证据，约 20–30 秒。", "",
])
for i, page in enumerate(data["pages"][12:], 1):
    lines.extend([f"### A{i}　{page['title'].replace('问答附录：', '')}", "", page["speech"], ""])

lines.extend([
    "## 彩排与表达", "",
    "第 6 页讲清主动触发、数据查询和结果展示，第 9 页讲清问题与优化结果。代码文件名和完整来源留在备注。", "",
    "语音和工程两页可以使用预留时间放慢。彩排超时时，先删重复解释。", "",
])
(HERE / "逐页口播稿.md").write_text("\n".join(lines), encoding="utf-8", newline="\n")
print(f"口播稿已生成：{len(main_pages)} 页，净内容 {total} 秒，预留 40 秒。")
