#!/usr/bin/env python3
"""使用系统字体重绘报告插图，写入前检查文字边界。"""

from __future__ import annotations

import argparse
import math
import os
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

WIDTH = 1800
INK = "#20262c"
MUTED = "#59636d"
LINE = "#b7c3c9"
GREEN = "#08736b"
BLUE = "#255ba4"
AMBER = "#986012"
PAPER = "#ffffff"
STILLS = Path(__file__).resolve().parent / "stills"
FONT_CANDIDATES = [
    Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts/msyh.ttc",
    Path("/mnt/c/Windows/Fonts/msyh.ttc"),
    Path("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"),
]


def font(size: int) -> ImageFont.FreeTypeFont:
    for path in FONT_CANDIDATES:
        if path.is_file():
            return ImageFont.truetype(str(path), size)
    raise RuntimeError("A Chinese system font is required; no font is bundled in the report")


def wrapped_lines(draw: ImageDraw.ImageDraw, text: str, size: int, width: int) -> list[str]:
    face = font(size)
    result = []
    for paragraph in text.split("\n"):
        line = ""
        for char in paragraph:
            if line and draw.textlength(line + char, font=face) > width:
                result.append(line)
                line = char
            else:
                line += char
        result.append(line)
    return result


def text(draw, xy, text, width, *, size=36, color=INK, height=None, leading=1.42):
    x, y = xy
    lines = wrapped_lines(draw, text, size, width)
    step = round(size * leading)
    required = step * len(lines)
    if height is not None and required > height:
        raise ValueError(f"Text does not fit ({required}>{height}): {text}")
    for line in lines:
        draw.text((x, y), line, font=font(size), fill=color)
        y += step
    return y


def node(draw, rect, title, body, *, accent=GREEN, fill=PAPER):
    x0, y0, x1, y1 = rect
    draw.rectangle(rect, fill=fill, outline=LINE, width=2)
    draw.line((x0, y0, x1, y0), fill=accent, width=5)
    y = text(draw, (x0 + 22, y0 + 18), title, x1 - x0 - 44, size=40)
    text(draw, (x0 + 22, y + 14), body, x1 - x0 - 44,
         size=34, color=MUTED, height=y1 - y - 24)


def arrow(draw, points, color=GREEN, *, dashed=False):
    for start, end in zip(points, points[1:]):
        if not dashed:
            draw.line((start, end), fill=color, width=4)
            continue
        dx, dy = end[0] - start[0], end[1] - start[1]
        length = math.hypot(dx, dy)
        for offset in range(0, max(1, round(length)), 22):
            limit = min(offset + 12, length)
            draw.line((
                (start[0] + dx * offset / length, start[1] + dy * offset / length),
                (start[0] + dx * limit / length, start[1] + dy * limit / length),
            ), fill=color, width=4)
    x0, y0 = points[-2]
    x1, y1 = points[-1]
    angle = math.atan2(y1 - y0, x1 - x0)
    tip = (x1, y1)
    wings = [
        (x1 - 17 * math.cos(angle) + sign * 8 * math.sin(angle),
         y1 - 17 * math.sin(angle) - sign * 8 * math.cos(angle))
        for sign in (-1, 1)
    ]
    draw.polygon([tip, *wings], fill=color)


def canvas(height):
    image = Image.new("RGB", (WIDTH, height), PAPER)
    return image, ImageDraw.Draw(image)


def still(name: str) -> Image.Image:
    """读取界面实拍帧。源帧由 capture_stills.py 从 .debug/ 转存，构建不依赖 .debug/。"""
    source = STILLS / f"{name}.png"
    if not source.is_file():
        raise FileNotFoundError(
            f"缺少界面帧 {source}；先在装有 Pillow 的解释器下运行 docs/submission/capture_stills.py"
        )
    image = Image.open(source).convert("RGB")
    if image.width > WIDTH:
        image = image.resize((WIDTH, round(image.height * WIDTH / image.width)), Image.LANCZOS)
    return image


