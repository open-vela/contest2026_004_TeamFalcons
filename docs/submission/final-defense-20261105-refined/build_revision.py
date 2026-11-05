"""Revise the supplied native deck inside a ppt-master round-trip workspace.

Usage: python build_revision.py <round-trip-workspace>
Import the original PPTX first; the script never changes the source package.
"""
from __future__ import annotations

import copy
import json
import shutil
import sys
from pathlib import Path
from lxml import etree as E

S = "http://www.w3.org/2000/svg"
NS = {"s": S}
INK, BODY, MUTED = "#0F172A", "#334155", "#64748B"
CYAN, PURPLE, BLUE, ORANGE, GREEN = "#0891B2", "#9333EA", "#3B6EF6", "#EA580C", "#15803D"
PALE, RULE = "#F8FAFC", "#CBD5E1"
W = Path(sys.argv[1]).resolve()
A = W / "authoring-svg-flat"
REFERENCE = W / "reference-svg"
if not REFERENCE.exists():
    REFERENCE.mkdir()
    for n in range(1, 17):
        shutil.copy2(A / f"slide_{n:02}.svg", REFERENCE)
ORIGINALS = {n: E.parse(str(REFERENCE / f"slide_{n:02}.svg")).getroot() for n in range(1, 17)}
SOURCES = {
    "config": "docs/maintenance-evidence/2026-11-05/config-mqtt-production.log:36-63；提交 e6a3491",
    "protocol": "docs/velaguard-candidate-mqtt-protocol.md:11-35；app/velaguard/vg_config_confirmation.c",
    "agent": "app/velaguard/vg_agent_seed.c；vg_agent_tools.c；vg_ai_contract.c；vg_advice.c；vg_agent_round.c",
    "advice": "docs/voice-evidence/2026-10-25/stream-rebuffer/final-accept.log:33-56",
    "rs485": "docs/maintenance-evidence/2026-10-18/defects.md：D-04；d04-fixed-0ms-slave.log；nuttx ff29d9b1a58",
    "tls": "docs/voice-evidence/2026-10-24/p1-p2-takeover/README.md；p1-p2-report.md；nuttx 45dd33ad936",
    "ptt": "docs/voice-evidence/2026-10-25/ptt-ram-fix/README.md；参赛仓 2120509",
    "voice": "docs/voice-evidence/2026-10-23/bvo14-closeout/README.md:62-96；matrix-foreground3/；10-25 运维助手改造见 ops-assistant/README.md",
    "ota_net": "docs/ota-evidence/2026-10-29/board-network.md:29-61（测试信任）",
    "ota_boot": "docs/ota-evidence/2026-10-28/t04-install.md；t05-rollback.md:48-139（诊断夹具）",
    "ota_open": "docs/ota-evidence/2026-10-29/t10-host-faults-release.md:48-50；docs/issues/ota/acceptance-todo.md",
    "memory": "docs/voice-evidence/2026-10-25/stream-rebuffer/build-final.log:12-18；docs/maintenance-evidence/2026-11-05/config-mqtt-auto-apply.log:50-65",
    "display": "docs/maintenance-evidence/2026-10-28/ltdc/README.md:17-27",
    "soak": "docs/maintenance-evidence/2026-10-18/old-soak-verdict.md；docs/maintenance-todo.md：QA-01",
    "mock": "原稿 images/image3.png、image5.png、image6.png、image7.png，真实 LVGL 页面在主机上以模拟快照渲染。来源：原稿制作归属-CLAUDE.md。",
}


def node(tag, **attrs):
    return E.Element(f"{{{S}}}{tag}", {k.replace("_", "-"): str(v) for k, v in attrs.items()})


def group(rt, ident, x, y, width, height):
    g = node("g", id=ident)
    g.set("data-pptx-bounds", f"{x} {y} {width} {height}")
    rt.append(g)
    return g


def text(g, x, y, value, size=40, color=BODY, bold=False, leading=None):
    t = node("text", x=x, y=y, font_size=size, fill=color, font_weight="bold" if bold else "normal")
    lines = value.split("\n")
    t.text = lines[0]
    for line in lines[1:]:
        span = node("tspan", x=x, dy=leading or round(size * 1.45))
        span.text = line
        t.append(span)
    g.append(t)
    return t


def rect(g, x, y, width, height, fill=PALE, stroke="none", radius=0):
    el = node("rect", x=x, y=y, width=width, height=height, fill=fill, stroke=stroke, stroke_width=2)
    if radius:
        el.set("rx", str(radius))
    g.append(el)


def line(g, x1, y1, x2, y2, color=RULE, width=2):
    g.append(node("line", x1=x1, y1=y1, x2=x2, y2=y2, stroke=color, stroke_width=width))


