#!/usr/bin/env python3
"""Write companion speeches and provenance for this CHATGPT deliverable."""
from pathlib import Path
import hashlib,json,zipfile

HERE=Path(__file__).resolve().parent
REPO=HERE.parents[2]
def write(path,text):path.write_text(text,encoding='utf-8',newline='\n')

for variant,label in [('scene','场景叙事版'),('tech','技术深度版')]:
    data=json.loads((HERE/f'content-{variant}.json').read_text(encoding='utf-8'))
    total=0;rows=[];speeches=[]
    for p in data['pages']:
        total+=p['seconds'];end=f'{total//60:02}:{total%60:02}'
        rows.append(f'| {p["page"]} | {p["title"]} | {p["seconds"]}秒 | {end} |')
        speeches.append(f'### 第{p["page"]}页　{p["title"]}（{p["seconds"]}秒）\n\n{p["speech"]}')
    assert total==440
    text=f'''# VelaGuard {label}口播稿 CHATGPT

全稿14页，净内容7分20秒，含第12页80秒视频；另预留40秒切页和播放切换。谢谢位于第14页。

界面截图使用真实LVGL页面及模拟场景数据。历史板测结论在对应页注明样本日期和范围。

## 时间分配

| 页 | 标题 | 净时长 | 累计 |
|---|---|---:|---:|
'''+ '\n'.join(rows)+'\n\n## 主讲正文\n\n'+'\n\n'.join(speeches)+'''

## 静帧备用讲解

当前第12页为原型实拍。视频未录制或播放失败时，返回前面相应界面，按正常采集、温度越限、解释和语音、网络中断的顺序讲解。明确界面截图为本地场景模拟，真机结论引用对应历史板测。

完整来源见PPT备注和content文件。彩排时优先保证第6、7、10页的原因、实现与证据讲清，使用预留40秒容纳停顿。
'''
    write(HERE/f'逐页口播稿-{label}-CHATGPT.md',text)

write(HERE/'80秒视频脚本-CHATGPT.md','''# VelaGuard 80秒视频脚本 CHATGPT

两版PPT共用，对应第12页。成片80秒，横屏1080p，H.264视频、AAC音频。当前PPT使用真实原型静帧占位。

## 镜头与旁白

| 时间 | 操作与画面 | 旁白与字幕 |
|---|---|---|
| 00–06 | 全景拍到原型、仪表支路和RJ45，切到屏幕。 | 这是运行openvela的VelaGuard，连接设备间巡检验证台架。标注录制日期和固件短摘要。 |
| 06–16 | 打开正常首页，保留两次刷新。 | 网关持续采集，将读数和在线状态集中到现场。点位名称、单位与接线对应。 |
| 16–28 | 人工改变测试从站或传感器数值，使温度跨过已配置阈值；查看告警和趋势。 | 温度越限后，先核对点位、数值和阈值，再查看变化过程。测试激励标注台架模拟。 |
| 28–45 | 保持同一告警，展示主动任务、工具调用和屏幕解释。 | 告警变化触发Agent，查询运行数据并整理解释。等待段可加速，字幕同时给出本次真实用时。 |
| 45–57 | 打开有效日报或本地运行报告。 | 交接时回顾运行情况和异常记录。标注报告来源；历史日报保留生成日期。 |
| 57–70 | 真人按键提问，显示回答，保留一句设备现场播报。 | 现场提问，查询关联点位。播放设备原声时暂停旁白。 |
| 70–80 | 在镜头内拔下RJ45，保留仪表连接，展示网络变化与新采样。 | 网络断开后，本地采集和告警继续运行。以新读数或采样计数说明观察结果。 |

## 录制与素材标识

每段先录完整原片，按同一点位和任务衔接解释，保存原始录制日期、固件摘要与点表配置。主机模拟从站时关闭其主站会话，保持板端单主站。

模拟界面可在片头或转场辅助解释，统一标注本地界面渲染、场景模拟数据。模拟回答与报告不作为真实模型执行记录，不拼接成连续真机操作。真机镜头保留实际数值与失败反馈，不强行套用PPT的模拟温度。

如果当次没有有效日报，45–57秒改拍趋势与告警恢复；如果语音当时失败，重录完整成功轮次，或改拍解释详情并在口播引用已有真人样本。等待加速说明以当次原片为依据。

## 嵌入与线上播放

两版第11页讲完后切到第12页。删除原型静帧及字幕带，嵌入同一份80秒MP4。播放后接第13页交付与试点计划。线上共享开启系统声音，同时备好独立MP4。

完整彩排控制在8分钟内，视频计入80秒。检查离线播放、音量、画面比例、共享声音和备用静帧讲解。
''')