def system_map():
    image, d = canvas(1050)
    text(d, (55, 25), "现场与配置", 350, size=37)
    text(d, (490, 25), "STM32H750B-DK  /  openvela", 810, size=40)
    text(d, (1360, 25), "联网与云端", 385, size=37)
    d.rectangle((465, 110, 1280, 935), fill="#f0f6f5")
    node(d, (55, 200, 390, 455), "Modbus 从站", "温度、水浸等\nRS485 / 9600", accent=GREEN)
    node(d, (490, 175, 1255, 350), "采集与本地规则", "已确认点表 · 统计 · 阈值/离线")
    node(d, (490, 425, 850, 625), "LVGL HMI", "首页、告警、趋势\n报告与从站详情")
    node(d, (895, 425, 1255, 625), "eMMC 数据", "点表与事件\n凭据和报告")
    node(d, (490, 700, 1255, 900), "ai_agent", "ReAct · 设备 Skill · 受限工具调用\n解释与查数，不决定本地告警", accent=BLUE)
    node(d, (55, 680, 390, 950), "点表配置入口", "Windows 上位机\n或串口 NSH 命令\n试读后人工确认", accent=INK)
    node(d, (1360, 175, 1745, 440), "MQTT Broker", "status / telemetry\nalarm / point_table\n当前为明文链路", accent=BLUE)
    node(d, (1360, 470, 1745, 670), "云看板", "只读订阅四类主题\nSQLite 留档与检索", accent=BLUE)
    node(d, (1360, 700, 1745, 900), "MiMo", "HTTPS 直连\n云端推理", accent=BLUE)
    arrow(d, [(390, 265), (490, 265)])
    arrow(d, [(670, 350), (670, 425)])
    arrow(d, [(1075, 350), (1075, 425)])
    arrow(d, [(1075, 625), (1075, 700)], BLUE)
    arrow(d, [(1255, 260), (1360, 260)], BLUE, dashed=True)
    arrow(d, [(1255, 795), (1360, 795)], BLUE, dashed=True)
    arrow(d, [(1552, 440), (1552, 470)], BLUE)
    arrow(d, [(390, 815), (435, 815), (435, 335), (490, 335)], INK)
    text(d, (55, 985), "实线：板内与现场链路    虚线：联网链路    云看板只读订阅，不向设备发布",
         1690, size=33, color=MUTED)
    return image


def agent_boundary():
    image, d = canvas(1160)
    text(d, (55, 20), "操作员的配置流程", 1690, size=40)
    node(d, (55, 100, 545, 280), "编辑候选表", "不改变运行中的采集表", accent=INK)
    node(d, (655, 100, 1145, 280), "试读并查看结果", "在这里等待人工判断", accent=INK)
    node(d, (1255, 100, 1745, 280), "人工确认生效", "独立发送 apply --confirm", accent=INK)
    arrow(d, [(545, 190), (655, 190)], INK)
    arrow(d, [(1145, 190), (1255, 190)], INK)
    text(d, (55, 330), "设备端 Agent  /  当前 C 实现", 1690, size=40)
    columns = [55, 475, 1270, 1745]
    d.rectangle((55, 405, 1745, 475), fill="#edf1f4")
    for x, label, width in zip(columns, ["入口", "已存在的限制", "适用边界"], [400, 775, 455]):
        text(d, (x + 20, 417), label, width - 40, size=34)
    rows = [
        ("vgpoint / vgdiscover", "run_shell 拒绝调用整个命令", "不开放配置入口"),
        ("vgcfg", "仅放行 dump 查询", "限制该命令"),
        ("文件工具", "限定数据目录；保护部分配置文件", "仍需收紧写路径"),
        ("vgstats / vgnet", "命令在允许表内", "子命令限制待补强"),
    ]
    y = 475
    for entry, rule, boundary in rows:
        d.line((55, y, 1745, y), fill=LINE, width=2)
        for x, item, width in zip(columns, [entry, rule, boundary], [420, 795, 475]):
            text(d, (x + 20, y + 25), item, width - 40, size=33, height=92)
        y += 115
    d.line((55, y, 1745, y), fill=LINE, width=2)
    text(d, (55, 985), "产品目标：只读查询、AI 推测与规则结论分开。", 1690, size=36, color=GREEN)
    text(d, (55, 1050), "允许表不等于完整只读证明；子命令限制与试读绑定待补强，见 3.7。", 1690, size=32, color=AMBER)
    return image


