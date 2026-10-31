#!/usr/bin/env python3
"""将排版版重排为连续主讲叙事，并将谢谢页放到最后。"""

from __future__ import annotations

import argparse
import json
import re
import xml.etree.ElementTree as ET
from pathlib import Path

from layout_deck import text

HERE=Path(__file__).resolve().parent
NS="http://www.w3.org/2000/svg"
T=lambda n:f"{{{NS}}}{n}"
ET.register_namespace("",NS)


def main():
    parser=argparse.ArgumentParser();parser.add_argument("workspace",type=Path);args=parser.parse_args()
    root_dir=args.workspace
    content=json.loads((HERE/"content.json").read_text(encoding="utf-8"))
    plan=[]
    for output,page in enumerate(content["pages"],1):
        source=page["input_slide"]
        path=root_dir/"authoring-svg-flat"/f"slide_{source:02}.svg"
        tree=ET.parse(path);root=tree.getroot()
        if source in [13,14,15,16]:
            text(root,"shape-2",186.67,77.33,933.33,40,"PROJECT DEFENSE · 决赛答辩",size=24)
            text(root,"shape-3",180,126,2100,120,page["title"],size=76,bold=True)
        if source==14:
            text(root,"shape-6",250,420,910,100,"联网时：查询与解释",size=60,bold=True)
            text(root,"shape-10",1380,420,930,100,"断网时：本地监测",size=60,bold=True)
            text(root,"shape-8",250,610,910,570,["实时采集与告警","按需查看 AI 解释","生成运行日报","语音查询设备状态","远程查看运行记录"],size=54,step=120)
            text(root,"shape-12",1380,610,930,570,["实时采集与告警","查看异常点位与阈值","本地趋势持续更新","查看本地统计报告","网络恢复后继续查询"],size=54,step=120)
        elif source==16:
            text(root,"shape-7",245,370,945,95,"语音识别与任务组织",size=56,bold=True)
            text(root,"shape-11",1370,370,945,95,"查数、回答与播报",size=56,bold=True)
            text(root,"shape-8",250,560,930,600,["按键录音","云侧 ASR 识别原始问题","板载 Agent 理解查询意图","选择点位或运行统计工具"],size=54,step=145)
            text(root,"shape-12",1370,560,930,600,["工具返回实时状态","模型组织屏幕回答","云侧 TTS 合成语音摘要","设备播放并结束本轮"],size=54,step=145)
            text(root,"shape-13",180,1260,2200,55,"真人按键样本：查一下A相电压，返回离线状态并完成播放（10-25）",size=32,align="middle")
        elif source==12:
            text(root,"shape-8",450,1250,1660,70,"github.com/open-vela/contest2026_004_TeamFalcons",size=40,align="middle").set("fill","#FFFFFF")
        elif source==10:
            text(root,"shape-5",0,1380,2560,55,"原型实拍",size=34,align="middle")
        # 更新原有页码，不添加新的常驻说明。
        for g in root.iter(T("g")):
            frame=g.get("data-pptx-frame")
            if not frame:continue
            x,y,w,h=map(float,frame.split())
            if x<200 and y>1330:
                for node in g.findall(T("text")):
                    if not re.fullmatch(r"(?:\d{2}|A[1-4])", "".join(node.itertext()).strip()):
                        continue
                    for child in list(node):node.remove(child)
                    node.text=f"{output:02}"
        path.write_bytes(ET.tostring(root,encoding="utf-8",xml_declaration=True))
        note=page["speech"]+"\n\n资料来源：\n"+"\n".join(page["sources"])
        if page.get("evidence_note"):note+="\n\n测试条件与进度：\n"+page["evidence_note"]
        (root_dir/"notes"/f"slide_{source:02}.md").write_text(note+"\n",encoding="utf-8",newline="\n")
        plan.append({"source_slide":source})
    (root_dir/"page_plan.json").write_text(json.dumps({"schema":"ppt-master.roundtrip-page-plan.v1","pages":plan},ensure_ascii=False,indent=2)+"\n",encoding="utf-8",newline="\n")
    print("16 页已编排为连续主讲，演示第 14 页，谢谢第 16 页。")


if __name__=="__main__":main()
