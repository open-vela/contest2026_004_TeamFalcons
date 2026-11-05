#!/usr/bin/env python3
"""CHATGPT 双版本答辩：保留官方原生母版，在 round-trip 副本编排内容。"""
from __future__ import annotations
import argparse, copy, json, shutil, sys, unicodedata
from pathlib import Path
import xml.etree.ElementTree as E

HERE=Path(__file__).resolve().parent
NS='http://www.w3.org/2000/svg'
Q=lambda s:f'{{{NS}}}{s}'
E.register_namespace('',NS)
INK='#142131';MUTED='#526278';TEAL='#0891B2';BLUE='#3D6BF5';PURPLE='#9138DE'
RED='#D34B47';AMBER='#BE820C';GREEN='#23875F';LIGHT='#EEF4F8';LINE='#CBD7E3'
TIMES=[10,30,20,30,35,40,40,25,25,35,25,80,35,10]
MAPS={'scene':[1,2,3,4,6,8,10,7,5,11,13,14,15,16],
      'tech':[1,2,3,5,6,11,8,4,10,11,13,14,15,16]}
LABELS={'scene':'场景叙事版','tech':'技术深度版'}
TITLES={
'scene':['VelaGuard','把设备间监测接到独立支路','从接班到交班，沿着一次异常查下去','接入仪表后，先核对读数再投入值守','从告警详情核对阈值，再看变化过程','Agent 把告警与运行记录组织成解释','现场提问，关联查询电压和设备负载','外网中断，现场仍有读数和告警','同一组运行数据，支撑屏幕与 Agent','从快速回复到显示并发，解决真实问题','升级维护从下载延伸到启动恢复','80秒，走完一次现场巡检','从台架验证走向现场试点','谢谢'],
'tech':['VelaGuard','监测支路与控制系统各司其职','一次温度越限，触发两条处理路径','采集、显示与 Agent 在 openvela 上协作','采样进入本地规则，AI 读取业务快照','从站回复越快，为何反而更容易丢帧','主动 Agent：取证、推理、写出与加载','业务工具读快照，输出经过板端检查','一次语音查询，跨过五个执行阶段','减少重复重绘，降低显示访问竞争','QSPI XIP 决定了升级的执行阶段','80秒，验证关键用户路径','工程交付与下一轮验证重点','谢谢']}
SPEECH={
'scene':[
'各位评委老师好，我是 Team Falcons 的叶培林。VelaGuard 是运行在 openvela 上的 AI 辅助巡检网关。今天从工厂设备间的一次温度异常展开。',
'设想一个需要轮班巡检的设备间。温湿度、电参量和水浸仪表接入独立监测支路，VelaGuard 在现场集中显示，上层平台接收 MQTT 数据。原有 PLC 继续负责控制。这里是部署方式示意，右侧是我们的真实验证台架。',
'值班人员接班时先看在线状态。模拟场景中，温度由二十八点四度升到三十六点二度，核查和处理后回到二十九度。后面几页跟随这次异常，分别看现场操作和系统如何响应。',
'仪表接入时，要把通信地址、寄存器、倍率、单位和阈值对应起来。当前使用 NSH 编辑候选点表，试读后核对设备读数，再由人确认生效。进入周期采集以后，屏幕和 Agent 都读取同一份已确认点表与运行数据。',
'温度达到三十二点六度时触发预警，三十六点二度时超过严重阈值。值班人员先看点名、当前值和触发阈值，再打开趋势确认变化过程。这里使用实际界面本地渲染，所有数值来自同一组模拟采样。',
'规则已经指出温度越限，Agent 继续整理相关运行信息。告警集合变化后，设备主动发起任务，读取告警 Skill，再调用运行报告工具，生成结构化建议文件，板端检查后加载到屏幕。图中的场景回答为模拟；历史真机样本完成八条告警解释，用时四十一点八三秒。',
'到现场后，人员可以询问温度异常时电压和负载是否正常。板端 Agent 理解原始问题，自主选择只读工具，屏幕显示完整回答，设备播报摘要。模拟界面中，电压二百二十四点六伏、UPS负载百分之四十二，均在线，人员据此继续核对通风和散热。十月二十五日已有真人按键到播放结束的真机样本。',
'如果外网在这时中断，现场人员仍然能看到温度、阈值和告警。本地采集继续产生三十六点一度的新样本。图中网络标志变为断开，但采集标志和仪表在线状态保留；仪表通信失败是另一种状态，会显示缺失读数。',
'这条使用流程由三层协作：采集线程取得数据，LVGL组织读数和告警，板载 Agent 查询快照并组织解释。MiMo 通过 HTTPS 完成模型推理，MQTT负责监测数据上报，eMMC保存配置和运行产物。',
'把功能放在同一台设备上，会遇到具体工程问题。快速从站的回复曾落在方向切换窗口中，改用发送完成中断释放方向后，零毫秒工况六百四十九次全部成功。语音浮层的重复重绘也造成额外显示负载，调整为内容变化时更新后，两组提交次数明显下降。',
'现场维护还需要考虑升级失败后的恢复。网络侧用 MQTTS 通知，HTTPS将镜像暂存在现有 eMMC FAT；本机确认后，重启进入片内启动程序烧写 QSPI。下载、安装和回滚已有各自的分项证据，完整签名任务安装链路继续验证。',
'接下来用八十秒展示正常采集、越限告警、主动解释、语音查询和断网后的本地监测。当前页面为原型静帧，完成拍摄后嵌入同一份视频。',
'模拟场景最后恢复到二十九度，交接记录保留这次越限。当前完成了采集、告警、主动解释及板端语音的关键路径，也有总线和显示问题的复验数据。下一步进入现场试点前，重点补齐整机长时间运行、并发存储异常和完整升级恢复验证。手机手册接入与手机语音仍按后续计划推进。',
'VelaGuard 将持续监测、现场交互和基于真实数据的 AI 解释结合在一台 openvela 网关上。谢谢各位老师。'],
'tech':[
'各位评委老师好，我是 Team Falcons 的叶培林。接下来重点介绍 VelaGuard 的本地可靠性、板载 Agent 和工程验证。',
'面向工厂设备间，我们选择独立仪表支路。网关是这条 RS485总线的唯一主站，上层平台接收监测数据，原有 PLC 保留控制职责。这种接法把现场监测任务放到独立网关，后面的本地规则与 Agent 都围绕这份数据运行。',
'温度超过阈值后有两条路径。规则路径直接形成告警并更新页面；Agent路径根据事件读取运行资料，随后生成解释。先完成本地判定，再组织解释，也让断网时的现场操作有明确的数据依据。',
'架构上，采集工作线程负责总线访问，HMI组织本地规则、运行快照与 LVGL页面。Agent经本地客户端连接 openvela ai_agent，ReAct决定工具调用，模型请求通过 HTTPS到 MiMo。只读业务工具从快照取数，MQTT负责遥测与告警上报，存储操作由各自流程处理。',
'采样首先换算为工程值，再进入本地判定。阈值支持大于等于、小于等于和等值比较，严重等级优先；通信失败使用最近八次采样窗口与配置门限。运行快照与事件记录供界面和业务工具读取，工具不会为一次模型查询重新打开 RS485。网络不可用时，规则判定和现场显示继续运行。',
'一次典型故障是从站越快，成功率反而越低。测量发现，方向释放依赖 tcdrain轮询，系统十毫秒tick把实际间隔拉到约十三毫秒，快速回复落在发送方向保持的窗口中。修复将DIR切回接收放到TC完成中断，txempty恢复查询语义。零毫秒工况从两次不同的失败样本，改善到六百四十九次全部成功。',
'主动任务由告警集合变化触发，定时任务生成日报。板载Agent读取Markdown Skill，经ReAct发起运行报告工具调用，再将结构化产物写入指定报告文件。板端解析后按当前告警加载解释。历史真机中，一次八条解释用时四十一点八三秒；一份自主日报用时一百九十四秒。这些是两个不同日期的单样本。',
'业务工具从内存快照读取点位、当前异常、恢复信息和运行概况。输出有两条检查路径：告警建议解析结构、启动标识及告警匹配；语音回答检查本轮工具依据、长度和UTF-8，再分成屏幕全文与播报摘要。权限和参数检查落实在C代码中，配置仍走独立人工流程。',
'语音分为录音、云侧识别、Agent查数、云侧合成和板端播放。原始问题直接交给Agent，可组合多个业务工具，屏幕全文与语音摘要分别输出。前台语音还能挂起一条后台Agent任务，结束后恢复排队；慢网络操作不放进业务快照锁内。',
'第二个工程问题是语音时偶发显示异常。增加LTDC错误计数后，两组六十秒窗口各观察到一次FIFO欠载，同时有四百多次显示提交。定位到浮层每一百毫秒重复设置相同标签和可见性。调整为内容变化时更新后，两组提交降到二十四和四十六次，窗口内欠载为零。两组云侧时延不同，因此这里比较显示计数。',
'固件从QSPI XIP执行，运行期不能擦写正在执行的区域。因此网络下载先落到现有eMMC FAT，本机确认后写入升级状态，重启由片内启动程序烧写QSPI，再进行新固件自检；失败恢复到出厂A槽。测试CA下已下载一百五十五万三千七百零四字节到待确认，安装与回滚是独立板测范围。',
'接下来播放八十秒演示，验证采集、规则告警、主动解释、语音和断网监测的关键用户路径。当前为原型实拍占位。',
'工程证据沿着问题定位、代码修复和板端复验保存。总线有四档回复延迟工况，显示有前后计数，主动任务和真人语音有对应日志，本次界面渲染也运行了七项检查。整机长时间运行、并发eMMC异常以及完整签名OTA仍是下一轮验证重点，后续功能围绕现场试点推进。',
'VelaGuard 把本地确定性监测与板载 Agent协作落在真实openvela设备上。谢谢各位老师。']}

