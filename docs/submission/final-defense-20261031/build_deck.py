#!/usr/bin/env python3
"""填充官方答辩模板的 round-trip 工作区，不修改官方原件。

先用 ppt-master 的 pptx_to_svg.py --inheritance-mode both --roundtrip 导入
官方模板，再运行本脚本。完整重建步骤见同目录 README.md。
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import xml.etree.ElementTree as ET
from pathlib import Path

NS = "http://www.w3.org/2000/svg"
ET.register_namespace("", NS)
TAG = lambda name: f"{{{NS}}}{name}"
HERE = Path(__file__).resolve().parent


def group(root, ident):
    return next(g for g in root.iter(TAG("g")) if g.get("id") == ident)


def replace_text(root, ident, lines, size=None, color=None, y=None, step=None):
    """保留导入对象的标识，在原有文本框内改字和显式换行。"""
    g = group(root, ident)
    old = next(g.iter(TAG("text")))
    attrs = dict(old.attrib)
    for k in ("data-pptx-text-model", "data-paragraph-line-height"):
        attrs.pop(k, None)
    if size:
        attrs["font-size"] = str(size)
    if color:
        attrs["fill"] = color
    if y is not None:
        attrs["y"] = str(y)
    text = ET.Element(TAG("text"), attrs)
    lines = [lines] if isinstance(lines, str) else lines
    text.text = lines[0]
    leading = step or float(attrs.get("font-size", "36")) * 1.5
    for line in lines[1:]:
        span = ET.SubElement(text, TAG("tspan"), x=attrs["x"], dy=str(leading))
        span.text = line
    parent = next(p for p in g.iter() if old in list(p))
    idx = list(parent).index(old)
    parent.remove(old)
    parent.insert(idx, text)


def add_text(root, ident, x, y, lines, size=36, color="#334155", bold=False, step=None):
    lines = [lines] if isinstance(lines, str) else lines
    height = len(lines) * (step or size * 1.5)
    g = ET.SubElement(root, TAG("g"), id=ident, **{
        "data-pptx-bounds": f"{x} {y-size} {2380-x} {height}",
    })
    text = ET.SubElement(g, TAG("text"), x=str(x), y=str(y),
                         **{"font-size": str(size), "fill": color,
                            "font-weight": "bold" if bold else "normal"})
    text.text = lines[0]
    for line in lines[1:]:
        ET.SubElement(text, TAG("tspan"), x=str(x), dy=str(step or size * 1.5)).text = line
    return g


def remove(root, ident):
    g = group(root, ident)
    parent = next(p for p in root.iter() if g in list(p))
    parent.remove(g)


def header(root, title, kicker="PROJECT DEFENSE · 决赛答辩"):
    replace_text(root, "shape-2", kicker)
    replace_text(root, "shape-3", title)


def picture(root, ident, filename, frame):
    """在官方视频/图片区域内放素材，保留区域和源页标识。"""
    g = group(root, ident)
    for item in list(g):
        g.remove(item)
    for attr in list(g.attrib):
        if attr == "data-pptx-semantic-object" or attr == "data-pptx-prst" or attr.startswith("data-pptx-av-"):
            g.attrib.pop(attr)
    g.set("data-pptx-object", "picture")
    x, y, w, h = frame
    ET.SubElement(g, TAG("image"), href=f"../images/{filename}",
                  x=str(x), y=str(y), width=str(w), height=str(h),
                  preserveAspectRatio="xMidYMid meet")


def process(root, titles, captions, foot):
    for ident, title in zip(("shape-8", "shape-13", "shape-18", "shape-23"), titles):
        replace_text(root, ident, title, size=38)
    for ident, text in zip(("shape-9", "shape-14", "shape-19", "shape-24"), captions):
        replace_text(root, ident, text, size=30)
    replace_text(root, "shape-28", foot, size=32)


def native_table(root, ident, columns, rows, font_size=34):
    """原表格因直接格式不能自动回写，按原框重建成可编辑 PowerPoint 表格。"""
    g = group(root, ident)
    x, y, w, h = map(float, g.get("data-pptx-frame").split())
    values = columns + [cell for row in rows for cell in row]
    texts = list(g.iter(TAG("text")))
    assert len(texts) == len(values)
    for i, (text, value) in enumerate(zip(texts, values)):
        for child in list(text):
            text.remove(child)
        text.text = value
        text.set("font-size", str(font_size))
    widths = [0.20, 0.40, 0.40] if w > 2000 else [1, 1, 1]
    header_colors = ["#111827", "#0891B2", "#9333EA"]
    styled_columns = [{"text": value, "fill": header_colors[i], "color": "#FFFFFF",
                       "bold": True, "align": "ctr"} for i, value in enumerate(columns)]
    styled_rows = []
    for n, row in enumerate(rows):
        styled_rows.append([{"text": text, "fill": "#F1F5F9" if n % 2 == 0 else "#FFFFFF",
                             "bold": i == 0, "color": "#111827" if i == 0 else "#334155",
                             "align": "ctr"} for i, text in enumerate(row)])
    payload = {"schema": "ppt-master.semantic-table.v2", "name": ident,
               "x": x, "y": y, "width": w, "height": h,
               "columns": styled_columns, "rows": styled_rows,
               "column_widths": widths,
               "row_heights": [0.85] + [1.0] * len(rows),
               "style": {"font_family": "MiSans", "font_size": font_size,
                         "border_color": "#CBD5E1", "border_width": 1,
                         "band_row": False},
               "defaults": {"cell": {"font_size": font_size, "valign": "middle",
                                      "padding": 14}}}
    g.set("data-pptx-replace-with", "table")
    g.set("data-pptx-native-authority", "json")
    g.attrib.pop("data-pptx-replacement-status", None)
    metadata = ET.Element(TAG("metadata"), type="application/json")
    metadata.text = json.dumps(payload, ensure_ascii=False)
    g.insert(0, metadata)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("workspace", type=Path)
    parser.add_argument("--materials", type=Path, required=True)
    args = parser.parse_args()
    workspace = args.workspace.resolve()
    folder = workspace / "authoring-svg-flat"
    image_dir = workspace / "images"
    note_dir = workspace / "notes"
    note_dir.mkdir(exist_ok=True)
    materials = args.materials
    content = json.loads((HERE / "content.json").read_text(encoding="utf-8"))

    assets = {
        "defense-rig.jpeg": materials / "图片内容" / "实物链接拓扑图.jpg",
        "defense-board.jpeg": materials / "图片内容" / "OPENVELA官方邮寄的开发板.jpg",
        "defense-stack.jpeg": materials / "图片内容" / "扩展板与开发板链接方式.jpg",
        "defense-alarm.png": HERE / "assets" / "alarm-current.png",
        "defense-trend.png": HERE / "assets" / "trend-current.png",
    }
    asset_manifest = []
    for target, source in assets.items():
        shutil.copyfile(source, image_dir / target)
        asset_manifest.append({"file": target, "source": str(source),
                               "sha256": hashlib.sha256(source.read_bytes()).hexdigest()})
    (workspace / "asset_manifest.json").write_text(json.dumps(asset_manifest, ensure_ascii=False, indent=2), encoding="utf-8", newline="\n")

    pages = []
    for n, item in enumerate(content["pages"], 1):
        source = item["source_slide"]
        tree = ET.parse(folder / f"slide_{source:02}.svg")
        root = tree.getroot()
        stem = f"p{n:02}_{item['key']}"
        pages.append({"source_slide": source, "svg": f"{stem}.svg"})

        if n == 1:
            replace_text(root, "shape-10", "VelaGuard", size=176)
            replace_text(root, "shape-7", ["基于 openvela 的 AI 辅助监测网关", "Team Falcons   叶培林   AI 硬件产品创新"],
                         size=44, color="#D9E5F0", y=814, step=78)
        elif n == 2:
            header(root, "小型设备现场，需要持续值守")
            picture(root, "shape-21", "defense-rig.jpeg", (186.67, 400, 1280, 670))
            replace_text(root, "shape-8", "原型台架实拍，含真实从站与测试从站", size=26, y=1110)
            replace_text(root, "shape-10", "面向现场运维人员", size=40)
            replace_text(root, "shape-12", ["多台设备的读数", "需要集中查看"], size=38, y=615, step=55)
            replace_text(root, "shape-14", ["告警出现后", "需要核对触发依据"], size=38, y=762, step=55)
            replace_text(root, "shape-16", ["交接与复盘", "需要可追溯的运行记录"], size=38, y=909, step=55)
            replace_text(root, "shape-17", "产品目标：配置完成后，独立承担监测与规则告警", size=34)
        elif n == 3:
            header(root, "一次巡检，覆盖状态查看到运行回顾")
            process(root, ["看实时状态", "核对规则告警", "查看 AI 解释", "回顾运行报告"],
                    ["数值、在线情况", "阈值、离线依据", "只读取证，标注推测", "来源、日期、统计"],
                    "同一个温湿度监测场景贯穿全程，运行配置由人工确认")
            add_text(root, "scenario-lead", 280, 365, "日常查看   /   异常处理   /   事后复盘", size=38, color="#0891B2")
        elif n == 4:
            header(root, "本地持续监测，板载 Agent 调度工具")
            # 导入器展开组坐标后，直接恢复组内原生形状会丢失父级坐标。
            # 将官方 10% 浅色底转为等效不透明底色，使这些对象按展开后的坐标重建。
            for ident, color in [("shape-7", "#EBF1FE"), ("shape-10", "#EBF1FE"),
                                 ("shape-13", "#E6F4F7"), ("shape-16", "#F4EBFD"),
                                 ("shape-19", "#E6F4F7"), ("shape-22", "#F4EBFD")]:
                geometry = next(group(root, ident).iter(TAG("path")))
                geometry.set("fill", color)
                geometry.attrib.pop("fill-opacity", None)
            replace_text(root, "shape-6", ["openvela", "网关", "STM32H750"], size=36, y=690, step=66)
            replace_text(root, "shape-8", "本地确定性业务", size=40)
            replace_text(root, "shape-9", ["Modbus 采集、帧统计、规则告警", "LVGL 显示与 eMMC 记录"], size=34, y=610, step=58)
            replace_text(root, "shape-11", "板载 ai_agent", size=40)
            replace_text(root, "shape-12", ["ReAct 调度、自定义 Skill、只读工具", "产物校验后展示解释与日报"], size=34, y=610, step=58)
            for ident, title in zip(("shape-14", "shape-17", "shape-20", "shape-23"),
                                    ("RS485 从站", "MiMo", "MQTT 留档", "eMMC")):
                replace_text(root, ident, title, size=34)
            for ident, lines in zip(("shape-15", "shape-18", "shape-21", "shape-24"),
                                    (["现场读数", "板端主站轮询"], ["HTTPS 推理", "联网服务"],
                                     ["状态与遥测", "告警与已确认表"], ["配置与事件", "解释与报告"])):
                replace_text(root, ident, lines, size=30, y=1000, step=48)
            add_text(root, "network-boundary", 200, 1232, "模型推理在云端，调度与工具执行在板端；网络不可用时本地监测继续", size=31)
        elif n == 5:
            header(root, "阈值与离线判定在本地完成")
            replace_text(root, "shape-6", "规则先判定", size=44)
            replace_text(root, "shape-10", "界面呈现", size=44)
            replace_text(root, "shape-8", ["采集只使用已确认点表", "阈值与离线由 C 代码判定", "候选编辑不改变运行配置", "联网失败保留本地依据"],
                         size=42, y=680, step=104)
            remove(root, "shape-12")
            g = ET.SubElement(root, TAG("g"), id="current-alarm-screen", **{"data-pptx-bounds": "1415 640 900 450"})
            ET.SubElement(g, TAG("image"), href="../images/defense-alarm.png", x="1415", y="640", width="900", height="450", preserveAspectRatio="xMidYMid meet")
            add_text(root, "simulation-disclosure", 1438, 1150, "当前代码渲染，模拟数据界面", size=26, color="#64748B")
            add_text(root, "offline-value-note", 270, 1255, "离线时明确显示不可用，最后一次成功值不冒充当前读数", size=31)
        elif n == 6:
            header(root, "Agent 主动取证，产出可校验的解释与日报")
            process(root, ["事件或时钟触发", "读取自定义 Skill", "调用只读工具", "校验产物并展示"],
                    ["告警变化 / 当日缺日报", "定义任务顺序与范围", "运行统计、时间、点位", "解释标注 AI 推测"],
                    "实测样本：主动解释 8 条告警，41.83 秒；自主日报 194 秒")
            add_text(root, "skills-label", 280, 370,
                     "alarm_interpretation   /   operations_report   /   modbus_query", size=33, color="#0891B2")
            add_text(root, "agent-artifacts", 280, 1095,
                     ["告警解释产物：alarm_advice.txt    日报产物：daily-日期.md", "以上为历史真机单样本，分别来自 2026-10-25、2026-09-16"], size=30, step=62)
        elif n == 7:
            header(root, "AI 只读查询，配置生效由人确认")
            native_table(root, "shape-5", ["对象", "允许的行为", "边界落实位置"], [
                ["实时数据", "读取运行快照", "C 工具参数与数量上限"],
                ["解释和日报", "写入受限产物", "产物格式与来源校验"],
                ["设备处置", "不向 Agent 授权", "无寄存器写入、清警权限"],
                ["配置变更", "人工单独确认", "候选与已确认表分离"],
                ["审计", "记录工具调用", "限流与会话门控"],
            ], font_size=36)
            replace_text(root, "shape-6", "NSH 现行流程：编辑候选、试读并展示、等待人工确认、单独 apply", size=32)
        elif n == 8:
            header(root, "按键提问，板端只读查询并播报")
            picture(root, "shape-21", "defense-stack.jpeg", (186.67, 400, 1280, 670))
            replace_text(root, "shape-8", "硬件原型实拍；语音链路以 10 月板测记录为依据", size=25, y=1110)
            replace_text(root, "shape-10", "已有板端语音成果", size=40)
            replace_text(root, "shape-12", ["按键录音与云侧 ASR", "原始问题交给板载 Agent"], size=35, y=615, step=54)
            replace_text(root, "shape-14", ["vg_point_read 取真实状态", "屏幕全文、TTS 简明播报"], size=35, y=762, step=54)
            replace_text(root, "shape-16", ["离线时明确没有当前读数", "不改配置、不代替确认"], size=35, y=909, step=54)
            replace_text(root, "shape-17", "10-25 实体按键与人声链路有完整播放记录，属于原型分项验证", size=31)
        elif n == 9:
            header(root, "工程难点，有对应工况与修复证据")
            replace_text(root, "shape-7", "RS485 快速回复兼容", size=42)
            replace_text(root, "shape-11", "图形与语音并发", size=42)
            replace_text(root, "shape-8", ["0 ms 回复工况", "修复方向释放时序后", "649 / 649 次采集成功", "另测 5 / 20 / 150 ms 回复", "四档启动错误计数均为 0"],
                         size=40, y=635, step=92)
            replace_text(root, "shape-12", ["减少语音浮层重复重绘", "两组各 60 秒板测", "显示提交 438 / 454 次", "降为 24 / 46 次", "FIFO 欠载各 1 次降为 0"],
                         size=40, y=635, step=92)
            replace_text(root, "shape-13", "短测支持对应修复有效，长期稳定性与并发存储仍需继续验证", size=32)
        elif n == 10:
            # 官方全屏视频页改为真实静帧。用户录制后可在 PowerPoint 中替换。
            picture(root, "shape-21", "defense-board.jpeg", (0, 0, 2560, 1440))
            remove(root, "shape-22")
            # 静帧底部使用简单底色保证字幕对比，属于文字背景。
            caption = group(root, "shape-20")
            caption.set("data-pptx-frame", "0 1265 2560 175")
            geometry = next(caption.iter(TAG("path")))
            geometry.set("d", "M 0 1265 L 2560 1265 L 2560 1440 L 0 1440 Z")
            geometry.set("fill", "#0F172A")
            replace_text(root, "shape-20", "真机演示：采集、告警、取证解释、断网监测", size=42, color="#FFFFFF", y=1335)
            add_text(root, "demo-still-disclosure", 850, 1400, "原型实拍静帧，配套视频按脚本录制", size=27, color="#CBD5E1")
            # 原型实拍在画面中心，标题移至底部官方文本区。
        elif n == 11:
            header(root, "初赛后更新与当前进度")
            for ident, title in zip(("shape-7", "shape-11", "shape-15", "shape-19"),
                                    ("本地业务与显示", "板端只读语音", "OTA 分项进展", "后续产品目标")):
                replace_text(root, ident, title, size=38)
            for ident, lines in zip(("shape-8", "shape-12", "shape-16", "shape-20"),
                                    (["快速从站兼容、减少重复重绘", "当前界面无头检查 6 / 6 通过"],
                                     ["按键录音、只读问答和播报", "已留真机记录，继续做稳定性验证"],
                                     ["测试信任下下载与续传到 READY", "安装与回滚另有分项，完整链路待验"],
                                     ["手机 PDF 接入、手机语音", "仍需完成完整链路实现和验收"])):
                replace_text(root, ident, lines, size=34, step=63)
            replace_text(root, "shape-21", "以原型和可复核证据交付，下一步补齐稳定性、接入体验与完整升级链路", size=32)
        elif n == 12:
            replace_text(root, "shape-4", "THANKS")
            replace_text(root, "shape-6", "本地持续监测，AI 依据真实数据辅助巡检", size=44)
            replace_text(root, "shape-7", "VelaGuard   Team Falcons   叶培林", size=34)
            replace_text(root, "shape-8", "独立监测   /   告警有依据   /   运行可回顾", size=33)
        elif n == 13:
            header(root, "问答附录：证据、条件与限制", "APPENDIX · 问答附录")
            native_table(root, "shape-5", ["验证对象", "证据范围", "不能外推的结论"], [
                ["采集兼容", "10-18 四档回复板测", "不代表所有从站均兼容"],
                ["主动解释", "10-25 单次 8 条告警", "不代表平均响应时间"],
                ["语音", "真人按键单样本与回喂样本", "不代表真人识别准确率"],
                ["界面", "10-31 当前代码无头 6 / 6", "不代替面板触摸与长测"],
                ["稳定性", "已知并发问题继续排查", "未宣称整机 24 小时通过"],
            ], font_size=34)
            replace_text(root, "shape-6", "仍需验证：并发存储超时、诊断与语音并发、完整生产 OTA 和长期运行", size=31)
        elif n == 14:
            header(root, "问答附录：两种配置入口的确认边界", "APPENDIX · 问答附录")
            replace_text(root, "shape-6", "现行 NSH / 上位机", size=42)
            replace_text(root, "shape-10", "手机接入目标", size=42)
            replace_text(root, "shape-8", ["编辑候选点表", "试读并展示结果", "停下来等待人工确认", "单独 apply --confirm", "已确认表才进入周期采集"],
                         size=40, y=660, step=94)
            replace_text(root, "shape-12", ["PDF 与从机地址同次上传", "云侧只生成有依据的候选", "板端确定性校验，不试读", "本机人工二次确认后添加", "完整链路仍待实现与验收"],
                         size=40, y=660, step=94)
            add_text(root, "confirm-boundary", 270, 1250, "Agent、手机上传和云侧解析均不能替代人工确认", size=32)
        elif n == 15:
            header(root, "问答附录：OTA 的分项证据与待验项", "APPENDIX · 问答附录")
            process(root, ["MQTTS 通知", "HTTPS 下载", "本机逐版本确认", "启动阶段安装"],
                    ["测试凭据下已板验", "摘要、签名与证书校验", "网络链路尚未串联验收", "安装 / 回滚有独立分项"],
                    "RJ45 出口，现有 eMMC FAT 暂存，出厂 A 槽恢复，不重格式化")
            add_text(root, "ota-evidence", 280, 1090,
                     ["10-29：1,553,704 B 镜像下载到 READY，Range 续传及证书拒绝有实证", "完整签名任务确认、安装、自检与上报，生产信任及真实断电测试仍待完成"],
                     size=31, step=63)
        elif n == 16:
            header(root, "问答附录：为什么使用 AI 与 openvela", "APPENDIX · 问答附录")
            replace_text(root, "shape-7", "AI 承担解释与信息整理", size=40)
            replace_text(root, "shape-11", "openvela 承担设备端运行", size=40)
            replace_text(root, "shape-8", ["规则判断已定义的异常", "Agent 按上下文选择只读工具", "结合事实生成告警说明与日报", "没有数据时明确缺失", "AI 价值仍需后续用户评估"],
                         size=39, y=640, step=91)
            replace_text(root, "shape-12", ["LVGL 图形界面与触摸交互", "ai_agent 的 ReAct 与 Skill", "板端执行工具、校验产物", "任务、网络与文件系统协作", "真实设备运行有对应记录"],
                         size=39, y=640, step=91)
            replace_text(root, "shape-13", "专属仓库：github.com/open-vela/contest2026_004_TeamFalcons", size=30)

        if n not in (1, 10, 12):
            add_text(root, "output-page-number", 95, 1382, f"{n:02}" if n <= 12 else f"A{n-12}", size=23, color="#64748B")
        tree.write(folder / f"{stem}.svg", encoding="utf-8", xml_declaration=True)
        notes = item["speech"]
        if item.get("sources"):
            notes += "\n\n资料来源：\n" + "\n".join(item["sources"])
        (note_dir / f"{stem}.md").write_text(notes + "\n", encoding="utf-8", newline="\n")

    (workspace / "page_plan.json").write_text(json.dumps({"schema": "ppt-master.roundtrip-page-plan.v1", "pages": pages}, ensure_ascii=False, indent=2), encoding="utf-8", newline="\n")
    # 用户确认不添加动画和配音。清除模板对象动画，避免删去示例对象后保留动画目标。
    print(f"已填充 {len(pages)} 页。接下来刷新摘要、质量检查并以 --roundtrip --native-charts-and-tables 导出。")


if __name__ == "__main__":
    main()
