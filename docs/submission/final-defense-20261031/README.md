# VelaGuard 决赛答辩材料

2026-10-31，Team Falcons，叶培林。采用官方模板，12 页主讲、4 页问答附录。主线为现场巡检与告警，纳入已有板端语音成果。

## 交付文件

- `VelaGuard-决赛答辩-20261031.pptx`：可编辑答辩稿，备注保存口播、来源和测试条件。
- `逐页口播稿.md`：逐页正文和问答。净内容 440 秒，含 80 秒视频，预留 40 秒切页与停顿。
- `80秒视频脚本.md`：镜头、操作、旁白、字幕及播放安排。

正式输出位于 `F:\Documents\velaguard作品提交\决赛答辩-20261031\`。当前第 10 页为原型实拍，视频按脚本录制后替换。

## 本轮文案调整

页面围绕任务、数据和下一步操作组织：接入页呈现录入、试读、核对与确认；验证页呈现测试条件和观察结果。重复标题、权限条目、接口说明、文件名和制作提示已从页面移除。技术条件和当前进度集中保存在备注与验证记录。

口播按产品使用过程展开，视频脚本按镜头和实际操作编排。保留官方模板、16 页顺序及实测数值。

## 制作源文件

- `content.json`：逐页标题、口播、来源和证据备注。
- `polish_deck.py`：本轮页面文案修改及表格更新。
- `write_speech.py`：从逐页内容生成口播稿。
- `build_deck.py`：首版官方模板填充程序，保留供初始制作复现。
- `normalize_package.py`：JPEG 类型声明规范化工具。
- `authoring/`、`images/`、`icons/`：当前页面 SVG 与引用素材。
- `page_plan.json`：页面顺序及源页映射。
- `verification.json`：输出摘要、检查和素材来源。

## 重建本轮润色稿

输入为提交 `0d4474a` 中的首版 PPT。旧版原件也保存在本机润色工作区的 `sources/source.pptx`。修改均在导入副本中完成。

在 WSL 中提取输入：

```bash
git show 0d4474a:docs/submission/final-defense-20261031/VelaGuard-决赛答辩-20261031.pptx > /tmp/velaguard-defense-base.pptx
```

在 Windows 中导入并修改，以下路径可替换为自己的制作目录：

```powershell
python C:\Users\19y\.codex\skills\ppt-master\scripts\pptx_to_svg.py `
  '\\wsl.localhost\Debian\tmp\velaguard-defense-base.pptx' `
  -o 'F:\Documents\velaguard作品提交\build\final-defense-copy-20261031' `
  --inheritance-mode both --roundtrip

python docs/submission/final-defense-20261031/polish_deck.py `
  'F:\Documents\velaguard作品提交\build\final-defense-copy-20261031'

python C:\Users\19y\.codex\skills\ppt-master\scripts\svg_authoring_view.py `
  'F:\Documents\velaguard作品提交\build\final-defense-copy-20261031\authoring-svg-flat' --refresh-summary

python C:\Users\19y\.codex\skills\ppt-master\scripts\svg_quality_checker.py `
  'F:\Documents\velaguard作品提交\build\final-defense-copy-20261031' --roundtrip --json

python C:\Users\19y\.codex\skills\ppt-master\scripts\svg_to_pptx.py `
  'F:\Documents\velaguard作品提交\build\final-defense-copy-20261031' `
  --roundtrip --native-charts-and-tables --primary-language zh-CN

python docs/submission/final-defense-20261031/write_speech.py
```

## 素材与验证

实物照片来自原型台架。第 5 页使用 `assets/alarm-rule-current.png`，源帧为 `.debug/defense-20261031/frames/fixture_check/05_fixture_alarm_warn3.ppm`，页面标注模拟数据。

该帧由当前 GUI 代码的 board/RGB565 无头构建生成，2026-10-31 执行的 6 项 CTest 通过。原始测试记录位于 `.debug/defense-20261031/Testing/Temporary/LastTest.log`。真机证据的日期与条件见各页备注。

官方模板使用 MiSans 和嵌入字体。发布前在 PowerPoint 中打开并导出全部 16 页，检查文字、表格和图像。线上答辩前再用实际电脑检查共享声音与视频播放。