SOURCES={
'scene_data':['render/scene_main.c','render/scene_model.c','render/scene_backend.c','mock-data.json'],
'topology':['VelaGuard_项目手册.md：独立网关及场景','AGENTS.md：V1、V2及总线单主站'],
'configuration':['docs/velaguard-host-nsh-protocol.md','AGENTS.md：人工确认流程'],
'architecture':['app/velaguard/vg_ui_backend_board.c','app/velaguard/vg_runtime.c','app/velaguard/vg_agent_round.c','app/velaguard/vg_agent_tools.c'],
'local':['app/velaguard/vg_alarm_eval.c','gui/main/ui/model/vg_model.c','app/velaguard/vg_runtime.c'],
'rs485':['docs/maintenance-evidence/2026-10-18/defects.md：D-04','docs/maintenance-todo.md：BUS-03'],
'agent':['app/velaguard/vg_agent_seed.c','app/velaguard/vg_advice.c','app/velaguard/vg_agent_round.c','docs/voice-evidence/2026-10-25/stream-rebuffer/final-accept.log','logs/Foleaf/2026-09-16/claude-code__dbd0eb98-6d13-4bdc-a758-a5c3605ffc49.jsonl：原始日报样本'],
'guard':['app/velaguard/vg_agent_tools.c','app/velaguard/vg_ai_contract.c','app/velaguard/vg_advice.c','docs/voice-evidence/2026-10-25/ops-assistant/README.md'],
'voice':['app/velaguard/vg_voice_round.c','app/velaguard/vg_agent_round.c','docs/voice-evidence/2026-10-25/tts-continuity/manual.log','docs/voice-evidence/2026-10-25/tts-continuity/manual-tail.log'],
'display':['docs/maintenance-evidence/2026-10-28/ltdc/README.md'],
'ota':['docs/adr/0008-ota-fat-staging-with-local-confirmation.md','docs/ota-evidence/2026-10-29/board-network.md','docs/ota-evidence/2026-10-28/t04-install.md','docs/ota-evidence/2026-10-28/t05-rollback.md','docs/issues/ota/acceptance-todo.md'],
'delivery':['docs/maintenance-todo.md','docs/voice-evidence/2026-10-25/ops-assistant/README.md','docs/issues/ota/acceptance-todo.md','.agents/skills/velaguard-board-inner-loop/SKILL.md','logs/：原始 AI Coding 日志','render/CMakeLists.txt']}

