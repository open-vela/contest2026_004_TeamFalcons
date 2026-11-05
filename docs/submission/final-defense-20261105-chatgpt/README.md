# VelaGuard 双版本决赛答辩 CHATGPT

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
cmake -S docs/submission/final-defense-20261105-chatgpt/render \
  -B .debug/chatgpt-defense-20261105 -DCMAKE_C_COMPILER=/usr/bin/gcc
cmake --build .debug/chatgpt-defense-20261105 -j 4
ctest --test-dir .debug/chatgpt-defense-20261105 --output-on-failure
```

场景入口只替代PC模拟器的数据与计时，在独立链接单元中复用项目模型。告警调用现有vg_alarm_sample；正式固件不包含本目录源文件。程序自行验证正常、预警、严重、网络中断及恢复状态。生成帧为480×272 RGB565。

## PPT重建

按ppt-master Edit Native PPTX导入原答辩稿到两个独立round-trip工作区。运行make_decks.py，参数为scene或tech及对应工作区路径。然后执行技能脚本stamp_native_fallbacks.py、svg_authoring_view.py --refresh-summary、svg_quality_checker.py --roundtrip、svg_to_pptx.py --roundtrip --native-charts-and-tables --primary-language zh-CN。

所有技能脚本使用本机绝对路径C:/Users/19y/.config/magpie/library/skills/ppt-master/scripts/。保留导入器生成的原生背板，不手改analysis/native-payloads。

运行prepare_materials.py生成口播与视频脚本。qa/render_ppt.ps1逐份调用PowerPoint导出PNG，使用协作互斥量，只关闭本轮演示文稿。最终检查与文件哈希见verification.json，视觉复核见qa/。