def replace_text(rt, old, new):
    found = False
    for t in rt.findall(".//s:text", NS):
        if "".join(t.itertext()) == old:
            for c in list(t):
                t.remove(c)
            lines = new.split("\n")
            t.text = lines[0]
            for part in lines[1:]:
                span = node("tspan", x=t.get("x"), dy=round(float(t.get("font-size", 26)) * 1.5))
                span.text = part
                t.append(span)
            found = True
    if not found:
        raise ValueError(f"Text not found: {old}")


def new_header(rt, section, title):
    rt.remove(rt.find("s:g[@id='shape-5']", NS))
    g = group(rt, "revision-header", 178, 78, 2100, 225)
    text(g, 180, 108, section, 24, CYAN, True)
    text(g, 178, 207, title, 72, INK, True)
    # Preserve the source accent's existing gradient.
    el = node("rect", x=180, y=285, width=110, height=11, rx=5, fill="url(#ggrad1)")
    g.append(el)


def page_num(rt, num):
    for g in rt.findall("s:g", NS):
        f = g.get("data-pptx-frame", "").split()
        if len(f) == 4 and float(f[1]) > 1350 and float(f[0]) < 150:
            for t in g.findall(".//s:text", NS):
                for c in list(t):
                    t.remove(c)
                t.text = f"{num:02}"


def clear_body(rt, keep=()):
    for el in list(rt):
        ident = el.get("id")
        if E.QName(el).localname in ("defs", "rect") or ident in keep or ident in ("shape-5", "revision-header"):
            continue
        f = el.get("data-pptx-frame", "").split()
        if len(f) == 4 and (float(f[1]) > 1300 or float(f[0]) > 2300 and float(f[1]) < 200):
            continue
        rt.remove(el)


def caption(rt, ident, message, y=1265, size=28):
    g = group(rt, ident, 180, y - size, 2200, size * 1.6)
    text(g, 180, y, message, size, MUTED)


def block(rt, ident, x, y, width, height, title, body, color=CYAN, size=40, bg=False):
    g = group(rt, ident, x, y, width, height)
    if bg:
        rect(g, x, y, width, height, PALE, RULE, 14)
    line(g, x, y, x, y + height, color, 5)
    text(g, x + 26, y + 53, title, 43, INK, True)
    text(g, x + 26, y + 125, body, size, BODY)
    return g


def diagram_steps(rt, ident, labels, y=390, height=245):
    # Editable process diagram, not a decorative illustration.
    g = group(rt, ident, 180, y, 2200, height)
    gap = 35
    width = (2200 - gap * (len(labels) - 1)) / len(labels)
    for i, (title, desc, color) in enumerate(labels):
        x = 180 + i * (width + gap)
        rect(g, x, y, width, height, "#FFFFFF", color, 9)
        text(g, x + 24, y + 55, f"{i+1:02}", 28, color, True)
        text(g, x + 24, y + 115, title, 40, INK, True)
        text(g, x + 24, y + 170, desc, 31, BODY)
        if i < len(labels) - 1:
            text(g, x + width + 8, y + height / 2 + 12, "›", 40, MUTED)


def footer_sources(keys):
    return [SOURCES[k] for k in keys]


PAGES = []


def save(src, stem, out, title, section, seconds, speech, sources, renderer=None):
    rt = copy.deepcopy(ORIGINALS[src])
    if renderer:
        new_header(rt, section, title)
        renderer(rt)
    page_num(rt, out)
    E.ElementTree(rt).write(str(A / f"{stem}.svg"), encoding="utf-8", xml_declaration=False)
    (W / "notes").mkdir(exist_ok=True)
    # Spoken notes contain only the actual talk. Evidence goes in a separate file.
    (W / "notes" / f"{stem}.md").write_text(speech.strip() + "\n", encoding="utf-8", newline="\n")
    PAGES.append(dict(page=out, source_slide=src, svg=f"{stem}.svg", title=title, seconds=seconds,
                      speaker_notes=speech.strip(), evidence=footer_sources(sources),
                      status="edited" if renderer else "referenced with notes"))


def scene(rt):
    replace_text(rt, "接入新电表", "接入新测点")
    replace_text(rt, "从站地址 3，保持寄存器 0x0000，倍率 0.1，逐项写进候选表", "网页上传手册和地址\n选择测点并下发")
    replace_text(rt, "候选 → 试读 → 人工确认", "校验后本机确认")
    replace_text(rt, "确认原因", "核对依据")
    replace_text(rt, "需要区分空调故障、采样异常还是通信中断", "先核对读数、新鲜度\n与通信状态")
    replace_text(rt, "当前 83.2 ℃，阈值 80.0 ℃，空调仍在制冷", "当前 83.2 ℃，阈值 80.0 ℃")
    caption(rt, "scene-source", "示例时间线；右图为真实 LVGL 页面的模拟快照。当前候选接入已完成 15 点联调。", size=29)