def el(p,tag,**attrs):return E.SubElement(p,Q(tag),{k.replace('_','-'):str(v) for k,v in attrs.items()})
def txt(p,x,y,value,size=48,color=INK,bold=False,anchor='start'):
    width=sum(1 if unicodedata.east_asian_width(c) in 'WF' else .68 for c in value)*size*1.08+12
    left=x-width/2 if anchor=='middle' else x-width if anchor=='end' else x
    g=el(p,'g',**{'data-pptx-bounds':f'{left} {y-size*1.1} {width} {size*1.4}'})
    n=el(g,'text',x=x,y=y,font_size=size,fill=color,font_weight='bold' if bold else 'normal',text_anchor=anchor)
    n.text=value;return n
def lines(p,x,y,values,size=46,color=INK,step=None,bold=False):
    for i,v in enumerate(values):txt(p,x,y+i*(step or size*1.5),v,size,color,bold)
def rect(p,x,y,w,h,fill=LIGHT,stroke='none',sw=2):return el(p,'rect',x=x,y=y,width=w,height=h,fill=fill,stroke=stroke,stroke_width=sw)
def line(p,x1,y1,x2,y2,color=LINE,width=3,dash=None,arrow=False):
    n=el(p,'line',x1=x1,y1=y1,x2=x2,y2=y2,stroke=color,stroke_width=width)
    if dash:n.set('stroke-dasharray',dash)
    if arrow:n.set('marker-end',f'url(#cg-arrow-{color[1:]})')
    return n
def node(p,x,y,w,h,title,sub=(),color=TEAL):
    rect(p,x,y,w,h,'#FFFFFF',color,3);rect(p,x,y,9,h,color)
    txt(p,x+30,y+62,title,49,color,True)
    lines(p,x+30,y+127,sub,39,MUTED,58)
def image(p,x,y,w,h,name):
    return el(p,'image',href='../images/chatgpt/'+name,x=x,y=y,width=w,height=h,preserveAspectRatio='xMidYMid meet')
def foot(p,value):txt(p,180,1284,value,28,MUTED)
def mockfoot(p):foot(p,'本地界面渲染 · 场景模拟数据；界面与告警判定复用项目代码')
def section(p,x,y,label,color=TEAL):
    txt(p,x,y,label,52,color,True);line(p,x,y+24,x+530,y+24,color,3)

def set_source_text(root,ident,values,size=72):
    g=next(n for n in root.iter(Q('g')) if n.get('id')==ident)
    old=next(g.iter(Q('text')));attrs=dict(old.attrib)
    attrs.pop('data-pptx-text-model',None);attrs.pop('data-paragraph-line-height',None)
    for child in list(old):old.remove(child)
    old.text=values if isinstance(values,str) else values[0];old.set('font-size',str(size))
    if isinstance(values,list):
        for v in values[1:]:el(old,'tspan',x=old.get('x'),dy=size*1.65).text=v
    return old

def body(root,title,num):
    for child in list(root):
        ident=child.get('id','')
        frame=child.get('data-pptx-frame','')
        isfooter=bool(frame and float(frame.split()[0])<200 and float(frame.split()[1])>1330)
        keep=child.tag in [Q('defs'),Q('rect')] or ident.startswith(('layout-','master-')) or ident in ['shape-3','shape-4'] or isfooter
        if not keep:root.remove(child)
        if isfooter:
            for n in child.iter(Q('text')):
                for sub in list(n):n.remove(sub)
                n.text=f'{num:02}'
    set_source_text(root,'shape-3',title,76)
    defs=root.find(Q('defs'))
    if defs is None:defs=el(root,'defs')
    for color in [MUTED,TEAL,BLUE,PURPLE,RED,GREEN]:
        m=el(defs,'marker',id='cg-arrow-'+color[1:],viewBox='0 0 10 10',refX=9,refY=5,markerWidth=8,markerHeight=8,orient='auto')
        el(m,'path',d='M 0 0 L 10 5 L 0 10 Z',fill=color)
    return el(root,'g',id=f'chatgpt-content-{num}',font_family='MiSans')

