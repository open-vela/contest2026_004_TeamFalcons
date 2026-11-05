# 决赛答辩技术硬核版：行业场景页

2026-11-05。基于 `final-defense-20261105-claude-v2` 工作区，只替换第 12 页：
原证据台账改为行业应用场景解读。其余 15 页未改动。

## 内容

| 文件 | 说明 |
|---|---|
| `deckgen.py` | 16 页 SVG 生成器，输出 2560×1440 页面 |
| `notes/` | 逐页备注，导出时写入 PPTX 备注 |
| `逐页口播稿.md` | 16 页口播与时间分配，净讲述 375 秒加 80 秒视频 |
| `制作归属-CLAUDE.md` | 来源、页面顺序、证据口径 |
| `全稿预览-CLAUDE-行业场景页.png` | PowerPoint 渲染的 16 页缩略图 |

## 第 12 页

标题「从一间机房，到一类现场」。左栏四类现场，右栏现场的四项要求，底部推广路径。
小型机房与设备间是当前验证场景；配电室、泵房、冷库与仓储标为同一形态的延伸。
页面不写市场规模、节省工时或投资回报数字。

原证据表的数字在其余页面已有对应：四档回复延迟与告警解释耗时在第 13 页，
OTA 下载在第 10 页，安装与回滚在第 11 页，界面测试与长测状态在第 15 页。

## 重新生成

工作区 `F:\Documents\velaguard作品提交\build\final-defense-20261105-claude-v3-industry`：

```bash
python3 deckgen.py
```

再用 ppt-master 的 `svg_to_pptx.py` 以 `authoring-svg-flat` 导出 flat 结构，
`--primary-language zh-CN`，带 notes；最后用 `embed_fonts.ps1` 嵌入 MiSans 字体。