def data_flow():
    image, d = canvas(1545)
    text(d, (55, 20), "不同产物使用不同路径", 1690, size=40)
    x1, x2, x3 = 320, 840, 1360
    w1, w2, w3 = 410, 410, 385
    text(d, (55, 185), "本地告警\n离线可用", 220, size=39, color=GREEN)
    node(d, (x1, 155, x1 + w1, 375), "周期采集", "只读已确认点表\nModbus / RS485")
    node(d, (x2, 155, x2 + w2, 375), "规则判定", "阈值与离线\n更新事件状态")
    node(d, (x3, 155, x3 + w3, 375), "告警页", "本地规则结果\n恢复由本地判断")
    arrow(d, [(x1 + w1, 265), (x2, 265)])
    arrow(d, [(x2 + w2, 265), (x3, 265)])
    text(d, (55, 555), "告警解释\n依赖网络", 220, size=39, color=BLUE)
    node(d, (x1, 525, x1 + w1, 745), "待处理告警", "pending_alarm.txt\n后台文件任务写入", accent=BLUE)
    node(d, (x2, 525, x2 + w2, 745), "Skill + MiMo", "心跳或提问触发\n读证据、生成解释", accent=BLUE)
    node(d, (x3, 525, x3 + w3, 745), "解释文件", "last_alarm.md\n与规则判定分开", accent=BLUE)
    arrow(d, [(1045, 375), (1045, 450), (525, 450), (525, 525)], BLUE)
    arrow(d, [(x1 + w1, 635), (x2, 635)], BLUE, dashed=True)
    arrow(d, [(x2 + w2, 635), (x3, 635)], BLUE)
    text(d, (55, 905), "运行报告\n离线可用", 220, size=39, color=GREEN)
    node(d, (x1, 875, x1 + w1, 1095), "本次上电统计", "通信质量与在线时间\n异常时间线")
    node(d, (x2, 875, x2 + w2, 1095), "固件写报告", "runtime-report.md\n报告页请求时更新")
    node(d, (x3, 875, x3 + w3, 1095), "报告页", "读取文本快照\n无须调用 MiMo")
    arrow(d, [(x1 + w1, 985), (x2, 985)])
    arrow(d, [(x2 + w2, 985), (x3, 985)])
    text(d, (55, 1295), "云端留存\n需要网络", 220, size=39, color=BLUE)
    node(d, (x1, 1215, x1 + w1, 1435), "周期快照", "已确认点表与实时值\n每 30 s 取一次", accent=BLUE)
    node(d, (x2, 1215, x2 + w2, 1435), "MQTT 四主题", "status / telemetry\nalarm / point_table", accent=BLUE)
    node(d, (x3, 1215, x3 + w3, 1435), "云看板", "只读订阅\nSQLite 留档与检索", accent=BLUE)
    arrow(d, [(x1 + w1, 1325), (x2, 1325)], BLUE)
    arrow(d, [(x2 + w2, 1325), (x3, 1325)], BLUE)
    text(d, (55, 1470), "四类主题由网络管理线程发布；采集与界面线程只入队，队列满时丢弃 telemetry、保留 alarm。",
         1690, size=32, color=MUTED)
    return image


def expansion_board():
    image, d = canvas(920)
    text(d, (55, 20), "扩展板接口与接线（Arduino / STMod+ 扩展方案）", 1690, size=40)
    node(d, (55, 105, 1745, 305), "STM32H750B-DK 主板",
         "LCD 480×272 触摸 · QSPI XIP 应用 · SDRAM 帧缓冲 · eMMC 配置与报告 · RJ45 以太网",
         accent=GREEN)
    node(d, (55, 360, 700, 660), "RS485 半双工",
         "UART7：PB4/D10 发送\nPA8/D5 接收，PK1/D4 方向\n3.3 V 收发器与终端电阻", accent=GREEN)
    node(d, (760, 360, 1250, 660), "备用 Wi-Fi",
         "ESP-01S 经 USART2\nPD5 / PD6 接 STMod+\n焊桥选择与独立 3.3 V", accent=GREEN)
    node(d, (1310, 360, 1745, 660), "调试与输出",
         "ST-LINK 虚拟串口\nNSH，不占用 RS485\n低边 MOSFET 输出", accent=INK)
    text(d, (55, 700), "RS485 方向由驱动在发送完成后切回接收；帧间隔只保留 RTU 的 3.5 字符。",
         1690, size=36, color=GREEN)
    text(d, (55, 765), "接口映射已与 MB1381-B01 原理图第 14、16 页及当前 board.h 交叉核对。",
         1690, size=34, color=MUTED)
    text(d, (55, 825), "ESP-01S 备用链路的代码路径已实现；2026-09-14 板测记录中 Wi-Fi 未关联，切换行为另测。",
         1690, size=32, color=AMBER)
    return image


DRAWN = (
    ("system-map", system_map),
    ("agent-boundary", agent_boundary),
    ("data-flow", data_flow),
    ("expansion-board", expansion_board),
)
STILL_FIGURES = ("home-32pt", "alarm-warn3", "trend-threshold", "host-ui")
FIGURE_NAMES = tuple(name for name, _ in DRAWN) + STILL_FIGURES


def render_all(out: Path) -> list[Path]:
    out.mkdir(parents=True, exist_ok=True)
    paths = []
    for name, build in DRAWN:
        target = out / f"{name}.png"
        build().save(target, optimize=True)
        paths.append(target)
    for name in STILL_FIGURES:
        target = out / f"{name}.png"
        still(name).save(target, optimize=True)
        paths.append(target)
    return paths


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=Path(__file__).parent / "figures")
    args = parser.parse_args()
    for target in render_all(args.output_dir):
        print(target)