def chart(p,key,x,y,w,h,cats,series,maxval,fmt='0',colors=None):
    """Editable native column chart with matching SVG fallback."""
    colors=colors or [TEAL,BLUE]
    g=el(p,'g',id=key,**{'data-pptx-replace-with':'chart'})
    px=x+65;py=y+55;pw=w-95;ph=h-150
    data={'x':x,'y':y,'width':w,'height':h,'name':key,'type':'column','grouping':'clustered',
          'categories':cats,'series':[{'name':name,'values':vals} for name,vals in series],
          'plot_area':{'x':px,'y':py,'width':pw,'height':ph},'show_legend':False,'gap_width':90,'overlap':0,
          'axes':{'category':{'visible':True,'position':'bottom','font_size':34,'color':INK,'major_gridlines':False},
                  'value':{'visible':False,'position':'left','minimum':0,'maximum':maxval,'major_gridlines':False}},
          'data_labels':{'show_value':True,'position':'outside_end','font_size':42,'color':INK,'number_format':fmt},
          'style':{'font_family':'MiSans','axis_font_size':34,'text_color':INK,'axis_color':LINE,'grid_color':'none',
                   'chart_area_fill':'#FFFFFF','plot_area_fill':'#FFFFFF','colors':colors}}
    el(g,'metadata',type='application/json').text=json.dumps(data,ensure_ascii=False)
    line(g,px,py+ph,px+pw,py+ph,LINE,2).set('id',key+'-axis')
    groupw=pw/len(cats);totalbar=groupw*.64;barw=totalbar/len(series)
    for i,cat in enumerate(cats):
        center=px+(i+.5)*groupw
        txt(g,center,py+ph+61,cat,34,INK,anchor='middle')
        for j,(_,vals) in enumerate(series):
            val=vals[i];bh=val/maxval*ph;bx=center-totalbar/2+j*barw
            rect(g,bx,py+ph-bh,barw-7,bh,colors[j%len(colors)])
            label=f'{val:.1f}%' if '%' in fmt else str(int(val))
            txt(g,bx+(barw-7)/2,py+ph-bh-18,label,42,INK,anchor='middle')
    return g

def topology(p,variant):
    txt(p,180,372,'拟落地：工厂设备间的独立监测支路',48,TEAL,True)
    node(p,180,440,470,180,'环境与水浸仪表',['温湿度 / 开关状态'])
    node(p,180,710,470,180,'电参量与 UPS 仪表',['电压 / 电流 / 负载'])
    line(p,700,525,700,800,TEAL,5);line(p,650,525,700,525,TEAL,5);line(p,650,800,700,800,TEAL,5)
    line(p,700,660,880,660,TEAL,5,arrow=True);txt(p,724,625,'RS485',34,TEAL)
    node(p,890,540,555,260,'VelaGuard',['openvela / STM32H750','采集 · 告警 · Agent'])
    if variant=='scene':
        image(p,1690,410,680,510,'prototype.jpg');txt(p,2030,959,'真实原型与验证台架',35,MUTED,anchor='middle')
        line(p,1165,800,1165,905,BLUE,4,arrow=True)
        txt(p,1210,870,'MQTT',35,BLUE)
        node(p,890,920,555,175,'运维平台',['遥测与告警留档'],BLUE)
        line(p,180,1130,2380,1130,LINE,3)
        txt(p,180,1215,'现有 PLC 与执行设备',48,MUTED,True)
        txt(p,900,1215,'原控制回路继续运行',45,MUTED)
    else:
        line(p,1445,660,1645,660,BLUE,5,arrow=True);txt(p,1490,620,'MQTT',35,BLUE)
        node(p,1660,540,700,260,'运维平台',['遥测与告警留档','供人员查看与交接'],BLUE)
        line(p,180,1025,2380,1025,LINE,3)
        txt(p,180,1120,'现有 PLC 与执行设备',48,MUTED,True)
        txt(p,900,1120,'原控制回路继续运行',45,MUTED)
        txt(p,900,1190,'监测数据供人员核查与判断',40,MUTED)
    foot(p,'部署方式示意；原型在台架验证。仪表总线由 VelaGuard 单主站采集。')

def journey(p,variant):
    if variant=='scene':
        image(p,180,370,1040,590,'01_home_normal.png');image(p,1340,370,1040,590,'11_home_recovered.png')
        txt(p,180,1025,'接班：6 个点位在线',49,TEAL,True);txt(p,1340,1025,'交班：温度恢复，保留记录',49,TEAL,True)
        line(p,250,1150,2270,1150,LINE,6)
        for x,val,label,c in [(350,'28.4℃','正常',TEAL),(1030,'36.2℃','越限核查',RED),(1960,'29.0℃','恢复交接',GREEN)]:
            el(p,'circle',cx=x,cy=1150,r=13,fill=c);txt(p,x,1120,val,50,c,True,anchor='middle');txt(p,x,1225,label,39,MUTED,anchor='middle')
    else:
        image(p,180,410,1000,567,'03_alarm_critical.png')
        node(p,1360,410,1010,230,'规则路径',['采样 → 阈值判定 → 本地告警'],TEAL)
        node(p,1360,850,1010,230,'Agent 路径',['事件 → 查询运行数据 → 解释'],PURPLE)
        line(p,1190,690,1290,690,MUTED,4);line(p,1290,525,1290,965,MUTED,4)
        line(p,1290,525,1350,525,MUTED,4,arrow=True);line(p,1290,965,1350,965,MUTED,4,arrow=True)
        txt(p,180,1125,'36.2℃  >  严重阈值 35.0℃',57,RED,True)
    mockfoot(p)

