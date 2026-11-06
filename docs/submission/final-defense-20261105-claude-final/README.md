# 决赛答辩技术硬核版：最终源文件

2026-11-05。基于 `final-defense-20261105-claude-v2` 工作区导出的 16 页版本，页序与 `决赛答辩-20261105-CLAUDE/` 中的 PPTX 一致。

## 相对 `final-defense-20261105-claude-industry/` 的改动

- 第 4 页合并采集链路与点表闸门，第 5 页改为写入权限矩阵。
- 第 10、11 页的 OTA 内容合并为一页，空出的页改为三个技术难点。
- 第 12 页由证据矩阵改为行业场景，四个场景与底部共同点。

## 文件

| 文件 | 说明 |
|---|---|
| `deckgen.py` | 16 页 SVG 生成器，输出 2560×1440 页面 |
| `notes/` | 逐页备注，导出时写入 PPTX 备注 |
| `逐页口播稿.md` | 16 页口播与时间分配，净讲述 381 秒加 80 秒视频，合计 461 秒 |
| `答辩文案.md` | 逐页讲述、翻页过渡、问答口径和表述红线 |
| `制作归属-CLAUDE.md` | 来源、页面顺序、证据口径 |
| `全稿预览-CLAUDE.png` | PowerPoint 渲染的 16 页缩略图 |

## 重新生成

工作区 `F:\Documents\velaguard作品提交\build\final-defense-20261105-claude-v2`：

```bash
python3 deckgen.py
```

再用 ppt-master 的 `svg_to_pptx.py` 以 `authoring-svg-flat` 导出 flat 结构，带 notes；最后用 PowerPoint 的 `SaveAs($Dst, 24, -1)` 嵌入 MiSans 字体。