def architecture(rt):
    clear_body(rt)
    diagram_steps(rt, "system-layers", [
        ("现场仪表", "RS485 / Modbus RTU\n单主站，固定 9600", CYAN),
        ("openvela 网关", "采集、规则、LVGL\n板载 ai_agent", BLUE),
        ("网络服务", "MiMo HTTPS 推理\nMQTTS 候选接入", PURPLE),
    ], height=265)
    block(rt, "local-loop", 180, 735, 1060, 350, "本地持续运行", "周期采集与规则告警\n屏幕、LED 与事件记录\n已确认点表和安全默认态", CYAN, 43)
    block(rt, "network-work", 1320, 735, 1060, 350, "联网增加的能力", "Agent 解释与运行日报\n只读看板与受控配置接入\nOTA 下载及状态上报", PURPLE, 43)
    caption(rt, "openvela-capabilities", "openvela 能力落点：LVGL 图形、ai_agent / ReAct、网络与存储；线程参数见备用页 A。", size=30)


def acquisition(rt):
    clear_body(rt)
    diagram_steps(rt, "acq-flow", [
        ("RS485", "9600 8N1\nCRC / DIR 时序", CYAN),
        ("周期采集", "已确认点表\n超时与重试", BLUE),
        ("规则判定", "阈值与离线\n基于真实样本", PURPLE),
        ("本地告警", "屏幕 / LED\n事件落盘", ORANGE),
    ])
    block(rt, "nsh-entry", 180, 735, 1060, 325, "维护入口：NSH", "编辑候选，运行 test\n人查看结果后单独确认\napply --confirm 才改变运行表", CYAN, 40)
    block(rt, "web-entry", 1320, 735, 1060, 325, "新增入口：网页 / MQTTS", "下发候选，板端确定性校验\n屏幕人工确认后直接添加\n随后正常采集，不做试读", PURPLE, 40)
    caption(rt, "acq-evidence", "台架：14 点；0 ms 从站回复，修复后 649 / 649 次请求成功（2026-10-18）。", size=30)


def onboarding(rt):
    clear_body(rt)
    diagram_steps(rt, "onboarding-flow", [
        ("上传手册", "PDF + 从机地址\n手机 / 电脑网页", CYAN),
        ("选择测点", "型号与原文依据\n云侧候选解析", BLUE),
        ("安全下发", "独立 MQTTS\n设备鉴权与 ACL", PURPLE),
        ("本机确认", "确定性校验\n真人触屏添加", ORANGE),
    ], height=265)
    g = group(rt, "onboarding-result", 180, 735, 2200, 360)
    text(g, 180, 790, "2026-11-05 联调结果", 40, CYAN, True)
    text(g, 180, 910, "14 → 15 点", 88, INK, True)
    text(g, 180, 990, "屏幕确认后生效，重启保留", 39, BODY)
    line(g, 1035, 805, 1035, 1055)
    text(g, 1120, 857, "新增湿度点：FC04 / 寄存器 1 / 倍率 0.1", 36, BODY)
    text(g, 1120, 930, "板端与看板均采集到 50.0 %RH", 42, INK, True)
    text(g, 1120, 1005, "读数来自模拟从站；配置生效与在线分别核对", 31, MUTED)
    caption(rt, "onboarding-rule", "模型生成候选；板端校验与本地人工确认决定生效。MQTT 不提供远程确认。", size=31)


def agent(rt):
    clear_body(rt, keep=("shape-54",))
    diagram_steps(rt, "agent-loop", [
        ("主动触发", "告警集合变化\n当日缺少日报", ORANGE),
        ("加载 Skill", "解释 / 运行报告\n约定证据与格式", PURPLE),
        ("ReAct 取证", "MiMo HTTPS\n只读工具读快照", BLUE),
    ], y=390, height=245)
    # Keep the source mock conversation object at its original native frame.
    block(rt, "advice-output", 180, 735, 1180, 330, "产物经板端校验后显示", "告警解释：alarm_advice.txt，VGADV1\n日报：daily-<日期>.md\n校验启动标识、请求、格式与有效期", PURPLE, 39)
    caption(rt, "agent-action", "执行结果是解释和报告文件；读数来自采集快照，工具查询不占 RS485 总线。", y=1190, size=31)
    caption(rt, "agent-mock-label", "右图：真实 LVGL 页面的模拟问答界面。", y=1265, size=29)