def configuration(p):
    xs=[180,760,1340,1920];heads=['录入候选','试读核对','查看变更','人工确认']
    details=[['地址 / 寄存器','类型 / 倍率 / 单位'],['返回值与仪表对照','确认读数含义'],['新增或修改的点位','阈值与采样参数'],['单独提交生效','开始周期采集']]
    for i,x in enumerate(xs):
        txt(p,x,430,f'0{i+1}',76,[TEAL,BLUE,PURPLE,TEAL][i]);line(p,x,470,x+440,470,LINE,3)
        txt(p,x,565,heads[i],54,INK,True);lines(p,x,655,details[i],42,MUTED,68)
        if i<3:line(p,x+445,570,x+555,570,MUTED,4,arrow=True)
    rect(p,180,890,2200,270,LIGHT)
    txt(p,225,970,'已确认点表',52,TEAL,True);line(p,640,980,860,980,TEAL,4,arrow=True)
    txt(p,905,970,'采集快照',52,INK,True);line(p,1290,980,1490,980,BLUE,4,arrow=True)
    lines(p,1540,965,['首页与告警','业务查询工具'],47,INK,76)
    foot(p,'当前接入入口为 NSH；手机手册辅助接入列入后续计划。')

def alarms(p):
    image(p,180,420,1050,595,'03_alarm_critical.png');image(p,1330,420,1050,595,'12_trend_recovery.png')
    section(p,180,366,'先核对本次异常',RED);section(p,1330,366,'再回看变化过程',TEAL)
    lines(p,180,1110,['36.2℃ / 严重阈值 35.0℃','点位、单位、等级与阈值同时可见'],45,INK,76)
    lines(p,1330,1110,['峰值后回落到 29.0℃','趋势与告警使用同一组采样'],45,INK,76)
    mockfoot(p)

def agent(p,variant):
    if variant=='scene':
        image(p,180,420,1235,700,'06_alarm_advice.png')
        txt(p,1530,430,'从规则结果继续核查',51,TEAL,True)
        for i,(a,b) in enumerate([('事件触发','告警集合变化'),('主动查数','Skill + 运行报告工具'),('显示解释','建议、依据、关注事项')]):
            y=550+i*235;txt(p,1530,y,f'0{i+1}',54,PURPLE,True);txt(p,1660,y,a,48,INK,True);txt(p,1660,y+76,b,39,MUTED)
        foot(p,'界面为场景模拟；10-25 真机单样本：8 条告警解释 41.83 秒完成并加载。')
    else:
        centers=[360,890,1440,2070];heads=['本地事件 / 定时器','板载 ai_agent','业务工具','报告与 HMI']
        for x,h in zip(centers,heads):txt(p,x,390,h,43,TEAL,True,anchor='middle');line(p,x,430,x,1150,LINE,3,dash='10 10')
        messages=[(0,1,520,'告警集合变化 / 日报到时'),(1,1,650,'读取任务 Skill'),(1,2,780,'ReAct 调用 vg_run_report'),(2,1,900,'返回统计与事件依据'),(1,3,1020,'write_file 写出结构化报告'),(3,3,1140,'板端解析并加载到页面')]
        for a,b,y,label in messages:
            if a==b:
                if a==3:txt(p,2380,y,label,39,INK,anchor='end')
                else:txt(p,centers[a]+35,y,label,39,INK)
            else:
                line(p,centers[a],y,centers[b],y,MUTED,4,arrow=True);txt(p,(centers[a]+centers[b])/2,y-25,label,39,INK,anchor='middle')
        foot(p,'真机单样本：告警解释 41.83 秒（10-25）；自主日报 194 秒（09-16）。')

def voice_page(p,variant):
    if variant=='scene':
        image(p,180,380,1330,754,'10_voice_answer.png')
        lines(p,1630,440,['现场问题','温度异常，','电压和负载正常吗？'],49,INK,88,True)
        lines(p,1630,780,['查询关联点位','屏幕显示全文','设备播报摘要'],46,TEAL,105)
        mockfoot(p)
    else:
        names=['板端录音','云侧 ASR','板载 Agent','云侧 TTS','板端播放'];xs=[180,630,1080,1530,1980]
        details=[['按键开始 / 结束','PCM 录音'],['识别原始问题','保留用户问法'],['组合业务工具','生成全文与摘要'],['流式合成语音','缓冲后起播'],['显示完整回答','播放摘要']]
        for i,x in enumerate(xs):
            txt(p,x,420,f'0{i+1}',72,TEAL if i%2==0 else BLUE)
            line(p,x,455,x+350,455,LINE,3);txt(p,x,550,names[i],50,INK,True);lines(p,x,645,details[i],38,MUTED,65)
            if i<4:line(p,x+355,565,x+435,565,MUTED,4,arrow=True)
        rect(p,180,900,2200,280,LIGHT)
        txt(p,230,985,'前台语音与后台任务协作',52,PURPLE,True)
        txt(p,230,1085,'语音前台 → 挂起一条后台 Agent 请求 → 播放结束后恢复排队',46,INK)
        foot(p,'10-25 真人按键样本完成录音、查询与播放；多问法验收另用合成 WAV 回喂 ASR。')

def offline(p):
    image(p,180,400,1220,692,'07_network_down.png')
    txt(p,1530,410,'网络与采集分别观察',50,INK,True)
    for y,a,b,c in [(560,'NET 断开','联网解释暂不可用',RED),(790,'ACQ 继续','6 个仪表仍在线',TEAL),(1020,'36.1℃ 新样本','本地告警与趋势更新',TEAL)]:
        txt(p,1530,y,a,54,c,True);txt(p,1530,y+78,b,42,MUTED)
    mockfoot(p)

