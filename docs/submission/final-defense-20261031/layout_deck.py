#!/usr/bin/env python3
"""在文案润色版的 round-trip 副本中调整字号、内容比例与对齐。"""

from __future__ import annotations

import argparse
import json
import xml.etree.ElementTree as ET
from pathlib import Path

from build_deck import group

NS = "http://www.w3.org/2000/svg"
ET.register_namespace("", NS)
T = lambda name: f"{{{NS}}}{name}"


def rectangle_path(x, y, w, h, radius=0):
    if not radius:
        return f"M {x} {y} L {x+w} {y} L {x+w} {y+h} L {x} {y+h} Z"
    r = min(radius, w/2, h/2)
    return (f"M {x+r} {y} L {x+w-r} {y} Q {x+w} {y} {x+w} {y+r} "
            f"L {x+w} {y+h-r} Q {x+w} {y+h} {x+w-r} {y+h} "
            f"L {x+r} {y+h} Q {x} {y+h} {x} {y+h-r} "
            f"L {x} {y+r} Q {x} {y} {x+r} {y} Z")


def box(root, ident, x, y, w, h):
    g = group(root, ident)
    g.set("data-pptx-frame", f"{x} {y} {w} {h}")
    path = g.find(T("path"))
    if path is not None:
        kind = g.get("data-pptx-prst", "rect")
        if kind == "ellipse":
            cx, cy = x+w/2, y+h/2
            path.set("d", f"M {x} {cy} A {w/2} {h/2} 0 1 0 {x+w} {cy} A {w/2} {h/2} 0 1 0 {x} {cy} Z")
        else:
            path.set("d", rectangle_path(x,y,w,h,24 if kind == "roundRect" else 0))
    return g


def text(root, ident, x, y, w, h, lines, size=54, step=None, align=None, bold=None):
    g = box(root, ident, x,y,w,h)
    old = next(g.iter(T("text")))
    attrs = dict(old.attrib)
    for attr in ["data-pptx-text-model", "data-paragraph-line-height"]:
        attrs.pop(attr, None)
    attrs["font-size"] = str(size)
    if align is not None:
        attrs["text-anchor"] = align
    anchor = attrs.get("text-anchor", "start")
    attrs["x"] = str(x+w/2 if anchor == "middle" else x+w if anchor == "end" else x)
    attrs["y"] = str(y+size*1.05)
    if bold is not None:
        attrs["font-weight"] = "bold" if bold else "normal"
    new = ET.Element(T("text"), attrs)
    lines = [lines] if isinstance(lines,str) else lines
    new.text = lines[0]
    for line in lines[1:]:
        ET.SubElement(new,T("tspan"),x=attrs["x"],dy=str(step or size*1.5)).text=line
    parent = next(p for p in g.iter() if old in list(p))
    idx = list(parent).index(old);parent.remove(old);parent.insert(idx,new)
    return new


def rich(root, ident, x,y,w,h,rows,align="start"):
    """rows 是 (原文, 字号, 基线, 颜色, 字重)，只改变呈现层级。"""
    g=box(root,ident,x,y,w,h)
    old=next(g.iter(T("text")))
    attrs=dict(old.attrib)
    for key in ["data-pptx-text-model","data-paragraph-line-height"]:
        attrs.pop(key,None)
    anchor_x=x+w/2 if align=="middle" else x
    attrs.update({"x":str(anchor_x),"y":str(rows[0][2]),"text-anchor":align,"font-size":str(rows[0][1])})
    new=ET.Element(T("text"),attrs)
    previous=rows[0][2]
    for i,(value,size,baseline,color,bold) in enumerate(rows):
        a={"font-size":str(size),"fill":color,"font-weight":"bold" if bold else "normal"}
        a.update({"x":str(anchor_x),"dy":str(0 if i==0 else baseline-previous)})
        ET.SubElement(new,T("tspan"),a).text=value
        previous=baseline
    parent=next(p for p in g.iter() if old in list(p))
    idx=list(parent).index(old);parent.remove(old);parent.insert(idx,new)


def line(root,ident,x1,y1,x2,y2):
    g=group(root,ident)
    g.set("data-pptx-frame",f"{min(x1,x2)} {min(y1,y2)} {abs(x2-x1)} {abs(y2-y1)}")
    g.find(T("path")).set("d",f"M {x1} {y1} L {x2} {y2}")