def boundary(rt):
    clear_body(rt)
    block(rt, "registration", 180, 390, 1380, 215, "注册与允许表", "五个业务数据工具只读；文件产物另受路径限制\n配置、寄存器、告警清除与输出控制无 Agent 入口", PURPLE, 38)
    block(rt, "runtime-guard", 180, 675, 1380, 215, "C 实现检查参数与调用预算", "超长查询、超量输出与越界参数直接拒绝\n每 60 秒最多 20 次，结果有长度和条数上限", CYAN, 38)
    block(rt, "audit", 180, 960, 1380, 215, "每次调用留下依据", "执行计数与脱敏审计，回答可追溯到取数\n解释标注 AI 推测，规则告警独立运行", BLUE, 38)
    g = group(rt, "human-authority", 1650, 390, 730, 785)
    rect(g, 1650, 390, 730, 785, INK, "none", 16)
    text(g, 1700, 465, "由人决定的操作", 44, "#FFFFFF", True)
    text(g, 1700, 590, "点表变更", 48, "#FFFFFF", True)
    text(g, 1700, 665, "网页候选需本机确认\nNSH 需试读后人工确认", 35, "#CBD5E1")
    text(g, 1700, 870, "每个新版本安装", 46, "#FFFFFF", True)
    text(g, 1700, 945, "仅接受板端屏幕或按键\n远程与语音不代确认", 35, "#CBD5E1")


def offline(rt):
    clear_body(rt)
    headers = ["故障", "本地行为", "联网能力", "恢复方式"]
    xs = [210, 650, 1410, 1940]
    g = group(rt, "offline-table", 180, 390, 2200, 685)
    rect(g, 180, 390, 2200, 85, INK)
    for x, label in zip(xs, headers): text(g, x, 447, label, 34, "#FFFFFF", True)
    rows = [
        ("网络全部不可达", "采集、规则、显示与事件落盘继续", "解释、日报与上报暂停", "链路恢复后重连"),
        ("Broker 断开", "告警队列暂存 8 条，满则丢最旧", "看板停止更新", "重连补发，断电不补"),
        ("MiMo 超时", "保留本地规则和真实读数", "本轮 AI 产物失败", "后续轮次重新触发"),
        ("Modbus 从站离线", "最近 8 样本中失败 5 次判离线", "联网时可上报告警", "成功采样即恢复"),
    ]
    for i, row in enumerate(rows):
        y = 475 + 145 * i
        rect(g, 180, y, 2200, 145, PALE if i % 2 == 0 else "#FFFFFF")
        for x, value in zip(xs, row): text(g, x, y + 82, value, 31 if x > 1000 else 33, INK if x == 210 else BODY, x == 210)
    caption(rt, "offline-demo-condition", "断网演示：先确认 Wi-Fi 备链路未接管，再拔 RJ45；镜头中保留连续采样时间。", y=1200, size=31)


def engineering(rt):
    clear_body(rt)
    block(rt, "rs485-fix", 180, 390, 2200, 330, "RS485：回复到达时，方向引脚还没释放", "根因：发送完成后的轮询把接收切换推迟约 13 ms\n修复：在 TX 完成中断释放 DIR，移除接收前多余清理\n同为 0 ms 回复从站，成功请求从 38 / 274 提升到 649 / 649", CYAN, 42)
    block(rt, "tls-fix", 180, 815, 2200, 330, "TLS：验签错误追到了 SDRAM 数据复制", "根因：捕获到 ServerHello 随机数复制时发生非对齐读取\n修复：禁止生成该路径的非对齐访问指令\n复测：4 轮共 155 次探测，未再出现 -0x4380", PURPLE, 42)
    caption(rt, "engineering-scope", "证据：2026-10-18 RS485 台架、2026-10-24 TLS 取证；完整根因与提交见备用页 D。", size=29)


def ota(rt):
    clear_body(rt)
    diagram_steps(rt, "ota-main-flow", [
        ("通知与下载", "MQTTS 通知\nRJ45 / HTTPS", CYAN),
        ("校验与暂存", "签名 / SHA-256\n现有 eMMC FAT", BLUE),
        ("本机确认", "每个版本明确确认\nREADY 后才可安装", ORANGE),
        ("安装与恢复", "stub 安装 B 槽\n自检失败回出厂 A", PURPLE),
    ], height=270)
    block(rt, "ota-network-evidence", 180, 770, 690, 320, "下载已板验", "测试 CA / 签名密钥\n流式下载到 READY\n无确认不写 pending", CYAN, 36)
    block(rt, "ota-install-evidence", 930, 770, 690, 320, "安装回滚已板验", "诊断夹具暂存镜像\n本机确认后安装 B\n自检 / 看门狗回 A", PURPLE, 36)
    block(rt, "ota-next", 1680, 770, 700, 320, "完整任务待验收", "生产签名任务闭环\n真实断电与循环升级\n采集、AI 并发长测", ORANGE, 36)
    caption(rt, "ota-isolation", "QSPI XIP 运行期间不擦写执行区域；重启后由片内 boot stub 安装，回滚目标固定为出厂 A 槽。", size=29)