def architecture(p):
    rect(p,630,370,1170,805,LIGHT)
    txt(p,685,440,'openvela / STM32H750B-DK',48,TEAL,True)
    node(p,180,615,340,230,'RS485 仪表',['Modbus RTU'])
    node(p,680,530,470,220,'采集工作线程',['总线访问 / 工程值'])
    node(p,1270,530,470,220,'本地规则与快照',['告警 / 事件 / 统计'])
    line(p,520,720,585,720,TEAL,4);line(p,585,720,585,640,TEAL,4);line(p,585,640,670,640,TEAL,4,arrow=True)
    line(p,1150,640,1260,640,TEAL,4,arrow=True)
    node(p,680,905,470,205,'LVGL 页面',['读数 / 趋势 / 告警'],BLUE)
    node(p,1270,905,470,205,'板载 ai_agent',['Skill / ReAct / 工具'],PURPLE)
    line(p,1505,750,1505,890,PURPLE,4,arrow=True)
    line(p,1505,830,915,830,BLUE,4);line(p,915,830,915,890,BLUE,4,arrow=True)
    node(p,1950,490,430,230,'MiMo',['HTTPS 模型推理'],PURPLE)
    line(p,1760,1000,1845,1000,PURPLE,4)
    # A wire gap denotes a crossing without a junction at the MQTT route.
    line(p,1845,600,1845,880,PURPLE,4);line(p,1845,910,1845,1000,PURPLE,4)
    line(p,1845,600,1940,600,PURPLE,4,arrow=True)
    line(p,1740,640,1805,640,BLUE,4);line(p,1805,640,1805,895,BLUE,4)
    line(p,1805,895,1940,895,BLUE,4,arrow=True)
    line(p,1505,1110,1505,1200,BLUE,4);line(p,1505,1200,1880,1200,BLUE,4)
    line(p,1880,1200,1880,1077,BLUE,4);line(p,1880,1077,1940,1077,BLUE,4,arrow=True)
    txt(p,1550,1175,'报告落盘',32,BLUE)
    node(p,1950,815,430,155,'MQTT',['遥测与告警'],BLUE)
    node(p,1950,1000,430,155,'eMMC',['配置与报告'],BLUE)
    foot(p,'模型推理在云侧；ReAct 调度与业务工具执行在板端。业务工具读取快照，不发起总线采集。')

def local_loop(p):
    for y,label,c in [(400,'采集与判定',TEAL),(740,'运行数据',BLUE),(1080,'联网任务',PURPLE)]:
        txt(p,180,y,label,48,c,True);line(p,180,y+35,2380,y+35,LINE,2)
    row=[(520,'Modbus 采样',['寄存器 → 工程值']),(1140,'本地规则',['阈值比较 / 失败窗口']),(1800,'现场显示',['告警 / 趋势 / 记录'])]
    for x,a,b in row:node(p,x,470,540,170,a,b)
    line(p,1060,555,1125,555,TEAL,4,arrow=True);line(p,1680,555,1785,555,TEAL,4,arrow=True)
    node(p,520,805,800,170,'业务快照与事件',['版本 / 在线状态 / 告警实例'],BLUE)
    node(p,1540,805,800,170,'只读业务工具',['点位 / 异常 / 恢复 / 运行报告'],BLUE)
    line(p,1410,640,1410,885,BLUE,4);line(p,1320,885,1525,885,BLUE,4,arrow=True)
    txt(p,520,1165,'Agent 解释、日报与语音',48,PURPLE,True)
    txt(p,1540,1165,'云侧任务与本地快照解耦',44,INK)
    foot(p,'阈值支持 ge / le / eq；离线使用最近 8 次采样窗口，配置门限在 5–8 次之间。')

def rs485(p):
    section(p,180,365,'定位：DIR 释放晚于快速回复',RED)
    txt(p,210,480,'修复前',46,RED,True);txt(p,210,760,'修复后',46,TEAL,True)
    for y in [540,820]:
        line(p,230,y,1280,y,LINE,3);rect(p,350,y-45,260,45,BLUE)
        txt(p,480,y-70,'请求发送',37,BLUE,anchor='middle')
    rect(p,615,495,535,45,'#F5C9C6');txt(p,710,600,'DIR 等待轮询 ≈13 ms',39,RED)
    rect(p,750,615,210,40,RED);txt(p,715,705,'快速回复落在窗口内',39,RED)
    line(p,620,780,620,870,TEAL,5);txt(p,650,915,'TC 中断立即释放到 RX',42,TEAL,True)
    rect(p,750,950,210,40,TEAL);txt(p,715,1040,'接收快速回复',39,TEAL)
    txt(p,180,1170,'tcdrain 轮询 → TC 完成中断',48,INK,True)
    txt(p,1440,365,'0 ms 从站设置：独立样本成功率',45,INK,True)
    chart(p,'rs485-success',1410,435,970,675,['修复前①','修复前②','修复后'],[('成功率',[13.9,67.2,100.0])],110,'0.0"%"',[TEAL])
    for x,value in [(1621,'38/274'),(1913,'88/131'),(2204,'649/649')]:txt(p,x,1160,value,37,MUTED,anchor='middle')
    foot(p,'10-18 台架板测；时序为机制示意，非逻辑分析仪采样。另测 5 / 20 / 150 ms 工况。')

def guard(p):
    node(p,180,420,620,260,'运行快照',['点位值 / 在线状态','异常 / 恢复 / 统计'],TEAL)
    line(p,800,550,1020,550,TEAL,4,arrow=True)
    node(p,1040,420,600,260,'C 业务工具',['参数检查 / 只读取数','不打开 RS485'],BLUE)
    line(p,1640,550,1820,550,BLUE,4,arrow=True)
    node(p,1840,420,540,260,'Agent 输出',['报告与界面文本'],PURPLE)
    section(p,180,820,'告警解释路径',TEAL)
    txt(p,180,940,'VGADV1 解析 → boot 匹配 → 当前告警匹配',45,INK)
    txt(p,180,1035,'检查后加载建议与依据',43,MUTED)
    section(p,1410,820,'语音回答路径',PURPLE)
    lines(p,1410,940,['本轮工具依据 / 长度 / UTF-8','屏幕全文与播报摘要拆分'],43,INK,95)
    foot(p,'业务工具可查询点位、当前异常、恢复、运行报告与系统概况；配置由独立人工流程提交。')

