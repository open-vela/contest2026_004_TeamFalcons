# VelaGuard 决赛答辩材料

2026-10-31，Team Falcons，叶培林。采用官方模板，全稿 16 页连续主讲，以谢谢页结束。

## 交付文件

- `VelaGuard-决赛答辩-20261031.pptx`：可编辑答辩稿，备注保存口播、来源和测试条件。
- `逐页口播稿.md`：16 页正文与时间分配。净内容 440 秒，含 80 秒视频，预留 40 秒切页与停顿。
- `80秒视频脚本.md`：镜头、操作、旁白、字幕及播放安排。

正式输出位于 `F:\Documents\velaguard作品提交\决赛答辩-20261031\`。第 14 页为演示，当前放原型实拍，录制完成后替换为视频。

## 主讲顺序

产品定位与巡检任务、设备接入、系统架构、本地告警、断网值守、Agent 主动任务、语音使用及数据路径、工程优化、验证结果、升级维护、真机演示、后续计划、谢谢。

原有四页补充内容已纳入主讲。断网场景说明现场人员仍可查看的数据，语音技术页展开录音、识别、查数、回答与播放。测试条件和实现进度保存在对应页面及备注中。

## 制作源文件

- `content.json`：当前页序、标题、口播、来源和证据备注。
- `story_deck.py`：重排连续主讲，改写场景与技术页，更新页码。
- `write_speech.py`：生成当前 16 页口播稿。
- `authoring/`、`images/`、`icons/`：当前页面 SVG 与引用素材。
- `page_plan.json`：当前输出页与输入版源页的映射。
- `verification.json`：输出摘要、检查和素材来源。
- `visual-qa/`：当前视觉检查记录与全稿预览，以及上一轮排版对比。

`build_deck.py`、`polish_deck.py`、`layout_deck.py` 保留各阶段制作过程；相应历史版本通过 Git 提交重建。

## 重建当前叙事版

输入为提交 `6d2fcdf` 中的排版版 PPT，本机工作区也保留该输入。

先在 WSL 中提取输入：

```bash
git show 6d2fcdf:docs/submission/final-defense-20261031/VelaGuard-决赛答辩-20261031.pptx > /tmp/velaguard-defense-layout.pptx
```

在 Windows 中导入副本并编排：

```powershell
python C:\Users\19y\.config\magpie\library\skills\ppt-master\scripts\pptx_to_svg.py `
  '\\wsl.localhost\Debian\tmp\velaguard-defense-layout.pptx' `
  -o 'F:\Documents\velaguard作品提交\build\final-defense-story-20261031' `
  --inheritance-mode both --roundtrip

python docs/submission/final-defense-20261031/story_deck.py `
  'F:\Documents\velaguard作品提交\build\final-defense-story-20261031'

python C:\Users\19y\.config\magpie\library\skills\ppt-master\scripts\svg_authoring_view.py `
  'F:\Documents\velaguard作品提交\build\final-defense-story-20261031\authoring-svg-flat' --refresh-summary

python C:\Users\19y\.config\magpie\library\skills\ppt-master\scripts\svg_quality_checker.py `
  'F:\Documents\velaguard作品提交\build\final-defense-story-20261031' --roundtrip --json

python C:\Users\19y\.config\magpie\library\skills\ppt-master\scripts\svg_to_pptx.py `
  'F:\Documents\velaguard作品提交\build\final-defense-story-20261031' `
  --roundtrip --native-charts-and-tables --primary-language zh-CN

python docs/submission/final-defense-20261031/write_speech.py
```

## 素材与检查

实物照片来自原型台架。第 6 页告警图为 `assets/alarm-rule-current.png`，页面标注模拟数据；源帧由当前 GUI 的 board/RGB565 无头构建生成，原始记录位于 `.debug/defense-20261031/`。2026-10-31 的 6 项 CTest 通过。

真机结论沿用标明日期的板测记录。手机接入和手机语音列为后续计划，OTA 网络下载、安装和回滚按各自验证范围说明。

发布前使用 PowerPoint 打开并渲染 16 页，检查场景页、语音路径、页码及最后一页。检查结果见 `visual-qa/QA_REPORT.md`。