def metrics(rt):
    clear_body(rt)
    entries = [
        (180, 390, "采集成功率", "649 / 649", "0 ms 回复从站，14 点台架\n2026-10-18；该轮全部成功", CYAN),
        (1320, 390, "一次告警解释轮次", "41.83 s", "单样本批量生成 8 条解释\n2026-10-25；含工具与文件写入", PURPLE),
        (180, 825, "候选接入闭环", "14 → 15 点", "真人本机确认，重启后保留\n2026-11-05；模拟湿度值 50.0 %RH", BLUE),
        (1320, 825, "历史语音固定矩阵", "28 / 30", "首播 ≤30 s 且完整播放\n2026-10-24；旧版固定问句路径", ORANGE),
    ]
    for i, (x, y, title, value, condition, color) in enumerate(entries):
        g = group(rt, f"metric-{i}", x, y, 1060, 350)
        line(g, x, y, x + 1060, y, color, 5)
        text(g, x, y + 65, title, 40, INK, True)
        text(g, x, y + 190, value, 90, color, True)
        text(g, x, y + 270, condition, 35, BODY)
    caption(rt, "metrics-note", "各项来自不同日期的专项测试；语音后续改为自然语言运维助手，该矩阵保留为历史结果。", size=29)


def video(rt):
    clear_body(rt, keep=("shape-44", "shape-47", "shape-50", "shape-53"))
    slots = [("00–08", "设备与接线全景"), ("08–20", "点位连续刷新"), ("20–34", "越限告警上屏"),
             ("34–50", "Agent 取证与解释"), ("50–60", "查看已生成的日报"), ("60–70", "本机确认新增测点"), ("70–80", "断网后采集继续")]
    g = group(rt, "video-shot-list", 180, 390, 1250, 710)
    for i, (time, title) in enumerate(slots):
        y = 433 + 91 * i
        text(g, 180, y, time, 32, ORANGE, True)
        text(g, 380, y, title, 40, INK, True)
    caption(rt, "video-mock-label", "右侧为模拟快照渲染，用于构图参考。正式答辩替换为真机录像。", y=1155, size=29)
    caption(rt, "video-status", "待录制：80 秒横屏视频；模型等待段可剪辑，字幕标出该次真实耗时。", y=1235, size=32)


def status(rt):
    clear_body(rt)
    block(rt, "verified-today", 180, 390, 1060, 650, "当前已验证的能力", "本地采集、规则告警与 LVGL\n板端 Agent 解释、日报与只读查询\n板端按键语音链路\n网页候选经 MQTTS 下发\n真人确认后新增测点，重启保留", CYAN, 39)
    block(rt, "next-delivery", 1320, 390, 1060, 650, "接下来的交付工作", "补齐真机演示视频\n新固件 24 小时连续运行\n完整签名 OTA 与断电恢复矩阵\n手机侧录音、播放与只读巡检\n以小机房为首个现场验证点", PURPLE, 39)
    g = group(rt, "product-takeaway", 180, 1120, 2200, 125)
    text(g, 180, 1195, "本地持续监测，AI 按真实数据解释，关键变更由人确认", 46, INK, True)


def tasks(rt):
    clear_body(rt)
    g = group(rt, "task-table", 180, 390, 1350, 800)
    text(g, 180, 438, "关键已有任务", 39, INK, True)
    rows = [("ai_agent", "prio 100", "32 KiB", "ReAct / HTTPS"), ("vghmi", "prio 100", "48 KiB", "LVGL / 触摸"),
            ("acq pthread", "prio 100", "8 KiB", "周期采集"), ("vgvoice", "prio 110", "4 KiB", "ASR / Agent / TTS"),
            ("vgptt", "prio 90", "4 KiB", "录音与播放投递"), ("nsh", "prio 100", "8 KiB", "维护与救援")]
    for i, row in enumerate(rows):
        y = 490 + 97 * i
        line(g, 180, y + 68, 1510, y + 68)
        for x, value in zip([180, 550, 825, 1085], row): text(g, x, y + 42, value, 31, BODY)
    block(rt, "historical-memory", 1630, 390, 750, 570, "历史构建资源", "2026-10-25 构建\n镜像 1,512,456 B\n静态 SRAM 362,932 B\nSRAM 区域 524,288 B\n静态占用 69.22%", CYAN, 35)
    caption(rt, "memory-limit", "该表列关键任务，非完整任务清单；后续已新增候选服务线程。静态 SRAM 占用不包含动态堆峰值。", y=1240, size=28)