def display(p,compact=False):
    if compact:
        txt(p,180,370,'快速从站兼容',51,TEAL,True)
        chart(p,'compact-rs485',180,440,1000,640,['前①','前②','修复后'],[('成功率',[13.9,67.2,100])],110,'0.0"%"',[TEAL])
        txt(p,180,1180,'方向释放从轮询改为 TC 中断',43,INK)
        txt(p,1330,370,'显示提交次数 / 60秒',51,BLUE,True)
        txt(p,1560,425,'蓝色：修复前   青色：修复后',34,MUTED)
        chart(p,'compact-display',1300,440,1080,640,['窗口1','窗口2'],[('修复前',[438,454]),('修复后',[24,46])],520,'0',[BLUE,TEAL])
        txt(p,1330,1180,'浮层从重复设置改为按变化更新',42,INK)
        foot(p,'历史板测：RS485 为 10-18；显示为 10-28 两组 60 秒窗口，云侧时延及识别结果不同。')
    else:
        txt(p,180,370,'显示提交次数 / 每组60秒',50,BLUE,True)
        chart(p,'ltdc-submissions',180,445,1230,650,['窗口1','窗口2'],[('修复前',[438,454]),('修复后',[24,46])],520,'0',[BLUE,TEAL])
        rect(p,180,1140,50,16,BLUE);txt(p,255,1170,'修复前',38,MUTED)
        rect(p,530,1140,50,16,TEAL);txt(p,605,1170,'修复后',38,MUTED)
        lines(p,1560,430,['每100 ms 重复设置','相同标签与可见性'],46,INK,78,True)
        line(p,1900,605,1900,705,MUTED,4,arrow=True)
        lines(p,1560,790,['按内容变化更新','减少 LVGL 失效重绘'],46,TEAL,78,True)
        txt(p,1560,1040,'FIFO 欠载：各 1 次 → 各 0 次',43,INK,True)
        foot(p,'10-28 两组各 60 秒观察窗口；前后云侧时延和识别结果不同，比较项为显示计数。')

def ota(p,variant):
    txt(p,180,375,'运行阶段：QSPI XIP',52,TEAL,True);txt(p,1440,375,'重启阶段：片内 boot stub',52,PURPLE,True)
    line(p,180,410,1200,410,TEAL,3);line(p,1440,410,2380,410,PURPLE,3)
    steps=[('通知与下载',['MQTTS / HTTPS','仅 RJ45']),('eMMC 暂存',['现有 FAT','摘要与签名']),('本机确认',['每个新版本','写入升级状态']),('烧写 QSPI',['启动程序执行','安装新镜像']),('自检与恢复',['通过后确认','失败回出厂 A'])]
    for i,(a,b) in enumerate(steps):
        x=180+i*450;txt(p,x,535,f'0{i+1}',76,TEAL if i<3 else PURPLE)
        txt(p,x,660,a,49,INK,True);lines(p,x,765,b,40,MUTED,70)
        if i<4:line(p,x+340,665,x+425,665,MUTED,4,arrow=True)
    rect(p,180,1010,2200,190,LIGHT)
    txt(p,225,1080,'1,553,704 B',67,TEAL,True);txt(p,840,1080,'测试 CA 下下载到 READY',46,INK,True)
    txt(p,840,1150,'安装与回滚分别完成板测；完整签名任务链路继续验证',39,MUTED)
    foot(p,'测试信任与诊断夹具证据；生产信任配置、完整安装上报及断电矩阵尚待验收。')

def delivery(p,variant):
    if variant=='scene':
        image(p,180,400,1140,646,'14_handover_report.png')
        txt(p,180,1140,'恢复后交接：峰值、状态与后续核查',46,TEAL,True)
        x=1460
    else:x=180
    stages=[('已验证关键路径','采集 / 告警 / 主动解释 / 真人语音',TEAL),('下一轮整机验证','长时间运行 / eMMC 并发 / 完整 OTA',BLUE),('现场试点后扩展','手机手册接入与手机语音巡检',PURPLE)]
    if variant=='scene':
        for i,(a,b,c) in enumerate(stages):
            rows=['长时间运行 / eMMC 并发','完整 OTA 安装与恢复'] if i==1 else [b]
            y=430+i*275;txt(p,x,y,a,48,c,True);lines(p,x,y+85,rows,38,MUTED,60)
        mockfoot(p)
    else:
        steps=[('复现问题','台架固定工况'),('测量定位','DWT / LTDC 计数'),('实现修复','应用 / 驱动 / UI'),('板端复验','前后日志与固件标识'),('沉淀交付','AI 日志 / 开发 Skill')]
        for i,(a,b) in enumerate(steps):
            x=180+i*450;txt(p,x,420,f'0{i+1}',68,TEAL if i%2==0 else BLUE)
            txt(p,x,535,a,49,INK,True);txt(p,x,625,b,34,MUTED)
            if i<4:line(p,x+350,535,x+425,535,MUTED,4,arrow=True)
        line(p,180,735,2380,735,LINE,3)
        txt(p,180,840,'已有实证',49,TEAL,True)
        txt(p,650,840,'RS485 四档延迟 / 主动任务 / 真人语音 / 显示计数',43,INK)
        txt(p,180,990,'下一轮验证',49,BLUE,True)
        txt(p,650,990,'整机长测 / eMMC 并发异常 / 完整签名 OTA',43,INK)
        txt(p,180,1140,'本轮可复现',49,PURPLE,True)
        txt(p,650,1140,'真实 UI 本地渲染，7 项检查通过',43,INK)
        foot(p,'历史板测沿用原始记录；本轮新增答辩场景与材料。开发 Skill 与设备 Skill 分别保存。')