def picture(root,ident,x,y,w,h):
    g=group(root,ident)
    for node in g.iter():
        if node.tag == T("g") and node.get("data-pptx-frame"):
            node.set("data-pptx-frame",f"{x} {y} {w} {h}")
        if node.tag == T("image"):
            node.attrib.update({"x":str(x),"y":str(y),"width":str(w),"height":str(h),"preserveAspectRatio":"xMidYMid meet"})


def flow(root,n):
    top=430 if n in [3,6] else 375
    height=610 if n==6 else 720 if n==3 else 590
    titles={
        3:["看实时状态","定位异常点位","查看 AI 解释","回顾运行报告"],
        6:["自动触发任务","读取任务 Skill","查询运行数据","展示解释与日报"],
        15:["接收升级通知","下载固件","查看版本并确认","重启安装"],
    }[n]
    captions={
        3:[["数值、在线情况"],["告警等级、","触发阈值"],["异常说明、","数据依据"],["运行统计、","告警记录"]],
        6:[["告警变化、","每日日报"],["告警解释、","运行报告"],["点位、统计、时间"],["异常说明、","运行摘要"]],
        15:[["MQTTS 板测通过"],["HTTPS 下载与","续传通过"],["签名任务确认","待联调"],["安装与回滚","分别板测"]],
    }[n]
    for i,x in enumerate([180,740,1300,1860]):
        base=5+i*5
        box(root,f"shape-{base}",x,top,480,height)
        box(root,f"shape-{base+1}",x,top,480,11)
        text(root,f"shape-{base+2}",x+32,top+60,416,130,f"{i+1:02}",size=100,align="middle")
        text(root,f"shape-{base+3}",x+26,top+(300 if n==3 else 245),428,94,titles[i],size=54,align="middle",bold=True)
        text(root,f"shape-{base+4}",x+26,top+(500 if n==3 else 390),428,170,captions[i],size=42,step=67,align="middle")
    for i,ident in enumerate(["shape-25","shape-26","shape-27"]):
        x=662+i*560
        line(root,ident,x,top+320,x+76,top+320)
    if n==6:
        text(root,"shape-56",180,328,2200,70,"自定义 Skill：告警解释与运行日报",size=42)
        text(root,"shape-28",180,1150,2200,90,"板测单次样本：8 条告警解释 41.83 秒，日报生成 194 秒",size=42,align="middle")
    if n==15:
        text(root,"shape-28",180,1030,2200,70,"测试环境：RJ45 网络、测试 CA、eMMC 暂存",size=40,align="middle")
        text(root,"shape-56",180,1140,2200,145,["10-29：1,553,704 B 镜像下载至待确认，完成续传与证书拒绝测试","下一步：串联签名任务、确认安装、自检与结果上报"],size=38,step=68)


def native_table(root):
    g=next(g for g in root.iter(T("g")) if g.get("data-pptx-replace-with")=="table")
    m=g.find(T("metadata"));data=json.loads(m.text)
    data.update({"x":180,"y":370,"width":2200,"height":870})
    data["column_widths"]=[440,880,880]
    data["row_heights"]=[120,150,150,150,150,150]
    data.setdefault("style",{})["font_family"]="MiSans"
    data["defaults"]["run"]["font_size"]=46
    for i,row in enumerate(data["rows"]):
        for cell in row:
            for paragraph in cell.get("paragraphs",[]):
                for run in paragraph.get("runs",[]):
                    run["font_size"]=48 if i==0 else 46
    m.text=json.dumps(data,ensure_ascii=False,separators=(",",":"))
    g.set("data-pptx-frame","180 370 2200 870")
    for child in list(g):
        if child is not m:g.remove(child)
    y=370
    for r,row in enumerate(data["rows"]):
        x=180;rh=data["row_heights"][r]
        for c,cell in enumerate(row):
            value="".join(run.get("text","") for p in cell.get("paragraphs",[]) for run in p.get("runs",[]))
            fill=["#111827","#0891B2","#9333EA"][c] if r==0 else ("#F1F5F9" if r%2 else "#FFFFFF")
            ET.SubElement(g,T("rect"),x=str(x),y=str(y),width=str(data["column_widths"][c]),height=str(rh),fill=fill,stroke="#CBD5E1",**{"stroke-width":"1"})
            ET.SubElement(g,T("text"),x=str(x+data["column_widths"][c]/2),y=str(y+rh/2+16),**{"text-anchor":"middle","font-size":"48" if r==0 else "46","fill":"#FFFFFF" if r==0 else "#334155","font-weight":"bold" if r==0 or c==0 else "normal"}).text=value
            x+=data["column_widths"][c]
        y+=rh


