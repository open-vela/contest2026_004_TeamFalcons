# VelaGuard 决赛答辩材料

制作日期：2026-10-31。内容基线：`02eef41`。队伍 Team Falcons，答辩人叶培林。

本次用户确认采用现场巡检与告警主线、线上答辩、官方模板、12 页主讲加 4 页问答附录，并纳入已有板端只读语音成果。该展示确认不自动扩大后续硬件开发范围，不把手机语音目标宣称已完成。没有修改 AGENTS.md 的规则正文。

## 文件

- `VelaGuard-决赛答辩-20261031.pptx`：可编辑答辩稿，按官方模板填充，主讲 12 页、附录 4 页。
- `逐页口播稿.md`：主讲和问答口播，净内容 440 秒，含第 10 页 80 秒视频，另留 40 秒切页和停顿，总上限 8 分钟。
- `80秒视频脚本.md`：实际操作、镜头、旁白、字幕、加速及备用方案。本轮未拍摄新视频，第 10 页当前为真实原型静帧。
- `content.json`：每页标题、时间、口播和来源。完整来源同时写入 PPT 备注，口播正文不朗读来源。
- `build_deck.py`：填充官方模板 round-trip 工作区的可重建源文件。
- `write_speech.py`：从逐页内容重新生成口播稿。
- `normalize_package.py`：仅规范 JPEG 类型声明的兼容性修正。
- `assets/`：当前代码生成的模拟数据画面，明确不是真机截图。
- `authoring/`：最终页面 SVG，便于审核和后续改字。只包含选中的 16 页，不包含工具内部不可变 backing。
- `images/`、`icons/`：这些 SVG 引用的实拍、官方模板图像与表格底图。
- `verification.json`：本次检查、素材摘要、限制及输出文件摘要。

正式输出同时复制到 `F:\Documents\velaguard作品提交\决赛答辩-20261031\`。原有官方模板、初赛 PPT、技术报告和旧素材不改写。

## 制作和重建

需要本机 ppt-master 技能、Python，以及原始官方模板。官方模板的 SHA256 记录在 `verification.json`。不要将旧 PPT 作为模板替代官方原件。

```powershell
python C:\Users\19y\.codex\skills\ppt-master\scripts\pptx_to_svg.py `
  'F:\Documents\velaguard作品提交\2026 首届 openvela AI 硬件全球开发者大赛答辩模板.pptx' `
  -o 'F:\Documents\velaguard作品提交\build\final-defense-20261031' `
  --inheritance-mode both --roundtrip

python docs/submission/final-defense-20261031/build_deck.py `
  'F:\Documents\velaguard作品提交\build\final-defense-20261031' `
  --materials 'F:\Documents\velaguard作品提交'

python C:\Users\19y\.codex\skills\ppt-master\scripts\svg_authoring_view.py `
  'F:\Documents\velaguard作品提交\build\final-defense-20261031\authoring-svg-flat' --refresh-summary

python C:\Users\19y\.codex\skills\ppt-master\scripts\svg_quality_checker.py `
  'F:\Documents\velaguard作品提交\build\final-defense-20261031' --roundtrip --json

python C:\Users\19y\.codex\skills\ppt-master\scripts\svg_to_pptx.py `
  'F:\Documents\velaguard作品提交\build\final-defense-20261031' `
  --roundtrip --native-charts-and-tables --primary-language zh-CN

# 将上一步打印的导出路径传入，保留原件，规范 JPEG 默认类型声明。
python docs/submission/final-defense-20261031/normalize_package.py `
  '<上一步导出文件.pptx>' '<发布文件.pptx>'
```

该流程保留官方 Master、Layout、主题、标识与封面图。表格因原模板使用直接格式，导入后按同一框架和官方配色重建为可编辑 PowerPoint 表格。项目中的流程和系统图使用模板原有对象填写，不拿图片替代可编辑结构。

兼容性修正只规范 `[Content_Types].xml`：原模板有大写 `JPG` 和 `image/.jpg`，与导出器新增的小写 `jpg` 声明冲突；`normalize_package.py` 去重并规范为 `image/jpeg`，所有其余 package part 保持原字节。系统图中的原生组展开后，浅色底按等效不透明配色重建，避免子对象恢复时丢失父级坐标。所有修正均保留在制作源文件，没有修改全局技能或官方原件。

模板使用 MiSans，并带嵌入字体。本机装有 MiSans-Regular，检查器对非 Office 默认字体给出可移植性提醒。未为消除提醒而替换官方字体；在其他电脑和会议共享环境仍应试播。工具模板中继承的 MiSans Medium 与嵌入字体保持原样。

## 当前代码界面截图

实际执行：

```bash
cmake -S gui/headless -B .debug/defense-20261031 \
  -DVG_HEADLESS_PROFILE=board -DVG_HEADLESS_COLOR_DEPTH=16
cmake --build .debug/defense-20261031 -j8
ctest --test-dir .debug/defense-20261031 --output-on-failure
```

6 项检查全部通过，总计 4.25 秒。截图来自 `alarm_check/07_alarm_ai_advice.ppm` 和 `fixture_check/04_fixture_trend_full_history.ppm`，以 Pillow 转为 PNG，无内容编辑。使用的 LVGL 是父工作区实际 checkout。它们说明实际 GUI 代码在模拟数据下的呈现，不作为真机采集、触摸、模型调用或长测证据。

## 表述范围

历史板测的单次时延、有限工况和主机模拟与整机长期验证分别说明。未沿用初赛 PPT 的 PR 数、测试数、AI 代码占比和旧资源预算。OTA 网络链路使用测试信任，真实安装和回滚来自独立分项，不将其拼成完整生产 OTA 已验收。

手机 PDF 与从机地址上传、手机语音仍按目标介绍。板端语音有实体按键和人声完整播放的历史样本，另有合成 WAV 回喂测试，备注分别指明来源。并发存储超时、诊断与语音并发以及长期稳定性继续保留待验边界。

本轮没有编译、烧录或操作真实开发板，没有更改点表、凭据和外部服务，没有 push、提交 PR 或上传主办方表单。PPT 的时间是编排目标，视频录制与完整彩排仍需完成。

提交前运行官方日志校验器，检查 324 个文件、77600 个事件，结果为带警告通过，退出码 1。唯一警告是 2026-09-16 一份 Claude 会话未列入 `manifest.sessions` 的既有问题，未改写该会话或日志清单。此结果不表示已完成主办方材料提交或日志推送。
