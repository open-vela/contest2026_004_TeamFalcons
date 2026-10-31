#!/usr/bin/env python3
"""对已交付答辩稿的 round-trip 副本润色，保留模板及对象标识。"""

from __future__ import annotations

import argparse
import json
import shutil
import xml.etree.ElementTree as ET
from pathlib import Path

from build_deck import group, remove, replace_text

HERE = Path(__file__).resolve().parent
NS = "http://www.w3.org/2000/svg"
ET.register_namespace("", NS)
T = lambda name: f"{{{NS}}}{name}"

# 每个键对应原答辩稿中的对象；None 删除该处说明，列表保留显式换行。
COPY = {
    2: {
        "shape-3": "现场巡检，需要及时掌握设备状态",
        "shape-8": "现场监测台架实拍",
        "shape-10": "现场人员的巡检任务",
        "shape-12": ["集中查看", "多台设备的读数"],
        "shape-14": ["发现异常后", "核对点位与触发阈值"],
        "shape-16": ["交接与复盘时", "查看告警和运行记录"],
        "shape-17": None,
    },
    3: {
        "shape-3": "一次巡检的使用流程",
        "shape-13": "定位异常点位",
        "shape-14": "告警等级、触发阈值",
        "shape-19": "异常说明、数据依据",
        "shape-24": "运行统计、告警记录",
        "shape-28": None,
        "shape-56": None,
    },
    4: {
        "shape-3": "openvela 网关的系统架构",
        "shape-8": "本地监测",
        "shape-9": ["Modbus 采集、规则告警", "LVGL 显示与运行记录"],
        "shape-11": "板载 Agent",
        "shape-12": ["ReAct 调度与自定义 Skill", "查询数据、生成解释与日报"],
        "shape-15": ["现场数据", "主站轮询"],
        "shape-18": ["HTTPS", "模型推理"],
        "shape-21": ["状态与遥测", "告警记录"],
        "shape-24": ["配置与事件", "解释与报告"],
        "shape-43": None,
    },
    5: {
        "shape-3": "在设备上定位告警，查看数值变化",
        "shape-6": "快速定位异常",
        "shape-8": ["查看实时读数与单位", "核对告警点位和阈值", "区分预警、严重与离线", "打开趋势查看数值变化"],
        "shape-10": "告警详情",
        "shape-24": "界面示例（模拟数据）",
        "shape-26": None,
    },
    6: {
        "shape-3": "告警自动解释，运行情况按日汇总",
        "shape-8": "自动触发任务",
        "shape-9": "告警变化、每日日报",
        "shape-13": "读取任务 Skill",
        "shape-14": "告警解释、运行报告",
        "shape-18": "查询运行数据",
        "shape-19": "点位、统计、时间",
        "shape-23": "展示解释与日报",
        "shape-24": "异常说明、运行摘要",
        "shape-28": "板测单次样本：8 条告警解释 41.83 秒，日报生成 194 秒",
        "shape-56": "自定义 Skill：告警解释与运行日报",
        "shape-58": None,
    },
    7: {
        "shape-3": "设备接入：核对读数后确认配置",
        "shape-6": None,
    },
    8: {
        "shape-3": "按键提问，查看回答并收听摘要",
        "shape-8": "网关扩展板实拍",
        "shape-10": "语音查询设备状态",
        "shape-12": ["按住按键", "直接说出问题"],
        "shape-14": ["查看点位读数", "了解设备在线情况"],
        "shape-16": ["阅读屏幕回答", "收听语音摘要"],
        "shape-17": "10-25 真人按键测试：完成录音、查询与播放",
    },
    9: {
        "shape-3": "采集兼容与显示优化",
        "shape-8": ["调整 RS485 方向释放时序", "0 ms 回复：649 / 649 次成功", "另测 5 / 20 / 150 ms 回复", "四档启动错误计数均为 0"],
        "shape-12": ["减少语音浮层重复重绘", "两组各 60 秒板测", "显示提交：438 / 454 次", "优化后：24 / 46 次", "FIFO 欠载：各 1 次降为 0"],
        "shape-13": None,
    },
    10: {
        "shape-20": "真机演示：采集、告警、解释与断网监测",
        "shape-5": "原型实拍",
    },
    11: {
        "shape-3": "初赛后更新与下一步计划",
        "shape-7": "采集与显示",
        "shape-8": ["快速从站兼容、语音浮层优化", "界面检查 6 / 6 通过"],
        "shape-11": "板端语音",
        "shape-12": ["按键提问、状态查询、语音播报", "已完成真人操作链路验证"],
        "shape-15": "OTA 进展",
        "shape-16": ["测试 CA 下下载、续传到待确认", "安装与回滚已分别完成板测"],
        "shape-19": "后续计划",
        "shape-20": ["整机稳定性与 OTA 全流程验证", "手机手册接入和语音巡检"],
        "shape-21": None,
    },
    13: {
        "shape-3": "验证结果",
        "shape-6": None,
    },
    14: {
        "shape-3": "设备接入方式",
        "shape-6": "现行接入流程",
        "shape-8": ["录入点位参数", "试读并核对返回值", "查看本次配置变更", "确认生效", "返回首页查看采集状态"],
        "shape-10": "手机接入规划",
        "shape-12": ["上传 PDF 手册", "填写从站地址", "查看识别结果", "在网关确认添加", "查看采集状态"],
        "shape-24": None,
    },
    15: {
        "shape-3": "固件升级进展",
        "shape-8": "接收升级通知",
        "shape-9": "MQTTS 板测通过",
        "shape-13": "下载固件",
        "shape-14": "HTTPS 下载与续传通过",
        "shape-18": "查看版本并确认",
        "shape-19": "签名任务确认待联调",
        "shape-23": "重启安装",
        "shape-24": "安装与回滚分别板测",
        "shape-28": "测试环境：RJ45 网络、测试 CA、eMMC 暂存",
        "shape-56": ["10-29：1,553,704 B 镜像下载至待确认，完成续传与证书拒绝测试", "下一步：串联签名任务、确认安装、自检与结果上报"],
    },
    16: {
        "shape-3": "AI 与 openvela 的作用",
        "shape-7": "AI 解释异常、整理信息",
        "shape-8": ["汇总告警上下文", "按需查询点位与运行统计", "生成异常说明", "整理运行日报"],
        "shape-11": "openvela 组织设备端运行",
        "shape-12": ["LVGL 图形与触摸交互", "ai_agent 的 ReAct 与 Skill", "任务与网络协作", "文件系统保存配置和报告"],
    },
}