def layout(root,n):
    if n not in [1,10,12]:
        old=next(group(root,"shape-3").iter(T("text")))
        heading="".join(old.itertext())
        text(root,"shape-3",180,126,2100,120,heading,size=76,bold=True)
        box(root,"shape-4",180,285,110,11)
    if n==1:
        text(root,"shape-10",203,488,2157,250,"VelaGuard",size=208,align="middle",bold=True)
        rich(root,"shape-7",220,790,2120,210,[
            ("基于 openvela 的 AI 辅助监测网关",60,850,"#D9E5F0",False),
            ("Team Falcons   叶培林   AI 硬件产品创新",42,944,"#D9E5F0",False),
        ],align="middle")
    elif n in [2,8]:
        height=850 if n==2 else 800
        picture(root,"shape-19",220,350,height*4/3,height)
        box(root,"shape-9",1475,350,905,height)
        header="现场人员的巡检任务" if n==2 else "语音查询设备状态"
        text(root,"shape-10",1540,415,780,90,header,size=56,bold=True)
        values=([["集中查看","多台设备的读数"],["发现异常后","核对点位与触发阈值"],["交接与复盘时","查看告警和运行记录"]]
                if n==2 else [["按住按键","直接说出问题"],["查看点位读数","了解设备在线情况"],["阅读屏幕回答","收听语音摘要"]])
        for i,lines in enumerate(values):
            y=575+i*190
            box(root,f"shape-{11+i*2}",1530,y+51,20,20)
            text(root,f"shape-{12+i*2}",1590,y,740,155,lines,size=52,step=78)
        caption="现场监测台架实拍" if n==2 else "网关扩展板实拍"
        text(root,"shape-8",180,350+height+22,1220,50,caption,size=32,align="middle")
        if n==8:text(root,"shape-17",180,1240,2200,60,"10-25 真人按键测试：完成录音、查询与播放",size=36,align="middle")
    elif n in [3,6,15]:flow(root,n)
    elif n==4:
        box(root,"shape-5",180,350,320,880)
        text(root,"shape-6",195,650,290,250,["openvela","网关","STM32H750"],size=44,step=74,align="middle",bold=True)
        box(root,"shape-7",550,350,880,415);box(root,"shape-10",1470,350,910,415)
        text(root,"shape-8",610,415,770,90,"本地监测",size=58,bold=True)
        text(root,"shape-9",610,555,770,170,["Modbus 采集、规则告警","LVGL 显示与运行记录"],size=46,step=82)
        text(root,"shape-11",1530,415,790,90,"板载 Agent",size=58,bold=True)
        text(root,"shape-12",1530,555,790,170,["ReAct 调度与自定义 Skill","查询数据、生成解释与日报"],size=46,step=82)
        for i,(bg,head,body,title,lines) in enumerate([
            (13,14,15,"RS485 从站",["现场数据","主站轮询"]),
            (16,17,18,"MiMo",["HTTPS","模型推理"]),
            (19,20,21,"MQTT 留档",["状态与遥测","告警记录"]),
            (22,23,24,"eMMC",["配置与事件","解释与报告"]),
        ]):
            x=550+i*465;box(root,f"shape-{bg}",x,810,420,420)
            text(root,f"shape-{head}",x+34,865,352,90,title,size=46,bold=True)
            text(root,f"shape-{body}",x+34,1020,352,160,lines,size=42,step=72)
    elif n in [5,14]:
        box(root,"shape-5",180,350,1050,880);box(root,"shape-9",1310,350,1070,880)
        left="快速定位异常" if n==5 else "现行接入流程"
        right="告警详情" if n==5 else "手机接入规划"
        text(root,"shape-6",250,420,910,100,left,size=62,bold=True)
        text(root,"shape-10",1380,420,930,100,right,size=62,bold=True)
        line(root,"shape-7",250,555,1160,555);line(root,"shape-11",1380,555,2310,555)
        lines=(["查看实时读数与单位","核对告警点位和阈值","区分预警、严重与离线","打开趋势查看数值变化"] if n==5 else
               ["录入点位参数","试读并核对返回值","查看本次配置变更","确认生效","返回首页查看采集状态"])
        text(root,"shape-8",250,610,910,570,lines,size=54,step=120 if n==14 else 132)
        if n==5:
            picture(root,"shape-23",1355,610,980,555)
            text(root,"shape-24",1380,1180,930,50,"界面示例（模拟数据）",size=32)
        else:text(root,"shape-12",1380,610,930,570,["上传 PDF 手册","填写从站地址","查看识别结果","在网关确认添加","查看采集状态"],size=54,step=120)
    elif n in [7,13]:native_table(root)
    elif n in [9,16]:
        for i,(bg,band,head,body) in enumerate([(5,6,7,8),(9,10,11,12)]):
            x=180+i*1125
            box(root,f"shape-{bg}",x,350,1075,870);box(root,f"shape-{band}",x,350,1075,120)
            heading="".join(next(group(root,f"shape-{head}").iter(T("text"))).itertext())
            text(root,f"shape-{head}",x+65,370,945,95,heading,size=56,bold=True)
        if n==9:
            rich(root,"shape-8",250,535,930,620,[
                ("调整 RS485 方向释放时序",44,590,"#334155",False),
                ("0 ms 回复：",44,685,"#334155",False),
                ("649 / 649 次成功",86,835,"#0891B2",True),
                ("另测 5 / 20 / 150 ms 回复",44,985,"#334155",False),
                ("四档启动错误计数均为 0",44,1090,"#334155",False),
            ])
            rich(root,"shape-12",1370,535,930,620,[
                ("减少语音浮层重复重绘",44,590,"#334155",False),
                ("两组各 60 秒板测",44,685,"#334155",False),
                ("显示提交：438 / 454 次",44,795,"#334155",False),
                ("优化后：24 / 46 次",72,935,"#3B6EF6",True),
                ("FIFO 欠载：各 1 次降为 0",44,1090,"#334155",False),
            ])
        else:
            text(root,"shape-8",250,560,930,600,["汇总告警上下文","按需查询点位与运行统计","生成异常说明","整理运行日报"],size=58,step=145)
            text(root,"shape-12",1370,560,930,600,["LVGL 图形与触摸交互","ai_agent 的 ReAct 与 Skill","任务与网络协作","文件系统保存配置和报告"],size=54,step=145)
            text(root,"shape-13",180,1260,2200,55,"专属仓库：github.com/open-vela/contest2026_004_TeamFalcons",size=32,align="middle")
    elif n==10:
        text(root,"shape-20",0,1265,2560,175,"真机演示：采集、告警、解释与断网监测",size=58,align="middle")
        text(root,"shape-5",0,1380,2560,55,"原型实拍",size=34,align="middle")
    elif n==11:
        headers=["采集与显示","板端语音","OTA 进展","后续计划"]
        bodies=[["快速从站兼容、语音浮层优化","界面检查 6 / 6 通过"],["按键提问、状态查询、语音播报","已完成真人操作链路验证"],["测试 CA 下下载、续传到待确认","安装与回滚已分别完成板测"],["整机稳定性与 OTA 全流程验证","手机手册接入和语音巡检"]]
        for i in range(4):
            base=5+i*4;x=180+(i%2)*1125;y=350+(i//2)*450
            box(root,f"shape-{base}",x,y,1075,410);box(root,f"shape-{base+1}",x,y,1075,112)
            text(root,f"shape-{base+2}",x+65,y+18,945,90,headers[i],size=56,bold=True)
            text(root,f"shape-{base+3}",x+65,y+180,945,190,bodies[i],size=48,step=82)
    elif n==12:
        text(root,"shape-4",570,450,1420,280,"THANKS",size=230,align="middle")
        text(root,"shape-6",400,830,1760,100,"本地持续监测，AI 依据真实数据辅助巡检",size=58,align="middle")
        text(root,"shape-7",450,1140,1660,90,"VelaGuard   Team Falcons   叶培林",size=44,align="middle")
        text(root,"shape-8",450,1250,1660,70,"独立监测   /   告警有依据   /   运行可回顾",size=40,align="middle")


def main():
    parser=argparse.ArgumentParser();parser.add_argument("workspace",type=Path);args=parser.parse_args()
    folder=args.workspace/"authoring-svg-flat"
    for n in range(1,17):
        p=folder/f"slide_{n:02}.svg";tree=ET.parse(p)
        layout(tree.getroot(),n)
        tree.write(p,encoding="utf-8",xml_declaration=True)
    print("16 页完成字号、内容区、图片和表格排版调整，备注保持原样。")


if __name__=="__main__":main()