def permissions(rt):
    clear_body(rt)
    g = group(rt, "permission-matrix", 180, 390, 2200, 750)
    xs = [210, 680, 1050, 1420, 1810, 2160]
    headers = ["操作", "NSH", "网页服务", "候选 MQTTS", "板载 Agent", "本机输入"]
    rect(g, 180, 390, 2200, 85, INK)
    for x, s in zip(xs, headers): text(g, x, 447, s, 30, "#FFFFFF", True)
    rows = [
        ["读业务数据", "已确认快照", "只读看板", "只查配置", "5 个只读工具", "显示"],
        ["生成 / 传候选", "编辑候选", "PDF + 地址", "query/open/put", "无配置入口", "预览"],
        ["提交运行表", "test 后确认", "发起下发", "不可确认", "不可提交", "确认添加"],
        ["安装新版本", "无远程确认", "无确认入口", "无确认入口", "无确认入口", "逐版本确认"],
        ["写寄存器 / 输出", "无产品入口", "无入口", "无入口", "无入口", "无产品入口"],
    ]
    for i, row in enumerate(rows):
        y = 475 + 125 * i
        rect(g, 180, y, 2200, 125, PALE if i % 2 == 0 else "#FFFFFF")
        for x, s in zip(xs, row): text(g, x, y + 75, s, 28, INK if x == 210 else BODY, x == 210)
    caption(rt, "permissions-protocol", "遥测：1883 明文；候选：独立 TLS / ACL；OTA：独立 offer 协议。手机语音巡检仍待实现。", y=1210, size=29)


def sequence(rt):
    clear_body(rt)
    diagram_steps(rt, "sequence-steps", [
        ("规则引擎", "采样事实\n告警集合变化", ORANGE),
        ("任务调度", "boot / req\nSkill + 上下文", CYAN),
        ("ai_agent", "MiMo 规划\n只读工具取证", PURPLE),
        ("文件与显示", "write_file 产物\nC 校验后上屏", GREEN),
    ], height=270)
    block(rt, "contract", 180, 770, 1060, 350, "输出契约", "告警文件采用 VGADV1 行式格式\n按 boot、req 与点位信息检查\n长度、编码、条数与有效期有界", PURPLE, 39)
    block(rt, "latency-breakdown", 1320, 770, 1060, 350, "历史轮次耗时", "LLM 39.890 s，工具阶段 1.960 s\n整轮 elapsed 41.830 s，8 条解释\n工具阶段含查询与文件写入", CYAN, 39)
    caption(rt, "sequence-latency", "2026-10-25 单次样本；分段计时包含取整差异，不据此计算单条请求响应时间。", size=29)


def bugs(rt):
    clear_body(rt)
    rows = [
        ("RS485 时序", "DIR 释放依赖 tcdrain 轮询，约晚 13 ms", "TX 完成中断切回接收", "38/274 → 649/649；nuttx ff29d9b1a58"),
        ("TLS 数据复制", "ServerHello 随机数源在 SDRAM 非对齐地址", "构建禁用非对齐访问", "4 轮 155 次无 -0x4380；nuttx 45dd33ad936"),
        ("按键录音交接", "STOP_RECORD 同时触发 START_ASR，clip 被清零", "正常停止保留 clip，取消时清理", "真人两轮 ASR 成功；参赛仓 2120509"),
    ]
    for i, (title, cause, fix, proof) in enumerate(rows):
        block(rt, f"bug-{i}", 180, 390 + 285 * i, 2200, 235, title,
              f"根因：{cause}\n修复：{fix}\n证据：{proof}", [CYAN, PURPLE, BLUE][i], 33)


def ota_detail(rt):
    clear_body(rt)
    cols = [
        (180, "控制与下载", "MQTT 3.1.1 over TLS，QoS 1\n精确 offer 主题，拒绝 retained\n仅 RJ45；HTTPS Range + If-Range\n512 B 二进制 manifest\nECDSA P-256 与 SHA-256\n暂存 /mnt/emmc/VGOTA/"),
        (930, "本机确认与安装", "每个版本由本机明确确认\nREADY 有效期 600 s\n联锁通过后写 pending BCB\n重启由片内 stub 烧写 B\nA/B 各 8 MiB\nBCB 两份，各 4096 B"),
        (1680, "试用、自检与恢复", "B 槽自检通过后 CONFIRMED\nTRIAL 看门狗约 32 s\n启动尝试计数逐次增加\n达到 3 次后，下一启动回 A\n回滚目标固定为出厂 A\n完整签名任务和断电矩阵待验"),
    ]
    for i, (x, title, body) in enumerate(cols): block(rt, f"ota-detail-{i}", x, 390, 700, 670, title, body, [CYAN, ORANGE, PURPLE][i], 31)
    caption(rt, "ota-source-scope", "网络到 READY：测试信任板验；安装 / 自检 / 回滚：诊断夹具板验。11-05 凭据联调不等于完整 OTA 验收。", y=1200, size=28)


save(1, "slide_01", 1, "VelaGuard", "", 12,
     "评委好，我是叶培林。VelaGuard 是一台独立监测网关，本地采集告警，板载 Agent 解释真实数据。", ["agent"])