TABLES = {
    7: [
        ["步骤", "核对内容", "完成操作"],
        ["连接设备", "从站地址与通信状态", "打开点表"],
        ["录入点位", "名称、地址、单位、阈值", "保存候选"],
        ["试读核对", "返回读数与设备读数", "查看结果"],
        ["查看变更", "本次新增或修改的点位", "确认生效"],
        ["回到首页", "实时读数与在线状态", "开始值守"],
    ],
    13: [
        ["验证项目", "测试条件", "观察结果"],
        ["采集", "0 ms 回复，649 次", "649 次成功"],
        ["告警解释", "10-25，8 条告警单样本", "41.83 秒完成并加载"],
        ["语音", "10-25，按键人声单样本", "完成查询与播放"],
        ["界面", "10-31，主机无头测试", "6 / 6 项通过"],
        ["显示", "两组各 60 秒语音窗口", "FIFO 欠载各 1 次降为 0"],
    ],
}


def update_table(root, values):
    g = next(g for g in root.iter(T("g")) if g.get("data-pptx-replace-with") == "table")
    metadata = g.find(T("metadata"))
    payload = json.loads(metadata.text)
    assert len(values) == len(payload["rows"])
    for row, new_row in zip(payload["rows"], values):
        assert len(row) == len(new_row)
        for cell, value in zip(row, new_row):
            runs = cell["paragraphs"][0]["runs"]
            runs[0]["text"] = value
            cell["paragraphs"][0]["runs"] = runs[:1]
            cell["paragraphs"] = cell["paragraphs"][:1]
    payload["style"]["font_family"] = "MiSans"
    metadata.text = json.dumps(payload, ensure_ascii=False, separators=(",", ":"))
    # JSON 是原生表格的内容来源。刷新同组预览，保持 SVG 审阅内容同步。
    for child in list(g):
        if child is not metadata:
            g.remove(child)
    x, y, w, h = (payload[k] for k in ("x", "y", "width", "height"))
    widths = [w * 0.2, w * 0.4, w * 0.4]
    row_h = h / len(values)
    font = payload["defaults"]["run"]["font_size"]
    for r, row in enumerate(values):
        left = x
        for c, text in enumerate(row):
            fill = ["#111827", "#0891B2", "#9333EA"][c] if r == 0 else ("#F1F5F9" if r % 2 else "#FFFFFF")
            ET.SubElement(g, T("rect"), x=str(left), y=str(y+r*row_h), width=str(widths[c]), height=str(row_h),
                          fill=fill, stroke="#CBD5E1", **{"stroke-width": "1"})
            ET.SubElement(g, T("text"), x=str(left+widths[c]/2), y=str(y+r*row_h+row_h/2+font*.32),
                          **{"text-anchor": "middle", "font-size": str(font),
                             "fill": "#FFFFFF" if r == 0 else "#334155",
                             "font-weight": "bold" if r == 0 or c == 0 else "normal"}).text = text
            left += widths[c]


def prune_empty_groups(root):
    """删除移除说明文本后留下的原生空组。"""
    changed = True
    while changed:
        changed = False
        for parent in list(root.iter()):
            for child in list(parent):
                if child.tag == T("g") and len(child) == 0:
                    parent.remove(child)
                    changed = True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("workspace", type=Path)
    args = parser.parse_args()
    root_dir = args.workspace.resolve()
    content = json.loads((HERE / "content.json").read_text(encoding="utf-8"))
    for n, page in enumerate(content["pages"], 1):
        file = root_dir / "authoring-svg-flat" / f"slide_{n:02}.svg"
        tree = ET.parse(file)
        root = tree.getroot()
        for ident, text in COPY.get(n, {}).items():
            if text is None:
                remove(root, ident)
            else:
                replace_text(root, ident, text)
        if n in TABLES:
            update_table(root, TABLES[n])
        if n == 5:
            # 规则告警页使用读数与阈值一致的当前夹具画面。
            image = next(group(root, "shape-23").iter(T("image")))
            image.set("href", "../images/polish-alarm.png")
            shutil.copyfile(HERE / "assets" / "alarm-rule-current.png", root_dir / "images" / "polish-alarm.png")
        prune_empty_groups(root)
        tree.write(file, encoding="utf-8", xml_declaration=True)
        note = page["speech"] + "\n\n资料来源：\n" + "\n".join(page["sources"])
        if page.get("evidence_note"):
            note += "\n\n测试条件与进度：\n" + page["evidence_note"]
        (root_dir / "notes" / f"slide_{n:02}.md").write_text(note+"\n", encoding="utf-8", newline="\n")
    print("已润色 16 页及备注，保留模板与页面顺序。")


if __name__ == "__main__":
    main()
