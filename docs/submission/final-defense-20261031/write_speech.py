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
    "净内容 7 分 20 秒，其中第 10 页视频 80 秒。另留 40 秒给切页、停顿与播放切换，8 分钟内结束。此为编排目标，尚未通过完整彩排。", "",
    "第 10 页当前是原型实拍静帧，尚未包含新视频。录制完成后替换再使用视频旁白。PPT 备注里的资料来源不朗读。", "",
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
            "录制完成后播放视频，现场不与视频旁白重复讲解。以下介绍语仅在视频无片头旁白时使用，并计入 80 秒。", "",
        ])
    lines.extend([page["speech"], ""])

lines.extend([
    "## 还未录制视频时的第 10 页备用讲法", "",
    "这里先展示原型实拍静帧。前面介绍的采集、告警、主动解释和语音都有对应历史板测记录。今天这份材料尚未内嵌新的完整演示视频，不能把这张静帧当成现场实时运行。", "",
    "这段备用讲法用于制作期间审阅。正式答辩优先按视频脚本补齐实录，不用备用说明替代应有演示。", "",
    "## 问答附录", "",
    "评委问到对应问题时再打开附录，不占前 8 分钟。单次回答先给结论，再用一项证据说明，约 20–30 秒。", "",
])
for i, page in enumerate(data["pages"][12:], 1):
    lines.extend([f"### A{i}　{page['title'].replace('问答附录：', '')}", "", page["speech"], ""])

lines.extend([
    "## 彩排与表达", "",
    "代码文件名、工具名和时间样本不逐字重复念两遍。第 6 页重点讲触发、取证、产物和校验，第 9 页重点讲问题及对应结果。", "",
    "语音与工程证据两页信息较多，可使用预留的 40 秒适当放慢。若超时，优先删解释性重复句，不加快到听不清。", "",
    "未经用户研究，不补充节省工时、市场规模、成本收益或识别准确率。主机测试、历史板测、当前模拟界面和待实现目标保持分别说明。", "",
])
(HERE / "逐页口播稿.md").write_text("\n".join(lines), encoding="utf-8")
print(f"口播稿已生成：{len(main_pages)} 页，净内容 {total} 秒，预留 40 秒。")