save(2, "slide_02", 2, "一个小机房的一天", "01 / 现场", 32,
     "先看一个小机房的值守过程。交接班时，人员先核对点位在线状态和最近采样时间。接入新传感器时，在网页上传手册和从机地址，选择测点，再到设备屏幕确认。温度越限后，本地规则立即告警，Agent 联网读取证据给出解释，交班时再查看运行日报。这里用示例时间线组织场景，右侧是项目真实界面在模拟快照上的渲染。", ["config", "mock"], scene)
save(3, "slide_03", 3, "一台网关上的本地与联网任务", "02 / 架构", 28,
     "现场仪表通过 RS485 接到这台网关。openvela 在板上运行采集、规则和 LVGL，也运行 ai_agent 的 ReAct 循环。左侧这些任务持续在本地工作，网络服务提供告警解释、日报、看板和受控接入。两部分通过快照和有界任务交互，所以模型响应变慢或网络断开时，本地监测仍继续。具体任务栈与优先级放在备用页。", ["agent", "protocol"], architecture)
save(4, "slide_04", 4, "采集、规则与两种配置入口", "03 / 采集", 32,
     "采集线程只使用已确认点表。Modbus 读数经过本地阈值和离线规则，再上屏并写事件记录。配置有两个入口：维护用 NSH 要先试读，人在查看结果后单独提交；新网页入口不试读，候选先经板端确定性校验，再由人在屏幕确认添加。未确认的候选不进入周期采集。固定 9600 的台架上，零延迟回复从站已完成六百四十九次连续成功请求。", ["protocol", "rs485"], acquisition)
save(5, "slide_05", 5, "手册辅助接入已完成真机联调", "04 / 接入", 40,
     "这是本轮新增的闭环。用户在网页上传 PDF 和从机地址，云端给出带原文依据的候选，用户选择后主动下发。候选通过独立 MQTTS 通道到板端，C 代码校验字段、容量和版本，最后由真人在本机确认。十一月五日联调新增了一个湿度点，运行表从十四点变成十五点，板端和看板都读到百分之五十湿度，重启后配置仍保留。这次读数来自模拟从站，配置提交和实际采集分别核对。", ["config", "protocol"], onboarding)
save(6, "slide_06", 6, "Agent 主动取证并生成解释与日报", "05 / Agent", 40,
     "Agent 有两种主动任务。告警集合变化会发起解释任务，当天缺少有效日报时会发起报告任务。板端把 Skill、上下文和请求标识交给 ai_agent，MiMo 规划后调用只读工具取证，工具读取同一份采集快照，不再占用总线。Agent 将结果写成受约束的文件，板端校验启动标识、请求、格式和有效期，再交给 LVGL 显示。它的执行产物是解释和报告，告警判定继续由本地规则负责。右侧为模拟问答界面。", ["agent", "advice", "mock"], agent)
save(7, "slide_07", 7, "C 代码落实权限与调用边界", "06 / 权限", 26,
     "权限在 C 代码里落实。五个业务数据工具只读，报告写入另有路径限制；运行时检查参数、输出容量和调用频率，每次调用记录执行依据与脱敏审计。设备 Agent 没有配置提交、寄存器写入、清除告警和输出控制入口。网页新增点位与新版本安装都需要人在本机确认，语音和远程请求不能代替这个动作。", ["agent", "protocol"], boundary)
save(9, "slide_09", 8, "断网时，本地监测继续运行", "07 / 断网", 24,
     "网络全部不可达时，采集、规则、显示与事件落盘继续，解释、日报和上报暂停。Broker 断开时，告警保留在八条 RAM 队列里，满了丢最旧，重连后补发，断电不保留。MiMo 超时就保留本地规则和真实读数。演示拔网线前要先核对 Wi-Fi 备链路，确保观察到的确实是离线运行。", ["agent", "protocol"], offline)
save(10, "slide_10", 9, "两个影响真机运行的工程问题", "08 / 攻坚", 42,
     "第一个问题在 RS485 时序。应用等发送完成后才释放方向引脚，轮询让接收切换晚了约十三毫秒，快速从站的回复已经到达。把 DIR 释放移进发送完成中断后，同为零延迟从站，成功请求从三十八比二百七十四提升到六百四十九次全部成功。第二个问题是 TLS 验签间歇失败，最终追到 ServerHello 随机数复制时的 SDRAM 非对齐读取。修复编译访问方式后，四轮一百五十五次探测未再出现这一错误。", ["rs485", "tls"], engineering)
