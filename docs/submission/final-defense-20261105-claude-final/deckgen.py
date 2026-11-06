#!/usr/bin/env python3
"""VelaGuard 决赛答辩稿页面生成器。

输出 authoring-svg-flat/slide_NN.svg（2560x1440 画布），沿用官方模板继承元素。
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "authoring-svg-flat")

W, H = 2560, 1440
M = 180          # 左右内容边距
TOP = 350        # 正文区上沿
BOT = 1296       # 正文区下沿

INK = "#0F172A"
BODY = "#334155"
MUTED = "#64748B"
FAINT = "#94A3B8"
LINE = "#E2E8F0"
PANEL = "#F8FAFC"
PANEL2 = "#F1F5F9"

CYAN = "#0891B2"
BLUE = "#3B6EF6"
PURPLE = "#9333EA"
AMBER = "#D97706"
RED = "#DC2626"
GREEN = "#16A34A"

MONO = "Consolas, &quot;Courier New&quot;, monospace"
SANS = "MiSans, &quot;Microsoft YaHei&quot;, sans-serif"


def n(v):
    if isinstance(v, float):
        s = f"{v:.2f}".rstrip("0").rstrip(".")
        return s if s else "0"
    return str(v)


def esc(s):
    return (str(s).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))


def attr(s):
    return esc(s).replace('"', "&quot;")


def text(x, y, s, size=40, fill=BODY, weight=None, anchor="start", font=None,
         opacity=None, ls=None, opacity_attr="fill-opacity"):
    a = [f'x="{n(x)}"', f'y="{n(y)}"', f'font-size="{n(size)}"', f'fill="{fill}"']
    if anchor != "start":
        a.append(f'text-anchor="{anchor}"')
    if weight:
        a.append(f'font-weight="{weight}"')
    if font:
        a.append(f'font-family="{font}"')
    if opacity is not None:
        a.append(f'{opacity_attr}="{n(opacity)}"')
    if ls is not None:
        a.append(f'letter-spacing="{n(ls)}"')
    return f'<text {" ".join(a)} xml:space="preserve">{esc(s)}</text>'


def tblock(x, y, lines, size=40, lh=None, fill=BODY, weight=None, anchor="start",
           font=None, first_gap=0):
    """多行文本，后续行用相对 dy（§4.2 契约）。"""
    lh = lh if lh is not None else round(size * 1.5)
    if not lines:
        return ""
    a = [f'x="{n(x)}"', f'y="{n(y)}"', f'font-size="{n(size)}"', f'fill="{fill}"']
    if anchor != "start":
        a.append(f'text-anchor="{anchor}"')
    if weight:
        a.append(f'font-weight="{weight}"')
    if font:
        a.append(f'font-family="{font}"')
    body = esc(lines[0])
    rest = []
    for i, line in enumerate(lines[1:], 1):
        dy = lh if i > 1 else lh + first_gap
        rest.append(f'<tspan x="{n(x)}" dy="{n(dy)}">{esc(line)}</tspan>')
    return f'<text {" ".join(a)} xml:space="preserve">{body}{"".join(rest)}</text>'


def rect(x, y, w, h, fill="none", stroke=None, sw=2, rx=0, opacity=None, dash=None, id=None):
    a = [f'x="{n(x)}"', f'y="{n(y)}"', f'width="{n(w)}"', f'height="{n(h)}"', f'fill="{fill}"']
    if rx:
        a.append(f'rx="{n(rx)}"')
    if stroke:
        a.append(f'stroke="{stroke}"')
        a.append(f'stroke-width="{n(sw)}"')
    if opacity is not None:
        a.append(f'fill-opacity="{n(opacity)}"')
    if dash:
        a.append(f'stroke-dasharray="{dash}"')
    if id:
        a.insert(0, f'id="{id}"')
    return f'<rect {" ".join(a)} />'


def line(x1, y1, x2, y2, stroke=FAINT, sw=2.5, dash=None, end=None, start=None):
    a = [f'x1="{n(x1)}"', f'y1="{n(y1)}"', f'x2="{n(x2)}"', f'y2="{n(y2)}"',
         f'stroke="{stroke}"', f'stroke-width="{n(sw)}"', 'stroke-linecap="round"']
    if dash:
        a.append(f'stroke-dasharray="{dash}"')
    if end:
        a.append(f'marker-end="url(#{end})"')
    if start:
        a.append(f'marker-start="url(#{start})"')
    return f'<line {" ".join(a)} />'


def path(d, fill="none", stroke=None, sw=2.5, dash=None, end=None):
    a = [f'd="{d}"', f'fill="{fill}"']
    if stroke:
        a.append(f'stroke="{stroke}"')
        a.append(f'stroke-width="{n(sw)}"')
        a.append('stroke-linecap="round"')
        a.append('stroke-linejoin="round"')
    if dash:
        a.append(f'stroke-dasharray="{dash}"')
    if end:
        a.append(f'marker-end="url(#{end})"')
    return f'<path {" ".join(a)} />'


def group(gid, children, bounds=None):
    b = f' data-pptx-bounds="{bounds}"' if bounds else ""
    inner = "".join(c for c in children if c)
    return f'<g id="{gid}"{b}>{inner}</g>'


def chip(x, y, w, h, label, color, size=26, fill=None, weight="bold"):
    bg = fill if fill else color
    return (rect(x, y, w, h, fill=bg, rx=h / 2) +
            text(x + w / 2, y + h / 2 + size * 0.36, label, size, "#FFFFFF",
                 weight=weight, anchor="middle"))


def card(x, y, w, h, fill="#FFFFFF", stroke=LINE, rx=18, sw=2):
    out = rect(x, y, w, h, fill=fill, stroke=stroke, sw=sw, rx=rx)
    return out


def label(x, y, s, color=MUTED, size=24):
    return text(x, y, s, size, color, ls=1)


# ---------- 通用页面骨架 ----------

ARROW_HEX = ["#64748B", "#CBD5E1", "#94A3B8", "#0891B2", "#3B6EF6", "#9333EA",
             "#D97706", "#16A34A", "#DC2626", "#F59E0B", "#A855F7", "#F87171"]


def arw(color):
    """箭头填充色与连线描边同色。"""
    return "arw-" + color.lstrip("#")


DEFS = (
    '<defs>'
    '<linearGradient id="ggrad1" x1="0" y1="0.5" x2="1" y2="0.5">'
    '<stop offset="0" stop-color="#0891B2" />'
    '<stop offset="0.5" stop-color="#3B6EF6" />'
    '<stop offset="1" stop-color="#9333EA" />'
    '</linearGradient>'
    + "".join(
        f'<marker id="arw-{c}" orient="auto" markerWidth="6" markerHeight="6" '
        f'refX="4.6" refY="3" viewBox="0 0 6 6">'
        f'<path d="M0,0 L6,3 L0,6 Z" fill="{v}" /></marker>'
        for c, v in [(h.lstrip("#"), h) for h in ARROW_HEX]
    )
    + '</defs>'
)

CHROME = (
    '<g id="layout-shape-4" data-pptx-object="picture" data-pptx-frame="2342.47 61.45 96 96" data-pptx-source-ref="layout:4">'
    '<image data-pptx-object="picture" data-pptx-frame="2342.47 61.45 96 96" href="../images/image1.png" '
    'x="2342.47" y="61.45" width="96" height="96" preserveAspectRatio="none" /></g>'
    '<g id="layout-shape-20" data-pptx-object="shape" data-pptx-frame="1638.47 1354.67 800 40" '
    'data-pptx-prst="rect" data-pptx-source-ref="layout:20" data-pptx-semantic-object="shape">'
    '<path d="M 1638.47 1354.67 L 2438.47 1354.67 L 2438.47 1394.67 L 1638.47 1394.67 Z" fill="none" '
    'stroke-width="2.66667" stroke-linecap="butt" stroke="none" stroke-linejoin="round" data-pptx-part="geometry" />'
    '<text x="2438.47" y="1382.33333333" text-anchor="end" xml:space="preserve" font-family="MiSans, sans-serif" '
    'font-size="20" fill="#64748B" data-pptx-text-baseline="metrics-v1">2026 首届 openvela AI 硬件开发者大赛</text></g>'
    '<g id="layout-shape-21" data-pptx-object="connector" data-pptx-frame="92.33 1333.33 2346 0" '
    'data-pptx-prst="straightConnector1" data-pptx-source-ref="layout:21" data-pptx-semantic-object="shape">'
    '<path d="M 92.33 1333.33 L 2438.33 1333.33" fill="#DEE0E3" stroke-width="1.33333" stroke-linecap="butt" '
    'stroke="#0F172A" stroke-opacity="0.12" stroke-linejoin="round" data-pptx-part="geometry" /></g>'
)

# 封面与封底用各自版式，不能引用正文版式的元素
CHROME_BARE = CHROME
for _ref in (' data-pptx-source-ref="layout:4"',
             ' data-pptx-source-ref="layout:20"',
             ' data-pptx-source-ref="layout:21"'):
    CHROME_BARE = CHROME_BARE.replace(_ref, "")


def image_rounded(x, y, w, h, href, rx=16, cid=None):
    cid = cid or f"clip{int(x)}{int(y)}"
    return (f'<defs><clipPath id="{cid}"><rect x="{n(x)}" y="{n(y)}" width="{n(w)}" '
            f'height="{n(h)}" rx="{n(rx)}" /></clipPath></defs>'
            f'<image href="{href}" x="{n(x)}" y="{n(y)}" width="{n(w)}" height="{n(h)}" '
            f'preserveAspectRatio="xMidYMid slice" clip-path="url(#{cid})" />')


def page_shell(body, number=None, chrome=True):
    head = ('<?xml version=\'1.0\' encoding=\'utf-8\'?>\n'
            '<svg xmlns="http://www.w3.org/2000/svg" version="1.1" width="2560" height="1440" '
            'viewBox="0 0 2560 1440" font-family="' + SANS + '">' + DEFS +
            '<rect x="0" y="0" width="2560" height="1440" fill="#FFFFFF" />')
    pn = ""
    if number is not None:
        pn = (f'<g id="page-number" data-pptx-bounds="88 1352 60 48">'
              f'<path d="M 94.08 1359.33 L 122.66 1359.33 L 122.66 1394.75 L 94.08 1394.75 Z" fill="none" />'
              f'{text(94.08, 1382, f"{number:02d}", 22.93, MUTED, font=SANS)}</g>')
    return head + body + pn + (CHROME if chrome else CHROME_BARE) + '</svg>'


def header(kicker, title, sub=None, page=None):
    out = [group("page-header", [
        text(M, 106, kicker, 24, CYAN, weight="bold", ls=2),
        text(M, 205 if not sub else 198, title, 72 if not sub else 68, INK, weight="bold"),
        rect(M, 285 if not sub else 276, 110, 11, fill="url(#ggrad1)", rx=5.5),
    ], bounds="160 70 2200 250")]
    if sub:
        out.append(group("page-sub", [text(M, 340, sub, 30, MUTED)],
                         bounds="160 300 2200 60"))
    return "".join(out)


def footnote(s, y=1290, color=FAINT, size=24):
    return group("page-footnote", [text(M, y, s, size, color)],
                 bounds=f"160 {y - 40} 2200 60")


# ---------- P1 封面 ----------

def p01_cover():
    body = []
    body.append('<rect x="0" y="0" width="2560" height="1440" fill="#05070E" fill-opacity="0" />')
    body.append(
        '<g id="layout-shape-8" font-family="&quot;MiSans Medium&quot;, sans-serif" data-pptx-object="picture" data-pptx-frame="0.07 0.04 2583.73 1446.32" '
        'data-pptx-source-ref="layout:8">'
        '<svg data-pptx-object="picture" data-pptx-frame="0.07 0.04 2583.73 1446.32" x="0.07" y="0.04" '
        'width="2583.73" height="1446.32" viewBox="0.00476 0.00533 0.99377 0.98164" '
        'preserveAspectRatio="none" overflow="hidden">'
        '<image href="../images/image2.png" x="0" y="0" width="1" height="1" preserveAspectRatio="none" />'
        '</svg></g>'
    )
    body.append(rect(0, 0, 2560, 1440, fill="#050914", opacity=0.18))
    body.append(group("cover-brand", [
        rect(180, 300, 76, 8, fill="url(#ggrad1)", rx=4),
        text(180, 372, "2026 首届 openvela AI 硬件开发者大赛 · 决赛答辩", 30, "#C7D2FE", weight="bold"),
        text(180, 610, "VelaGuard", 200, "#FFFFFF", weight="bold"),
        text(180, 742, "面向小型机房与设备间的 AI 辅助监测网关", 56, "#E2E8F0"),
        text(180, 812, "独立运行在 STM32H750B-DK + openvela 上，本地规则值守，板载 Agent 解释真实数据", 30, "#94A3B8"),
    ], bounds="180 290 2200 560"))
    body.append(group("cover-spec", [
        rect(180, 1016, 2200, 132, fill="#070B14", opacity=0.78, rx=14),
        text(224, 1098, "STM32H750B-DK", 30, "#67E8F9", font=MONO),
        text(604, 1098, "openvela", 30, "#93C5FD", font=MONO),
        text(857, 1098, "LVGL", 30, "#C4B5FD", font=MONO),
        text(1004, 1098, "ai_agent", 30, "#C4B5FD", font=MONO),
        text(1219, 1098, "Modbus RTU", 30, "#67E8F9", font=MONO),
        text(1484, 1098, "MQTTS", 30, "#93C5FD", font=MONO),
        text(1659, 1098, "HTTPS OTA", 30, "#93C5FD", font=MONO),
        text(2336, 1090, "Team Falcons · 叶培林", 30, "#F1F5F9", anchor="end", weight="bold"),
        text(2336, 1128, "答辩 2026-11-07", 24, "#94A3B8", anchor="end"),
    ], bounds="180 1016 2200 132"))
    return page_shell("".join(body), chrome=False)


# ---------- P2 开场视频 ----------

VIDEO_BEATS = [
    ("01", "首页与上电", "网关、从站、RJ45 同框"),
    ("02", "在线点位刷新", "14 点在线，4 条告警"),
    ("03", "越限与告警", "83.2 ℃ 跨过 80.0 ℃"),
    ("04", "Agent 解释", "只读工具，规则与推测分开"),
    ("05", "运行日报", "当天统计与数据来源"),
    ("06", "按键问答", "录音、识别、设备播报"),
    ("07", "手册解析下发", "云端解析，板端确认"),
]


def p02_video():
    body = [header("01 / 演示", "先看它怎么工作", None)]
    body.append(group("video-sub", [
        text(180, 340, "从首页到云端手册接入，7 个片段，78 秒", 30, MUTED),
    ], bounds="180 300 980 60"))
    body.append(group("video-rule", [
        line(1200, 392, 1200, 1130, "#E2E8F0", 2),
    ], bounds="1200 392 2 738"))
    for i, (num, head, desc) in enumerate(VIDEO_BEATS):
        y = 400 + i * 112
        body.append(group(f"video-beat-{i}", [
            text(180, y + 30, num, 22, CYAN, weight="bold", font=MONO),
            text(240, y + 30, head, 28, INK, weight="bold"),
            text(520, y + 30, desc, 21, MUTED),
        ], bounds=f"180 {y} 980 76"))
    body.append(group("video-poster", [
        image_rounded(1240, 360, 1140, 642, "../images/board-run.jpg", 18, "clipvideo"),
        rect(1240, 360, 1140, 642, fill="none", stroke="#CBD5E1", rx=18),
        f'<circle cx="1810" cy="681" r="64" fill="none" stroke="#FFFFFF" stroke-opacity="0.55" stroke-width="2" />',
        f'<circle cx="1810" cy="681" r="56" fill="#0F172A" fill-opacity="0.82" />',
        f'<path d="M 1788 651 L 1846 681 L 1788 711 Z" fill="#FFFFFF" />',
        text(1810, 1040, "播放 78 秒视频", 24, BODY, weight="bold", anchor="middle"),
        text(1810, 1080, "台架实拍，视频单独播放", 20, MUTED, anchor="middle"),
    ], bounds="1240 360 1140 740"))
    return page_shell("".join(body), number=2)


# ---------- P3 一个机房的一天 ----------

BEATS = [
    ("07:40", "交接班", ["逐台核对点位的在线状态", "和最近一次采样时间"],
     "首页", "一屏给出状态与异常"),
    ("09:15", "接入新电表", ["从站地址 3，保持寄存器 0x0000，", "倍率 0.1，逐项写进候选表"],
     "点表", "候选 → 试读 → 人工确认"),
    ("10:12", "回风温度越限", ["当前 83.2 ℃，阈值 80.0 ℃，", "空调仍在制冷"],
     "规则", "本地判定，告警上屏并落盘"),
    ("10:13", "确认原因", ["需要区分空调故障、采样异常", "还是通信中断"],
     "Agent", "只读取值，规则与推测分开"),
    ("16:30", "交班复盘", ["汇总当天告警、恢复次数", "与离线时长"],
     "日报", "落盘，按键提问听取摘要"),
]


def p02_day():
    body = [header("02 / 现场", "一个小机房的一天", None)]
    x0, x1 = 180, 1540
    rail_x = 330
    top = 392
    step = 168
    body.append(line(rail_x, top - 12, rail_x, top + step * 4 + 40, "#CBD5E1", 3))
    body.append(line(958, top - 12, 958, top + step * 4 + 40, "#E2E8F0", 2))
    rows = []
    for i, (tm, title, scene, tag, system) in enumerate(BEATS):
        y = top + i * step
        rows.append(group(f"day-{i + 1}", [
            text(x0, y + 42, tm, 36, INK, weight="bold", font=MONO),
            f'<circle cx="{rail_x}" cy="{y + 32}" r="10" fill="#FFFFFF" stroke="{BLUE}" stroke-width="4" />',
            text(374, y + 42, title, 36, INK, weight="bold"),
            tblock(374, y + 84, scene, 26, 36, MUTED),
            text(996, y + 34, tag, 22, CYAN, weight="bold", ls=1),
            tblock(996, y + 72, [system], 27, 38, BODY),
        ], bounds=f"{x0} {y - 6} 1370 156"))
    body.append("".join(rows))
    body.append(group("day-screen", [
        image_rounded(1640, 392, 740, 419, "../images/home_14_alarm.png", 16, "clipday"),
        rect(1640, 880, 740, 188, fill=PANEL, stroke=LINE, rx=14),
        text(1672, 926, "当前 4 条告警", 22, AMBER, weight="bold", ls=1),
        text(1672, 974, "回风温度 83.2 ℃", 24, INK, weight="bold", font=MONO),
        text(1672, 1014, "UPS 负载 102.3% · 烟雾 10.0%", 23, BODY, font=MONO),
        text(1672, 1054, "水浸 1.0 · 其余 10 个点位正常", 23, BODY, font=MONO),
    ], bounds="1640 392 740 700"))
    return page_shell("".join(body), number=3)


def stat(x, y, value, unit, label, color=INK, vsize=64):
    return (text(x, y, value, vsize, color, weight="bold", font=MONO) +
            text(x + len(value) * vsize * 0.58 + 10, y, unit, 24, MUTED) +
            text(x, y + 40, label, 24, MUTED))


def panel(x, y, w, h, title=None, accent=None, fill="#FFFFFF", stroke=LINE):
    out = rect(x, y, w, h, fill=fill, stroke=stroke, rx=18)
    if title:
        out += rect(x, y, 8, 40, fill=accent or BLUE, rx=4)
        out += text(x + 28, y + 32, title, 28, INK, weight="bold")
    return out


def node(x, y, w, h, title, sub=None, accent=BLUE, fill="#FFFFFF", tsize=30, ssize=22):
    out = rect(x, y, w, h, fill=fill, stroke=accent, sw=2.5, rx=14)
    if sub:
        out += text(x + w / 2, y + h / 2 - 4, title, tsize, INK, weight="bold", anchor="middle")
        out += text(x + w / 2, y + h / 2 + 34, sub, ssize, MUTED, anchor="middle")
    else:
        out += text(x + w / 2, y + h / 2 + tsize * 0.35, title, tsize, INK, weight="bold", anchor="middle")
    return out


def harrow(x1, x2, y, color="#94A3B8", sw=3):
    return line(x1, y, x2, y, color, sw, end=arw(color))


# ---------- P3 架构与任务 ----------

LAYERS = [
    ("云端与网络", ["MiMo 推理 (HTTPS)", "MQTT Broker", "云看板 (只读)"], "#9333EA"),
    ("板载应用", ["采集与规则", "LVGL HMI", "只读工具桥", "OTA 客户端"], "#3B6EF6"),
    ("openvela 组件", ["ai_agent", "LVGL", "lwIP", "mbedTLS", "FAT / SDMMC"], "#0891B2"),
    ("硬件", ["STM32H750B-DK", "RS485", "eMMC", "LCD + 触摸", "RJ45"], "#64748B"),
]

LINK_LABELS = ["HTTPS 推理 · MQTTS 控制", "系统调用", "驱动与中断"]

TASKS = [
    ("ai_agent", "100", "32 KiB", "ReAct 轮次、HTTPS/TLS、Skill 载入"),
    ("vghmi", "100", "48 KiB", "LVGL 页面、渲染与触摸事件"),
    ("acq", "100", "8 KiB", "Modbus 主站轮询，vghmi 内 pthread"),
    ("vgvoice", "110", "4 KiB", "语音轮次与云侧 ASR/TTS 调度"),
    ("vgptt", "90", "4 KiB", "按键录音与播放投递"),
    ("nsh", "100", "8 KiB", "配置、试读、救援与维护命令"),
]


def p03_arch():
    body = [header("03 / 架构", "一台设备上的四层与六个任务", None)]
    x, y0 = 180, 380
    w = 1290
    for i, (name, items, color) in enumerate(LAYERS):
        y = y0 + i * 218
        body.append(group(f"layer-{i}", [
            rect(x, y, w, 178, fill="#FFFFFF", stroke=LINE, rx=16),
            rect(x, y, 10, 178, fill=color, rx=5),
            text(x + 40, y + 56, name, 34, INK, weight="bold"),
            "".join(
                rect(x + 40 + j * 246, y + 84, 226, 66, fill=PANEL, stroke="#E2E8F0", rx=10) +
                text(x + 40 + j * 246 + 113, y + 128, it, 24, BODY, anchor="middle")
                for j, it in enumerate(items)
            ),
        ], bounds=f"{x} {y} {w} 178"))
        if i < len(LAYERS) - 1:
            body.append(group(f"link-{i}", [
                line(x + w / 2, y + 178, x + w / 2, y + 218, "#CBD5E1", 3, end=arw("#CBD5E1")),
                text(x + w / 2 + 24, y + 208, LINK_LABELS[i], 21, FAINT),
            ], bounds=f"{x + w / 2} {y + 178} 440 40"))
    tx, tw = 1560, 820
    body.append(group("task-table", [
        rect(tx, 380, tw, 830, fill=PANEL, stroke=LINE, rx=18),
        text(tx + 32, 440, "板端任务", 30, INK, weight="bold"),
        text(tx + 32, 482, "优先级与栈均为构建期常量", 22, MUTED),
        "".join(
            rect(tx + 24, 516 + i * 112, tw - 48, 96, fill="#FFFFFF", stroke="#E2E8F0", rx=12) +
            text(tx + 48, 558 + i * 112, name, 26, INK, weight="bold", font=MONO) +
            text(tx + 372, 558 + i * 112, "prio " + prio, 21, MUTED, font=MONO) +
            text(tx + tw - 56, 558 + i * 112, stack, 25, BLUE, font=MONO, anchor="end") +
            text(tx + 48, 594 + i * 112, role, 22, MUTED)
            for i, (name, prio, stack, role) in enumerate(TASKS)
        ),
    ], bounds=f"{tx} 380 {tw} 830"))
    return page_shell("".join(body), number=4)


# ---------- P4 采集到告警 ----------

STAGES = [
    ("RS485 帧", ["9600 8N1", "半双工 DIR 时序", "帧级 CRC 统计"]),
    ("Modbus 主站", ["功能码 03", "单主站独占总线", "超时与重试"]),
    ("已确认点表", ["最多 32 点", "类型 / 倍率 / 单位", "候选表不参与采集"]),
    ("规则判定", ["阈值 warn / crit", "离线与数据过期", "事件写入运行记录"]),
    ("三个出口", ["LVGL 告警上屏", "eMMC 事件落盘", "MQTT 遥测与告警"]),
]

BANDS = [("0 ms", "649 / 649", "修复前丢帧的同一工况"),
         ("5 ms", "525 / 525", "同轮连续采集"),
         ("20 ms", "471 / 471", "同轮连续采集"),
         ("150 ms", "454 / 454", "同轮连续采集")]


def p04_pipeline():
    body = [header("04 / 采集", "从一帧 RS485 到一条告警", None)]
    y, h = 384, 270
    x0, gap, bw = 180, 62, 390
    stages = [
        ("RS485 帧", ["9600 8N1", "半双工 DIR 时序", "帧级 CRC"]),
        ("Modbus 主站", ["功能码 03", "单主站独占总线", "超时与重试"]),
        ("已确认点表", ["候选只写", "人确认后生效", "最多 32 点"]),
        ("规则判定", ["阈值 warn / crit", "离线与数据过期", "事件写入运行记录"]),
        ("三个出口", ["LVGL 告警上屏", "eMMC 事件落盘", "MQTT 遥测与告警"]),
    ]
    for i, (title, items) in enumerate(stages):
        x = x0 + i * (bw + gap)
        accent = [CYAN, CYAN, BLUE, AMBER, PURPLE][i]
        gate = i == 2
        body.append(group(f"stage-{i}", [
            rect(x, y, bw, h, fill="#EFF6FF" if gate else "#FFFFFF",
                 stroke=accent, sw=4 if gate else 2.5, rx=16),
            text(x + 28, y + 56, f"{i + 1}", 30, accent, weight="bold", font=MONO),
            text(x + 28, y + 110, title, 34, INK, weight="bold"),
            tblock(x + 28, y + 168, items, 24, 40, MUTED),
        ], bounds=f"{x} {y} {bw} {h}"))
        if i < len(stages) - 1:
            body.append(harrow(x + bw + 10, x + bw + gap - 10, y + h / 2))

    body.append(group("gate-candidate", [
        rect(180, 700, 480, 200, fill="#FFFBEB", stroke="#FCD34D", rx=16),
        text(212, 756, "候选表", 30, "#92400E", weight="bold"),
        text(212, 800, "candidate.json", 22, "#B45309", font=MONO),
        text(212, 850, "add / set / del 只写这一份", 22, BODY),
    ], bounds="180 700 480 200"))
    body.append(group("gate-human", [
        line(660, 800, 738, 800, "#F59E0B", 3, end=arw("#F59E0B")),
        f'<circle cx="780" cy="800" r="42" fill="#FFFFFF" stroke="#F59E0B" stroke-width="3" />',
        text(780, 812, "人", 30, "#B45309", weight="bold", anchor="middle"),
        text(780, 744, "apply --confirm", 22, AMBER, weight="bold", anchor="middle", font=MONO),
        line(822, 800, 900, 800, "#F59E0B", 3, end=arw("#F59E0B")),
    ], bounds="660 700 240 200"))
    body.append(group("gate-committed", [
        rect(900, 700, 480, 200, fill="#F0FDF4", stroke="#86EFAC", rx=16),
        text(932, 756, "已确认表", 30, "#15803D", weight="bold"),
        text(932, 800, "points.json", 22, "#15803D", font=MONO),
        text(932, 850, "采集 / 首页 / 告警只读它", 22, BODY),
    ], bounds="900 700 480 200"))

    body.append(group("nsh-evidence", [
        rect(1480, 700, 900, 240, fill="#0B1220", rx=16),
        text(1508, 736, "2026-10-18 板端实录 · 14 行 READ 全部 ok=1", 19, "#64748B"),
        text(1508, 778, "nsh> vgpoint test", 21, "#E2E8F0", font=MONO),
        text(1508, 812, "READ id=acu_ret raw=832 value=83.2 ok=1", 21, "#67E8F9", font=MONO),
        text(1508, 846, "OK cmd=test table=candidate n=14", 21, "#4ADE80", font=MONO),
        text(1508, 880, "vgpoint apply → ERR code=need_confirm · exit 1", 21, "#F87171", font=MONO),
        text(1508, 914, "vgpoint apply --confirm → OK table=committed", 21, "#4ADE80", font=MONO),
    ], bounds="1480 700 900 240"))

    chips = [("8 KiB", "线程栈"), ("50 ms", "点间间隔"),
             ("500 ms", "读超时 + 重试 1 次"), ("200 ms", "轮末 sleep"),
             ("跳过", "总线锁忙")]
    body.append(group("acq-const", [
        rect(180, 990, 2200, 130, fill=PANEL, stroke=LINE, rx=14),
        text(212, 1036, "采集线程", 24, INK, weight="bold"),
        text(212, 1070, "vghmi 内 detached pthread", 19, MUTED),
        "".join(
            rect(600 + i * 336, 1014, 320, 82, fill="#FFFFFF", stroke="#E2E8F0", rx=10) +
            text(620 + i * 336, 1056, value, 26, INK, weight="bold", font=MONO) +
            text(620 + i * 336, 1088, label, 18, MUTED)
            for i, (value, label) in enumerate(chips)
        ),
    ], bounds="180 990 2200 130"))
    return page_shell("".join(body), number=5)


# ---------- P5 对外开放的接口 ----------

IFACES = [
    ("上位机", "PC ↔ 板", ["ST-LINK VCP · USART3", "COM3 115200 8N1"],
     "vgpoint 8 动词 · vgcfg · vgdiscover",
     ["32 点上限", "缺 --confirm → need_confirm / exit 1"], "已实现", GREEN),
    ("看板", "板 → 云", ["MQTT 1883", "测试链路 · 仅上行"],
     "status · telemetry · alarm · point_table",
     ["30 s 周期", "告警队列 8 条"], "已实现", GREEN),
    ("候选配置", "云 → 板", ["MQTT over TLS", "QoS1 · 非 retained"],
     "config/request · config/state",
     ["请求 16 KiB", "lease 600 s"], "板端已验", AMBER),
    ("OTA", "云 ↔ 板", ["MQTTS 8883", "HTTPS 数据面"],
     "ota/offer · status · result · confirm",
     ["offer 1536 B", "本机长按 2 s"], "板端已验", AMBER),
    ("手机", "手机 → 云 → 板", ["HTTPS + MQTT TLS", "云侧解析"],
     "PDF + 从机地址 · 语音巡检",
     ["候选只到本机确认页", "语音只读"], "接口已定", "#94A3B8"),
    ("现场总线", "板 → 从站", ["RS485", "9600 8N1"],
     "Modbus FC03 · 单主站",
     ["14 点 / 轮"], "已实现", GREEN),
]


def p05_interfaces():
    body = [header("05 / 接口", "谁能改这台设备", None)]
    cols = ["上位机 NSH", "看板 MQTT", "候选 MQTT", "OTA MQTTS", "手机", "Agent", "本机屏幕/按键"]
    rows = [
        ("读实时值", [
            ("✓", "vgpoint get", GREEN, True),
            ("订阅", "4 主题", BLUE, False),
            ("—", "", MUTED, False),
            ("—", "", MUTED, False),
            ("只读", "语音巡检", BLUE, False),
            ("✓", "5 工具", GREEN, False),
            ("✓", "首页 / 告警", GREEN, False),
        ]),
        ("写候选点表", [
            ("✓", "add / set / del", GREEN, True),
            ("—", "", MUTED, False),
            ("✓", "put", GREEN, True),
            ("—", "", MUTED, False),
            ("提议", "PDF + 地址", AMBER, False),
            ("—", "", MUTED, False),
            ("—", "", MUTED, False),
        ]),
        ("提交点表", [
            ("✓", "apply --confirm", GREEN, True),
            ("—", "", MUTED, False),
            ("—", "", MUTED, False),
            ("—", "", MUTED, False),
            ("—", "", MUTED, False),
            ("—", "", MUTED, False),
            ("✓", "二次确认", GREEN, False),
        ]),
        ("确认安装", [
            ("—", "", MUTED, False),
            ("—", "", MUTED, False),
            ("—", "", MUTED, False),
            ("提议", "offer", AMBER, True),
            ("—", "", MUTED, False),
            ("—", "", MUTED, False),
            ("✓", "长按 2 s", GREEN, False),
        ]),
        ("控制输出", [
            ("—", "", MUTED, False),
            ("—", "", MUTED, False),
            ("—", "", MUTED, False),
            ("—", "", MUTED, False),
            ("—", "", MUTED, False),
            ("—", "", MUTED, False),
            ("—", "", MUTED, False),
        ]),
    ]
    x0, label_w, cw = 180, 300, 270
    y0, hh, rh = 384, 70, 138
    body.append(group("perm-head", [
        rect(x0, y0, 2200, hh, fill="#0F172A", rx=10),
        "".join(
            text(x0 + label_w + i * cw + cw / 2, y0 + 45, c, 20, "#E2E8F0",
                 weight="bold", anchor="middle")
            for i, c in enumerate(cols)
        ),
    ], bounds=f"{x0} {y0} 2200 {hh}"))
    for r, (label, cells) in enumerate(rows):
        y = y0 + hh + 8 + r * (rh + 6)
        body.append(group(f"perm-{r}", [
            rect(x0, y, 2200, rh, fill="#FFFFFF" if r % 2 == 0 else "#F8FAFC", rx=10),
            line(x0 + label_w, y + 12, x0 + label_w, y + rh - 12, "#E2E8F0", 2),
            text(x0 + 32, y + 58, label, 25, INK, weight="bold"),
            "".join(
                text(x0 + label_w + i * cw + cw / 2, y + 58, sym,
                     30 if sym in ("✓", "—") else 22,
                     color, weight="bold", anchor="middle") +
                (text(x0 + label_w + i * cw + cw / 2, y + 96, detail,
                      18, MUTED, anchor="middle", font=MONO if mono else None)
                 if detail else "")
                for i, (sym, detail, color, mono) in enumerate(cells)
            ),
        ], bounds=f"{x0} {y} 2200 {rh}"))
    body.append(group("perm-legend", [
        text(180, 1222, "✓ 可直接执行", 19, GREEN, weight="bold"),
        text(430, 1222, "提议 只发起不生效", 19, AMBER, weight="bold"),
        text(750, 1222, "订阅 / 只读 仅读取", 19, BLUE, weight="bold"),
        text(1090, 1222, "— 无入口", 19, MUTED, weight="bold"),
    ], bounds="180 1196 1200 40"))
    return page_shell("".join(body), number=6)


TOOLS = [
    ("vg_point_read", "单点实时值、单位、新鲜度、阈值"),
    ("vg_anomaly_read", "全部异常点位，按严重程度排序"),
    ("vg_run_report", "通信质量、在线情况、异常时间线"),
    ("vg_alarm_recovery", "告警是否恢复，只认本次上电事件"),
    ("vg_status_read", "在线、告警、网络与 AI 连通性概况"),
]


def p06_agent():
    body = [header("06 / Agent", "告警进来，Agent 怎么跑完一轮", None)]
    steps = [("触发", "告警变化 / 定时日报"), ("载入 Skill", "alarm_interpretation.md"),
             ("LLM 规划", "MiMo over HTTPS"), ("工具调用", "只读取值，结果回填")]
    bw, gap, y = 280, 53, 520
    for i, (title, sub) in enumerate(steps):
        x = 180 + i * (bw + gap)
        body.append(group(f"step-{i}", [
            rect(x, y, bw, 150, fill="#FFFFFF", stroke=PURPLE, sw=2.5, rx=14),
            text(x + bw / 2, y + 64, title, 30, INK, weight="bold", anchor="middle"),
            text(x + bw / 2, y + 108, sub, 20, MUTED, anchor="middle"),
        ], bounds=f"{x} {y} {bw} 150"))
        if i:
            body.append(harrow(x - gap + 10, x - 10, y + 75))
    # 回填环路
    x_tool = 180 + 3 * (bw + gap) + bw / 2
    x_llm = 180 + 2 * (bw + gap) + bw / 2
    body.append(path(f"M {x_tool} {y + 150} L {x_tool} 770 L {x_llm} 770 L {x_llm} {y + 158}",
                     stroke="#A855F7", sw=3, end=arw("#A855F7")))
    body.append(group("loop-note", [
        tblock(180, 832, ["工具结果回填到上下文，同一轮最多 10 次迭代，",
                          "每次响应最多 2 个工具调用。"], 24, 38, MUTED),
    ], bounds="180 790 1000 90"))
    # 三个上限
    body.append(group("limits", [
        rect(180, 920, 1280, 200, fill=PANEL, stroke=LINE, rx=16),
        text(212, 986, "20", 48, PURPLE, weight="bold", font=MONO),
        text(288, 986, "次 / 60 秒", 23, MUTED),
        text(212, 1026, "工具调用速率上限", 21, MUTED),
        text(520, 986, "300", 48, INK, weight="bold", font=MONO),
        text(618, 986, "秒 · 单轮截止", 23, MUTED),
        text(520, 1026, "超时放弃，不补写解释", 21, MUTED),
        text(910, 986, "32", 48, INK, weight="bold", font=MONO),
        text(982, 986, "KiB · Agent 栈", 23, MUTED),
        text(910, 1026, "TLS 与 JSON 同栈", 21, MUTED),
    ], bounds="180 920 1280 200"))
    # 右侧工具表
    body.append(group("tool-table", [
        rect(1520, 384, 860, 500, fill=PANEL, stroke=LINE, rx=18),
        text(1552, 442, "五个只读工具", 30, INK, weight="bold"),
        "".join(
            text(1552, 506 + i * 78, name, 25, PURPLE, font=MONO, weight="bold") +
            text(1552, 542 + i * 78, desc, 22, BODY)
            for i, (name, desc) in enumerate(TOOLS)
        ),
    ], bounds="1520 384 860 500"))
    # 右侧：自然语言问答
    body.append(group("chat", [
        text(1520, 920, "自然语言问答", 22, CYAN, weight="bold", ls=1),
        image_rounded(1520, 940, 860, 340, "../images/chat_temp_crop.png", 14, "clipchat"),
    ], bounds="1520 900 860 380"))
    body.append(group("contract", [
        text(180, 1160, "VGADV1 · boot/req 匹配 · 8 条上限 · 任一项不合格整份丢弃", 21, MUTED, font=MONO),
    ], bounds="180 1136 1280 40"))
    return page_shell("".join(body), number=7)


# ---------- P7 只读权限 ----------

def p07_guard():
    body = [header("07 / 边界", "只读不是提示词，是 C 代码", None)]
    layers = [
        ("注册层", "只注册 5 个 VelaGuard 只读工具", ["产品 allowlist 是硬上限，运行配置只能关不能加", "vgpoint / vgcfg / vgdiscover apply 不注册"], CYAN),
        ("运行层", "参数、条数与频率在 C 里强制", ["点位查询超过 48 字节直接拒绝，不截断", "单次输出上限 2048 字节，列表 12 条", "20 次 / 60 秒，窗口外返回 rate_limited"], BLUE),
        ("审计层", "每次执行都计数并留痕", ["exec_count 原子累加，供回答依据判定", "审计写 /data/velaguard/logs/agent_tools.log", "单文件 64 KiB 轮转，参数先脱敏"], PURPLE),
    ]
    y0 = 390
    for i, (name, head, items, color) in enumerate(layers):
        y = y0 + i * 268
        body.append(group(f"guard-{i}", [
            rect(180, y, 1320, 250, fill="#FFFFFF", stroke=LINE, rx=18),
            rect(180, y, 12, 250, fill=color, rx=6),
            text(216, y + 52, name, 26, color, weight="bold", ls=1),
            text(216, y + 104, head, 34, INK, weight="bold"),
            tblock(216, y + 158, items, 24, 40, MUTED),
        ], bounds=f"180 {y} 1320 250"))
    body.append(group("deny", [
        rect(1560, 390, 820, 464, fill="#0F172A", rx=18),
        text(1596, 448, "明确不做", 26, "#F87171", weight="bold", ls=1),
        tblock(1596, 512, ["写寄存器", "应用配置", "清除告警", "控制数字量输出", "覆盖本地安全规则"], 30, 62, "#E2E8F0"),
    ], bounds="1560 390 820 464"))
    body.append(group("audit", [
        rect(1560, 890, 820, 360, fill=PANEL, stroke=LINE, rx=18),
        text(1596, 948, "工具表在编译期固定", 26, INK, weight="bold"),
        text(1596, 1000, "#define VG_TOOL_RATE_WINDOW_MS 60000u", 22, "#334155", font=MONO),
        text(1596, 1040, "#define VG_TOOL_RATE_MAX_CALLS  20", 22, "#334155", font=MONO),
        text(1596, 1100, "速率规则写在 vg_agent_query.c，", 22, MUTED),
        text(1596, 1134, "主机测试可直接驱动。", 22, MUTED),
        text(1596, 1196, "vg_agent_tools.c", 22, PURPLE, font=MONO),
    ], bounds="1560 890 820 360"))
    return page_shell("".join(body), number=8)


def tw(s, size):
    return sum(size if ord(c) > 127 else size * 0.56 for c in s)


def msg(x1, x2, y, label, color="#64748B", dashed=False, size=22):
    out = line(x1, y, x2, y, color, 2.5,
               dash="8 8" if dashed else None, end=arw(color))
    cx = (x1 + x2) / 2
    w = tw(label, size) + 24
    out += group(f"msg-{int(cx)}-{int(y)}", [
        rect(cx - w / 2, y - 34, w, 30, fill="#FFFFFF"),
        text(cx, y - 10, label, size, color, anchor="middle"),
    ], bounds=f"{cx - w / 2} {y - 42} {w} 42")
    return out


def p08_sequence():
    body = [header("08 / 时序", "一条告警解释的端到端路径", None)]
    lanes = [(310, "规则引擎", AMBER, 700), (730, "任务队列", CYAN, 700),
             (1190, "ai_agent", PURPLE, 1150), (1700, "MiMo", BLUE, 1150),
             (2210, "屏幕 / 存储", GREEN, 1150)]
    for x, name, color, yend in lanes:
        body.append(group(f"lane-{name}", [
            rect(x - 125, 384, 250, 92, fill="#FFFFFF", stroke=color, sw=2.5, rx=14),
            text(x, 442, name, 26, INK, weight="bold", anchor="middle"),
        ], bounds=f"{x - 125} 384 250 92"))
        body.append(line(x, 480, x, yend, "#E2E8F0", 2, dash="6 8"))
    body.append(group("triggers", [
        rect(180, 770, 760, 380, fill=PANEL, stroke=LINE, rx=16),
        text(212, 826, "一轮从哪里来", 26, INK, weight="bold"),
        "".join(
            f'<circle cx="218" cy="{886 + i * 78}" r="6" fill="{c}" />' +
            text(240, 894 + i * 78, head, 24, INK, weight="bold") +
            text(240, 928 + i * 78, desc, 20, MUTED)
            for i, (head, desc, c) in enumerate([
                ("告警边沿", "阈值或离线状态变化，写事件后投递任务", AMBER),
                ("定时日报", "到点自动生成，不需要人提问", CYAN),
                ("按键提问", "人按下后走语音链路，仍然只读", PURPLE),
            ])
        ),
        text(212, 1112, "同一条告警在一轮里只解释一次。", 20, FAINT),
    ], bounds="180 770 760 380"))
    msgs = [
        (310, 730, 580, "阈值越限，写事件记录", AMBER, False),
        (730, 1190, 650, "投递解释任务", CYAN, False),
        (1190, 1700, 720, "Skill + 上下文 + 工具表", PURPLE, False),
        (1700, 1190, 800, "tool_call", BLUE, True),
        (1190, 1700, 950, "工具结果回填", PURPLE, False),
        (1700, 1190, 1020, "最终 JSON", BLUE, True),
        (1190, 2210, 1090, "schema 校验后上屏", GREEN, False),
        (1190, 2210, 1140, "解释写 alarm_advice.txt", GREEN, False),
    ]
    for x1, x2, y, label, color, dashed in msgs:
        body.append(msg(x1, x2, y, label, color, dashed))
    body.append(group("tool-call-note", [
        path("M 1170 838 L 1104 838 L 1104 884 L 1170 884", stroke=PURPLE, sw=2.5, end=arw(PURPLE)),
        text(1090, 856, "执行只读工具", 21, PURPLE, anchor="end"),
        text(1090, 890, "读采集快照", 21, FAINT, anchor="end"),
    ], bounds="940 818 250 94"))
    body.append(group("takeaway", [
        rect(180, 1190, 2200, 100, fill=PANEL, stroke=LINE, rx=16),
        text(212, 1250, "8 条共 41.83 s，单条 5.23 s；模型推理占 95%，工具取数 1.96 s 读的是快照，不占 RS485。", 26, BODY),
    ], bounds="180 1190 2200 100"))
    return page_shell("".join(body), number=9)


def p09_offline():
    body = [header("09 / 断网", "网线拔掉，值守不中断", None)]
    rows = [
        ("RJ45 拔掉", "采集、判定、上屏、事件落盘照常", "MQTT 与 MiMo 不可达", "插回后按主题补发", GREEN),
        ("MQTT Broker 断开", "告警进 8 条 RAM 队列，满则丢最旧", "云看板停更", "重连补发，断电不补", AMBER),
        ("MiMo 超时", "屏幕保留本地规则与真实读数", "不生成解释与日报", "下一轮重新触发", AMBER),
        ("从站离线", "8 个样本里失败 5 次即判离线", "告警按 QoS 1 上报", "一次成功采样即恢复", CYAN),
    ]
    cols = [(212, "故障"), (640, "本地行为"), (1460, "云端与上报"), (1900, "恢复")]
    body.append(group("m-head", [
        rect(180, 384, 2200, 64, fill="#0F172A", rx=10),
        "".join(text(x, 426, h, 23, "#E2E8F0", weight="bold") for x, h in cols),
    ], bounds="180 384 2200 64"))
    for i, (fault, local, cloud, recover, color) in enumerate(rows):
        y = 456 + i * 118
        body.append(group(f"m-{i}", [
            rect(180, y, 2200, 104, fill="#FFFFFF" if i % 2 == 0 else PANEL),
            rect(180, y, 6, 104, fill=color),
            text(212, y + 62, fault, 27, INK, weight="bold"),
            text(640, y + 62, local, 23, BODY),
            text(1460, y + 62, cloud, 23, MUTED),
            text(1900, y + 62, recover, 23, MUTED),
        ], bounds=f"180 {y} 2200 104"))
    body.append(group("offline-note", [
        rect(180, 990, 2200, 240, fill="#0F172A", rx=18),
        text(216, 1048, "断网时发生什么", 24, "#94A3B8", weight="bold", ls=1),
        text(216, 1116, "规则与显示跑在本地任务里，网络失败只影响解释、日报和上报。", 28, "#E2E8F0"),
        text(216, 1176, "演示：拔掉 RJ45，观察采样时间继续前进；插回后 status 主题恢复在线。", 24, "#64748B"),
    ], bounds="180 990 2200 240"))
    return page_shell("".join(body), number=10)


def p11_ota():
    body = [header("11 / OTA", "一次 OTA 从 offer 到回滚", None)]
    stages = [
        ("控制面 · MQTTS 8883", "Offer", PURPLE, [
            "ota/offer 订阅",
            "status · result",
            "confirm 发布",
            "9 字段 · ≤1536 B",
            "QoS1 · 非 retained",
        ]),
        ("数据面", "Download", CYAN, [
            "HTTPS · eth0",
            "Range + If-Range",
            "/mnt/emmc/VGOTA",
            "先取 manifest 512 B",
        ]),
        ("数据面", "Verify", CYAN, [
            "TLS 链 · 主机名 · 时间",
            "manifest CRC32 + 绑定",
            "ECDSA P-256",
            "流式 SHA-256 + 回读",
            "前 8 B 向量表",
        ]),
        ("本机", "Local confirm", AMBER, [
            "屏幕长按 2 s / 按键",
            "5 项联锁",
            "READY 600 s",
            "远程 -EPERM",
        ]),
        ("安装", "Install", BLUE, [
            "仅 boot stub 擦写",
            "B 槽 8 MiB",
            "BCB 2×4096 B",
            "先写 FAILED 围栏",
        ]),
        ("回滚", "Self-test", RED, [
            "B 槽确定性自检",
            "通过 → CONFIRMED",
            "失败 → trial + 1",
            "trial 3 → 回 A",
            "IWDG 32 s",
        ]),
    ]
    widths = [320, 320, 440, 320, 320, 360]
    x = 180
    y, h = 384, 360
    for i, (lane, title, color, lines) in enumerate(stages):
        w = widths[i]
        body.append(group(f"ota-st-{i}", [
            rect(x, y, w, h, fill="#FFFFFF", stroke=color, sw=2.5, rx=16),
            rect(x, y, w, 8, fill=color, rx=4),
            text(x + 24, y + 44, lane, 18, color, weight="bold", ls=1),
            text(x + 24, y + 94, title, 28, INK, weight="bold"),
            tblock(x + 24, y + 140, lines, 20, 36, MUTED),
        ], bounds=f"{x} {y} {w} {h}"))
        if i < len(stages) - 1:
            body.append(harrow(x + w + 2, x + w + 22, y + 188))
        x += w + 24

    panels = [
        ("控制面", "#F5F3FF", "#C4B5FD", PURPLE, [
            "vg/{id}/ota/offer 订阅",
            "status · result · confirm 发布",
            "QoS1 · status 每 2 s",
            "outbox 8×256 B",
            "result 6 种取值",
        ]),
        ("数据面", "#ECFEFF", "#A5F3FC", CYAN, [
            "manifest 512 B 二进制 + CRC32",
            "镜像上限 8,380,416 B",
            "Range + If-Range",
            "900 s · 3 次重试",
            "暂存 image + 4×4096 + 1 MiB",
        ]),
        ("槽位与 BCB", PANEL, LINE, BLUE, [
            "A/B 各 8 MiB",
            "BCB 2×4096 B · 140 B 头",
            "CRC32 · 取最高有效 seq",
            "trial 3 · IWDG 32 s",
            "A 0x90000000 / B 0x90800000",
        ]),
    ]
    for i, (title, fill, stroke, color, lines) in enumerate(panels):
        px = 180 + i * 750
        body.append(group(f"ota-p-{i}", [
            rect(px, 790, 700, 380, fill=fill, stroke=stroke, rx=16),
            text(px + 28, 842, title, 26, color, weight="bold"),
            tblock(px + 28, 900, lines, 20, 42, BODY),
        ], bounds=f"{px} 790 700 380"))
    return page_shell("".join(body), number=12)


def p10_hard():
    body = [header("10 / 攻坚", "三个难住我们的问题", None)]
    stories = [
        ("01", "TLS 握手 -0x4380", "网络 / TLS", RED, [
            ["critical 告警态 15 次探测 12 次失败，", "13 次 -0x4380；静屏 30/30 通过。"],
            ["ServerHello random 32 B 复制源在", "SDRAM 奇地址；memcpy 内联成 LDR；", "H750 FMC 不支持非对齐读（ES0392）。"],
            ["nuttx 45dd33ad936 加", "-mno-unaligned-access；", "ai_agent 7c33205 / 67e8b14 修请求", "初始化与 POLLOUT。"],
            ["4 轮 155 次探测 0 次 -0x4380；", "正式镜像 ASR→LLM→TTS 全部完成；", "8 次新握手无错。"],
        ]),
        ("02", "RS485 DIR 晚释放", "采集 / 时序", AMBER, [
            ["0 ms 从站 274 次请求只有 38 次成功，", "223 次超时；3–4/14 点在线。"],
            ["DIR 只在 up_txempty() 切回接收；", "tcdrain 轮询加 10 ms tick，实测晚", "12.7–13.8 ms，0 ms 从站 1–2 ms 的", "回复落在发送窗口。"],
            ["nuttx ff29d9b1a58 在 TX 完成中断写", "DIR；up_txempty 改纯查询，删 400 us", "忙等；参赛仓 ff140dd。"],
            ["0 ms 档从 38/274 到 649/649，", "14/14 在线。"],
        ]),
        ("03", "按键 ASR 必败", "语音 / 状态机", PURPLE, [
            ["松手或 15 秒上限后提示识别失败；", "串口 asr rc=io status=0 req=0，", "录音 RAM 有数据但识别拿不到 clip。"],
            ["STOP_RECORD | START_ASR 同时到达，", "STOP_RECORD 无条件 finish_owned 并", "清零，紧接着 take_owned(0) 失败。"],
            ["参赛仓 2120509 仅在没有 START_ASR", "时取消并清零；新增 check_ptt_capture.py。"],
            ["3 秒松手 RAM 95,972 B、HTTP 200、", "请求 128,193 B；15 秒 476,116 B；", "真人复测 81,450 B、asr rc=ok。"],
        ]),
    ]
    heads = ["现象", "根因", "修复", "验证"]
    col_x = [560, 1010, 1460, 1910]
    for i, head in enumerate(heads):
        body.append(text(col_x[i], 404, head, 18, MUTED, weight="bold", ls=1))
    for r, (idx, title, sub, color, cells) in enumerate(stories):
        y = 430 + r * 260
        body.append(group(f"hard-{r}", [
            rect(180, y, 2200, 250, fill="#FFFFFF" if r % 2 == 0 else "#F8FAFC", rx=14),
            rect(180, y + 12, 6, 226, fill=color, rx=3),
            text(216, y + 55, idx, 22, color, weight="bold", font=MONO),
            text(216, y + 104, title, 28, INK, weight="bold"),
            text(216, y + 142, sub, 18, MUTED),
            tblock(col_x[0], y + 48, cells[0], 20, 30, BODY),
            tblock(col_x[1], y + 48, cells[1], 20, 30, BODY),
            tblock(col_x[2], y + 48, cells[2], 20, 30, BODY),
            tblock(col_x[3], y + 48, cells[3], 20, 30, BODY),
        ], bounds=f"180 {y} 2200 250"))
    body.append(group("hard-close", [
        text(180, 1240, "三次根因分别在驱动、时序和状态机，业务层没有改。", 21, FAINT),
    ], bounds="180 1216 1400 34"))
    return page_shell("".join(body), number=11)


def p12_industry():
    body = [header("12 / 行业", "从一间机房到一个行业", None)]
    segments = [
        ("01", "小型数据中心 / 边缘机房", CYAN,
         "精密空调、UPS、配电柜、温湿度、烟感、水浸",
         "无专职值守；告警只报数值，不解释原因",
         "本地规则 + AI 解释 + 本机确认"),
        ("02", "连锁网点 / 门店", BLUE,
         "空调、配电箱、UPS、门磁、温湿度",
         "点位分散；远程只看不改，配置不敢开放",
         "4 条上行主题 + 权限矩阵 + 候选确认"),
        ("03", "基站 / 一体化机房", AMBER,
         "空调、电源、蓄电池、烟感、水浸",
         "高温、断电、水浸；网络不稳定",
         "断网本地继续 + 8 条告警队列 + 双槽 OTA"),
        ("04", "园区配电间 / 物业", PURPLE,
         "多品牌电表、配电柜、温湿度、水浸",
         "品牌多、点表难配；只抄表不分析",
         "点表候选 + 试读 / 确认 + 日报"),
    ]
    for i, (idx, name, color, devices, pain, entry) in enumerate(segments):
        x = 180 + (i % 2) * 1130
        y = 384 + (i // 2) * 280
        body.append(group(f"seg-{i}", [
            rect(x, y, 1070, 260, fill="#FFFFFF", stroke=LINE, rx=16),
            rect(x, y + 12, 8, 236, fill=color, rx=4),
            text(x + 28, y + 44, idx, 22, color, weight="bold", font=MONO),
            text(x + 28, y + 88, name, 30, INK, weight="bold"),
            text(x + 28, y + 134, "设备", 17, MUTED, weight="bold", ls=1),
            text(x + 120, y + 134, devices, 21, BODY),
            text(x + 28, y + 178, "痛点", 17, MUTED, weight="bold", ls=1),
            text(x + 120, y + 178, pain, 21, BODY),
            text(x + 28, y + 222, "切入点", 17, MUTED, weight="bold", ls=1),
            text(x + 120, y + 222, entry, 21, INK),
        ], bounds=f"{x} {y} 1070 260"))
    body.append(group("seg-band", [
        rect(180, 960, 2200, 160, fill="#0F172A", rx=16),
        text(216, 1018, "四个场景的共同点：设备少、无人值守、网络不稳、告警要能解释。", 28, "#E2E8F0"),
        text(216, 1066, "单网关 32 点上限 · 台架 14 点 / 7 个从站", 22, "#94A3B8"),
        line(1500, 990, 1500, 1090, "#1E293B", 2),
        text(1540, 1024, "同一套固件", 30, "#FFFFFF", weight="bold"),
        text(1540, 1064, "换一份点表", 30, "#FFFFFF", weight="bold"),
    ], bounds="180 960 2200 160"))
    return page_shell("".join(body), number=13)


def p13_perf():
    body = [header("13 / 数据", "把耗时和资源摊开", None)]
    # 左上：告警解释分解（单条堆叠条）
    body.append(group("p-alarm", [
        rect(180, 384, 1060, 340, fill=PANEL, stroke=LINE, rx=18),
        text(212, 440, "告警解释 41.83 s 的构成", 28, INK, weight="bold"),
        text(212, 476, "2026-10-25 板端单样本，8 条合计", 21, MUTED),
        rect(212, 512, 884, 56, fill="#A855F7", rx=10),
        rect(1100, 512, 40, 56, fill=CYAN, rx=10),
        text(212, 632, "模型推理", 22, MUTED),
        text(212, 676, "39.89 s", 36, "#6B21A8", weight="bold", font=MONO),
        text(212, 706, "95%", 20, MUTED),
        text(560, 632, "工具取数", 22, MUTED),
        text(560, 676, "1.96 s", 36, "#0E7490", weight="bold", font=MONO),
        text(560, 706, "5% · 读快照", 20, MUTED),
        text(1208, 632, "单条平均", 22, MUTED, anchor="end"),
        text(1208, 676, "5.23 s", 36, INK, weight="bold", anchor="end", font=MONO),
        text(1208, 706, "2 次工具调用", 20, MUTED, anchor="end"),
    ], bounds="180 384 1060 340"))
    # 右上：Modbus 四档（带标签列）
    body.append(group("p-modbus", [
        rect(1280, 384, 1100, 340, fill=PANEL, stroke=LINE, rx=18),
        text(1312, 440, "四档回复延迟下的请求数", 28, INK, weight="bold"),
        text(1312, 476, "2026-10-18 板端台架，14 点位全部在线", 21, MUTED),
        "".join(
            text(1420, 517 + i * 48, label, 22, MUTED, anchor="end", font=MONO) +
            rect(1440, 496 + i * 48, int(700 * v / 649), 30, fill="#93C5FD", rx=8) +
            text(1450 + int(700 * v / 649), 517 + i * 48, f"{v}", 22, INK, font=MONO, weight="bold")
            for i, (label, v) in enumerate([("0 ms", 649), ("5 ms", 525), ("20 ms", 471), ("150 ms", 454)])
        ),
        text(1312, 704, "每档请求全部成功，无一失败", 21, FAINT),
    ], bounds="1280 384 1100 340"))
    # 左下：语音首播（横向条 + 30 秒预算线）
    body.append(group("p-voice", [
        rect(180, 760, 1060, 460, fill=PANEL, stroke=LINE, rx=18),
        text(212, 816, "语音首播中位耗时", 28, INK, weight="bold"),
        text(212, 852, "30 条合成语音固定矩阵，28/30 在 30 秒内完成", 21, MUTED),
        line(820, 890, 820, 1130, "#CBD5E1", 2, dash="6 8"),
        text(826, 886, "30 s 预算", 20, FAINT),
        "".join(
            text(212, 930 + i * 76, label, 23, MUTED) +
            rect(320, 906 + i * 76, int(500 * v / 30), 32, fill="#67E8F9", rx=8) +
            text(1208, 930 + i * 76, f"{v:.1f} s", 24, "#0E7490", weight="bold", anchor="end", font=MONO)
            for i, (label, v) in enumerate([("异常", 18.4), ("点位", 26.2), ("恢复", 23.7)])
        ),
        text(212, 1184, "超时条目计入失败；文字正确 29/30。", 21, FAINT),
    ], bounds="180 760 1060 460"))
    # 右下：资源
    body.append(group("p-res", [
        rect(1280, 760, 1100, 460, fill="#0F172A", rx=18),
        text(1312, 816, "构建与运行资源", 26, "#E2E8F0", weight="bold"),
        text(1312, 896, "1.13%", 46, "#67E8F9", weight="bold", font=MONO),
        text(1600, 896, "Flash 占用 1,512,456 B", 22, "#94A3B8"),
        text(1312, 986, "69.22%", 46, "#FBBF24", weight="bold", font=MONO),
        text(1660, 986, "静态 SRAM 362,932 B", 22, "#94A3B8"),
        text(1312, 1076, "438 → 24", 34, "#FFFFFF", weight="bold", font=MONO),
        text(1640, 1076, "60 秒窗口内的显示提交次数", 22, "#94A3B8"),
        text(1312, 1160, "2026-10-25 构建值；显示提交来自 2026-10-28 板测", 20, "#64748B"),
    ], bounds="1280 760 1100 460"))
    return page_shell("".join(body), number=14)


def p15_next():
    body = [header("14 / 进度", "初赛之后，从单机走向平台", None)]
    added = [
        ("10-17", "稳定性", ["模组、时区、文件系统", "与看门狗的一轮修复"]),
        ("10-23", "语音链路", ["按键录音、云侧识别与播报", "30 条固定矩阵"]),
        ("10-28", "双槽与回滚", ["BCB、片内 stub", "B 槽自检与失败回退"]),
        ("10-29", "OTA 控制面", ["MQTTS 通知与回执", "HTTPS 下载、Range 续传"]),
        ("11-05", "手机手册接入", ["PDF 上传、云端解析", "MQTTS 下发、本地确认生效"]),
    ]
    body.append(group("added-head", [
        text(180, 436, "初赛后新增", 26, INK, weight="bold"),
    ], bounds="180 408 400 44"))
    body.append(line(200, 552, 1960, 552, "#CBD5E1", 3))
    for i, (date, head, desc) in enumerate(added):
        x = 200 + i * 440
        body.append(group(f"added-{i}", [
            f'<circle cx="{x}" cy="552" r="9" fill="{BLUE}" />',
            text(x + 24, 516, date, 22, MUTED, font=MONO),
            text(x, 618, head, 27, INK, weight="bold"),
            tblock(x, 660, desc, 21, 32, MUTED),
        ], bounds=f"{x} 492 420 220"))
    body.append(group("next-head", [
        text(180, 780, "下一步", 26, INK, weight="bold"),
    ], bounds="180 752 400 44"))
    for i, (num, head, desc, tag, tagc) in enumerate([
        ("1", "多网关集群与远程运维", ["一台看板管多站", "设备身份、批量点表、批量升级"], "平台", BLUE),
        ("2", "协议与设备生态", ["Modbus TCP、BACnet、SNMP", "空调、电表、UPS、配电模板"], "生态", PURPLE),
        ("3", "预测性维护与能效优化", ["从告警解释走到趋势预测", "故障前移与能耗基线"], "智能", GREEN),
        ("4", "行业方案与交付模板", ["语音巡检、点表模板", "伙伴可复制的行业方案"], "行业", AMBER),
    ]):
        x = 180 + i * 556
        body.append(group(f"next-{i}", [
            card(x, 812, 532, 376, fill=PANEL),
            text(x + 40, 900, num, 44, PURPLE, weight="bold", font=MONO),
            text(x + 40, 966, head, 28, INK, weight="bold"),
            tblock(x + 40, 1010, desc, 21, 32, MUTED),
            chip(x + 40, 1100, 104, 44, tag, tagc, size=20),
        ], bounds=f"{x} 812 532 376"))
    return page_shell("".join(body), number=15)


def p16_thanks():
    body = []
    body.append(
        '<g id="closing-art" data-pptx-object="picture" data-pptx-frame="0 0 2560 1440">'
        '<svg data-pptx-object="picture" data-pptx-frame="0 0 2560 1440" x="0" y="0" width="2560" height="1440" '
        'viewBox="0.06481 0 0.87038 1" preserveAspectRatio="none" overflow="hidden">'
        '<image href="../images/image3.jpeg" x="0" y="0" width="1" height="1" preserveAspectRatio="none" /></svg></g>'
        '<g id="closing-scrim" data-pptx-object="shape" data-pptx-frame="0 0 2560 1440" '
        'data-pptx-prst="roundRect" data-pptx-av-adj="val 0" data-pptx-semantic-object="shape">'
        '<path d="M 0 0 L 2560 0 L 2560 1440 L 0 1440 Z" fill="#000000" fill-opacity="0.62" stroke-width="2.66667" '
        'stroke-linecap="butt" stroke="none" stroke-linejoin="round" data-pptx-part="geometry" /></g>'
    )
    body.append(group("thanks", [
        text(1280, 660, "谢谢", 200, "#FFFFFF", weight="bold", anchor="middle"),
        rect(1110, 740, 340, 8, fill="url(#ggrad1)", rx=4),
        text(1280, 860, "本地持续值守，AI 依据真实数据解释", 40, "#E2E8F0", anchor="middle"),
        text(1280, 950, "VelaGuard　Team Falcons　叶培林", 26, "#94A3B8", anchor="middle"),
        text(1280, 1000, "github.com/open-vela/contest2026_004_TeamFalcons", 24, "#CBD5E1", anchor="middle", font=MONO),
    ], bounds="700 500 1160 540"))
    return page_shell_closing("".join(body))


def page_shell_closing(body):
    return page_shell(body, number=None)


PAGES = [p01_cover, p02_video, p02_day, p03_arch, p04_pipeline, p05_interfaces, p06_agent,
         p07_guard, p08_sequence, p09_offline, p10_hard, p11_ota, p12_industry, p13_perf,
         p15_next, p16_thanks]


def main():
    os.makedirs(OUT, exist_ok=True)
    for i, fn in enumerate(PAGES, 1):
        svg = fn()
        p = os.path.join(OUT, f"slide_{i:02d}.svg")
        with open(p, "w", encoding="utf-8", newline="\n") as f:
            f.write(svg)
        print(f"wrote {p} ({len(svg)} bytes)")


if __name__ == "__main__":
    main()