def add_notes(work,stem,variant,index,keys):
    note=SPEECH[variant][index]+'\n\n资料来源：\n'+'\n'.join(s for k in keys for s in SOURCES[k])
    if any(k=='scene_data' for k in keys):note+='\n\n场景条件：本地 LVGL 渲染，固定模拟点表与采样。AI文本、报告和网络状态由演示快照注入；不是本次真机或模型执行记录。'
    if 'display' in keys:note+='\n\n显示计数来自两组各60秒窗口；云侧时延与识别结果不同，一次识别失败。计数比较不等于完整语音链路成功率，也不等于长期可靠性结论。'
    if 'ota' in keys:note+='\n\nOTA：测试CA、测试签名与凭据；下载到待确认。安装/回滚为独立诊断夹具；完整签名任务本机确认、安装、自检及结果上报仍待串联。'
    (work/'notes'/f'{stem}.md').write_text(note+'\n',encoding='utf-8',newline='\n')
    return note

def main():
    ap=argparse.ArgumentParser();ap.add_argument('variant',choices=MAPS);ap.add_argument('workspace',type=Path);ap.add_argument('--representative',action='store_true');args=ap.parse_args()
    variant=args.variant;work=args.workspace
    images=work/'images/chatgpt';images.mkdir(parents=True,exist_ok=True)
    for p in (HERE/'assets').glob('*.png'):shutil.copy2(p,images/p.name)
    shutil.copy2(HERE.parent/'final-defense-20261031/images/image_4ed3da31383c9acc.jpg',images/'prototype.jpg')
    roots={i:E.parse(work/'authoring-svg-flat'/f'slide_{i:02}.svg').getroot() for i in set(MAPS[variant])}
    plan=[];records=[]
    functions_scene=[None,lambda p:topology(p,'scene'),lambda p:journey(p,'scene'),configuration,alarms,lambda p:agent(p,'scene'),lambda p:voice_page(p,'scene'),offline,architecture,lambda p:display(p,True),lambda p:ota(p,'scene'),None,lambda p:delivery(p,'scene'),None]
    functions_tech=[None,lambda p:topology(p,'tech'),lambda p:journey(p,'tech'),architecture,local_loop,rs485,lambda p:agent(p,'tech'),guard,lambda p:voice_page(p,'tech'),display,lambda p:ota(p,'tech'),None,lambda p:delivery(p,'tech'),None]
    keys_scene=[['topology'],['topology'],['scene_data'],['configuration'],['scene_data','local'],['scene_data','agent'],['scene_data','voice'],['scene_data','local'],['architecture'],['rs485','display'],['ota'],['voice','local'],['scene_data','delivery'],['topology']]
    keys_tech=[['topology'],['topology'],['scene_data','local'],['architecture'],['local'],['rs485'],['agent'],['guard'],['voice'],['display'],['ota'],['voice','local'],['delivery'],['topology']]
    functions=functions_scene if variant=='scene' else functions_tech
    keys=keys_scene if variant=='scene' else keys_tech
    for i,source in enumerate(MAPS[variant]):
        num=i+1;root=copy.deepcopy(roots[source]);stem=f'chatgpt_{variant}_{num:02}'
        if num==1:
            old=set_source_text(root,'shape-7',['基于 openvela 的 AI 辅助巡检网关',f'Team Falcons   叶培林   {LABELS[variant]}'],52)
        elif num==14:
            set_source_text(root,'shape-6','本地持续监测，AI 依据真实数据辅助巡检',52)
        elif num==12:
            # Original native photograph and dark caption band remain.
            for n in root.iter(Q('text')):
                text=''.join(n.itertext())
                if '真机演示' in text:
                    for c in list(n):n.remove(c)
                    n.text='80秒真机演示：采集、告警、解释、语音与断网监测';n.set('font-size','44')
        else:
            p=body(root,TITLES[variant][i],num);functions[i](p)
        target=work/'authoring-svg-flat'/f'{stem}.svg'
        target.write_bytes(E.tostring(root,encoding='utf-8',xml_declaration=True))
        plan.append({'source_slide':source,'svg':target.name})
        note=add_notes(work,stem,variant,i,keys[i])
        records.append({'page':num,'title':TITLES[variant][i],'source_slide':source,'svg':target.name,'seconds':TIMES[i],'speech':SPEECH[variant][i],'source_keys':keys[i],'notes':note})
    (work/'page_plan.json').write_text(json.dumps({'schema':'ppt-master.roundtrip-page-plan.v1','pages':plan},ensure_ascii=False,indent=2)+'\n',encoding='utf-8',newline='\n')
    (HERE/f'content-{variant}.json').write_text(json.dumps({'author':'CHATGPT','date':'2026-11-05','variant':LABELS[variant],'input_commit':'a075560','timing':TIMES,'pages':records},ensure_ascii=False,indent=2)+'\n',encoding='utf-8',newline='\n')
    print(f'{variant}: 14 slides, {sum(TIMES)} seconds, source-preserving template')

if __name__=='__main__':main()