save(11, "slide_11", 10, "OTA 下载、安装与恢复的验证范围", "09 / OTA", 30,
     "OTA 通过 MQTTS 接收通知，用 RJ45 上的 HTTPS 下载到 eMMC。校验后仍需本机确认，重启由片内 stub 安装 B，自检失败回出厂 A。当前网络下载已在测试信任下到达 READY，安装与回滚完成诊断夹具板验。完整生产签名任务、真实断电和并发长测继续分别验收。", ["ota_net", "ota_boot", "ota_open", "config"], ota)
save(13, "slide_13", 11, "测试结果与各自的条件", "10 / 证据", 28,
     "这四项数据按测试条件分别看。采集是零延迟回复下六百四十九次全部成功；告警解释是一个轮次批量生成八条，共四十一点八三秒；最新候选接入由真人确认，点表十四变十五并重启保留。右下是旧版语音固定问句矩阵，三十条里二十八条满足首播预算并完整播放。后续语音已经改为自然语言运维助手，这里保留它的历史测试口径。", ["rs485", "advice", "config", "voice"], metrics)
save(14, "slide_14", 12, "80 秒演示视频位：待录制", "11 / 演示", 84,
     "下面通过八十秒录像展示真机采集、告警解释、本机确认和断网运行。", ["mock", "config", "advice"], video)
save(15, "slide_15", 13, "当前成果与现场验证计划", "12 / 落地", 26,
     "目前已把本地采集告警、板端 Agent、按键语音和网页候选接入连接起来，最新接入已完成真人确认和重启保留。接下来的交付重点是真机视频、新固件二十四小时连续运行，以及完整签名 OTA 和断电恢复。手机侧语音巡检仍待实现。我们先以小机房作为现场验证点，再根据点表和实际运维需求扩展。", ["config", "voice", "ota_open", "soak"], status)
save(16, "slide_16", 14, "谢谢", "", 6,
     "本地持续值守，AI 依据真实数据解释。我的汇报到这里，欢迎提问。", ["agent"])
save(3, "appendix_a_tasks", 15, "备用 A：关键任务与历史构建资源", "备用 / 任务与资源", 0,
     "这张表列出主要已有任务的配置。这里的资源数据来自十月二十五日构建，静态 SRAM 只反映链接时占用，动态堆峰值需要另测。后续候选接入新增了服务线程，表中六项不是完整的系统任务清单。", ["memory"], tasks)
save(5, "appendix_b_permissions", 16, "备用 B：配置与执行权限矩阵", "备用 / 接口权限", 0,
     "网页只发起候选下发，候选 MQTTS 不提供确认。运行表提交由本机人工确认，NSH 继续使用先试读后人工提交的维护流程。遥测与候选使用独立会话和权限，手机语音巡检仍待实现。Agent 的解释和报告文件写入有独立的受限路径。", ["protocol", "agent", "config"], permissions)
save(8, "slide_08", 17, "备用 C：Agent 时序与产物校验", "备用 / Agent 时序", 0,
     "Agent 的网络工具调用消息和落盘文件格式分开。告警解释落盘采用 VGADV1 行式格式，由板端 C 代码校验后显示。历史轮次的工具阶段包括查询和文件写入，因此不能把一千九百六十毫秒全算成取数耗时，也不能把整轮时间除以八当成独立请求响应时间。", ["agent", "advice"], sequence)
save(10, "appendix_d_bugs", 18, "备用 D：根因、修复与复测记录", "备用 / 故障定位", 0,
     "三项修复分别位于串口时序、编译访问方式和录音状态机。按键录音问题中，正常停止与启动识别同时发生，旧代码提前清空 clip；修复后正常停止保留录音，取消时才清理，真人两次识别成功。每项证据均保留了测试条件和对应提交。", ["rs485", "tls", "ptt"], bugs)
save(11, "appendix_e_ota", 19, "备用 E：OTA 参数与恢复条件", "备用 / OTA 机制", 0,
     "OTA 暂存使用现有 eMMC FAT。每个新版本均需本机确认，重启后才由 stub 安装 B。试用启动由看门狗保护，三次试用失败后，下一次启动返回出厂 A。网络下载、诊断夹具安装和生产完整签名任务的验证范围分别记录，生产凭据联调也不能代替完整 OTA 验收。", ["ota_net", "ota_boot", "ota_open", "config"], ota_detail)

plan = {"schema": "ppt-master.roundtrip-page-plan.v1", "pages": [{"source_slide": p["source_slide"], "svg": p["svg"]} for p in PAGES]}
(W / "page_plan.json").write_text(json.dumps(plan, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
(W / "revision_content.json").write_text(json.dumps({"date": "2026-11-05", "main_pages": 14, "appendix_pages": 5,
    "planned_seconds": sum(p["seconds"] for p in PAGES), "video_seconds": 80, "pages": PAGES}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
print(f"Authored {len(PAGES)} planned pages; main runtime {sum(p['seconds'] for p in PAGES)} seconds including 80-second pending video.")