write(HERE/'README.md','''# VelaGuard 双版本决赛答辩 CHATGPT

2026-11-05。输入为a075560中的答辩稿，沿用官方母版、版式、字体、封面和结束页。

## 交付

- VelaGuard-决赛答辩-场景叙事版-CHATGPT.pptx：跟随接班、越限核查、语音查询、断网与交班。
- VelaGuard-决赛答辩-技术深度版-CHATGPT.pptx：重点解释本地规则、RS485时序、板载Agent、校验、语音、显示并发及OTA。
- 两份逐页口播稿、两张全稿预览、共用80秒视频脚本。

两版均14页、净内容440秒，包含第12页80秒视频，预留40秒切换；第14页谢谢，无附录。视频录制前保留原型实拍静帧。

正式输出：F:/Documents/velaguard作品提交/决赛答辩-20261105/。文件均带CHATGPT标识，制作归属见OWNERSHIP.md。

## 内容与证据

mock-data.json定义连贯的6点场景。assets中的界面由真实LVGL页面本地渲染；数据、AI文本和报告均为模拟快照。原始P6帧保存在assets/raw-frames-CHATGPT.zip。

RS485、显示计数、主动解释、日报、真人语音及OTA分项引用已有真实验收记录，完整来源在content文件与PPT备注。没有执行新的板端烧录或现场部署测试。

## 本地界面重建

在仓库根目录用WSL执行，避免与其他作者的构建目录混用：

```bash
cmake -S docs/submission/final-defense-20261105-chatgpt/render \\
  -B .debug/chatgpt-defense-20261105 -DCMAKE_C_COMPILER=/usr/bin/gcc
cmake --build .debug/chatgpt-defense-20261105 -j 4
ctest --test-dir .debug/chatgpt-defense-20261105 --output-on-failure
```

场景入口只替代PC模拟器的数据与计时，在独立链接单元中复用项目模型。告警调用现有vg_alarm_sample；正式固件不包含本目录源文件。程序自行验证正常、预警、严重、网络中断及恢复状态。生成帧为480×272 RGB565。

## PPT重建

按ppt-master Edit Native PPTX导入原答辩稿到两个独立round-trip工作区。运行make_decks.py，参数为scene或tech及对应工作区路径。然后执行技能脚本stamp_native_fallbacks.py、svg_authoring_view.py --refresh-summary、svg_quality_checker.py --roundtrip、svg_to_pptx.py --roundtrip --native-charts-and-tables --primary-language zh-CN。

所有技能脚本使用本机绝对路径C:/Users/19y/.config/magpie/library/skills/ppt-master/scripts/。保留导入器生成的原生背板，不手改analysis/native-payloads。

运行prepare_materials.py生成口播与视频脚本。qa/render_ppt.ps1逐份调用PowerPoint导出PNG，使用协作互斥量，只关闭本轮演示文稿。最终检查与文件哈希见verification.json，视觉复核见qa/。
''')

frames=REPO/'.debug/chatgpt-defense-20261105/scenes'
if frames.is_dir():
    with zipfile.ZipFile(HERE/'assets/raw-frames-CHATGPT.zip','w',compression=zipfile.ZIP_DEFLATED) as z:
        for p in sorted(frames.glob('*.ppm')):z.write(p,p.name)
assets=[]
for p in sorted((HERE/'assets').glob('*')):
    if p.is_file():assets.append({'file':p.relative_to(HERE).as_posix(),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'kind':'synthetic-ui-render' if p.suffix=='.png' else 'raw-render-archive'})
write(HERE/'asset-manifest.json',json.dumps({'author':'CHATGPT','mock_dataset':'mock-data.json','assets':assets},ensure_ascii=False,indent=2)+'\n')
print('Companion materials generated for both CHATGPT variants.')
