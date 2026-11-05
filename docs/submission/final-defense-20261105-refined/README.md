# VelaGuard 决赛答辩完善版

2026-11-05。按用户确认的两轮 grilling 结果完成：保留原稿视觉，以离线网关工程实现为主线，更新候选接入成果，主讲与备用细节分开。

## 交付文件

| 文件 | 用途 |
|---|---|
| VelaGuard-决赛答辩-完善版-CODEX.pptx | 14 页主讲、5 页隐藏备用，含 19 页口播备注 |
| 逐页口播稿-CODEX.md | 逐页时间、主讲口播与备用页说明 |
| 80秒视频脚本-CODEX.md | 待录制视频的分镜、真实耗时和本机确认要求 |
| 逐页证据与表述校正.md | 实现状态、指标口径和逐页来源 |
| 全稿预览-CODEX.png | PowerPoint 渲染的全稿缩略图 |
| build_revision.py | 对原生 PPTX 导入工作区实施修改的源码 |
| revision_content.json、page_plan.json、notes/ | 页面映射、内容、时间及口播源文件 |

计划口播 370 秒、视频 80 秒，共 7 分 30 秒，余 30 秒用于停顿和切页。第 12 页目前明确标为待录制，不含视频或自动播放音频。时间是排练预算，需在嵌入视频后按本人语速试讲。

本机另存于 `F:\Documents\velaguard作品提交\决赛答辩-20261105-CODEX完善版\`。用户提供的 Claude 原稿保持不变，封面、结束页、品牌与模拟界面资产沿用原来源。

## 页面安排

1. 产品定位
2. 小机房的一天
3. 本地与联网任务
4. 采集与两种配置入口
5. 手册辅助接入真机联调
6. Agent 主动取证、解释与日报
7. C 代码权限边界
8. 断网行为
9. RS485 与 TLS 工程问题
10. OTA 机制和验证范围
11. 测试结果与条件
12. 80 秒待录制视频位
13. 当前成果与现场验证计划
14. 结束
15. 备用 A：任务与历史资源
16. 备用 B：权限矩阵
17. 备用 C：Agent 时序与产物校验
18. 备用 D：故障定位
19. 备用 E：OTA 参数

原稿独立行业扩展页并入第 13 页的现场验证计划。第 15–19 页隐藏，普通放映在第 14 页结束；需要时按页码跳转。

## 再生成

需要用户提供的原稿以及已安装的 ppt-master。原稿 SHA-256 为 `f379f8f74595f16420e4aef4c143d8afc9091ac3b9fda8dc9e31fe6e23fe382e`；本机来源路径为 `F:\Documents\velaguard作品提交\决赛答辩-20261105-CLAUDE\VelaGuard-决赛答辩-技术硬核版-CLAUDE.pptx`。

先用 `pptx_to_svg.py <原稿.pptx> -o <新工作区> --inheritance-mode both --roundtrip` 导入，再运行本目录的 `python build_revision.py <新工作区>`。脚本把首次导入的页面保存在自己的 `reference-svg/` 中，后续修改可重复生成，避免把上一轮编辑结果当作原稿。

之后依次执行 ppt-master 的 `svg_authoring_view.py <新工作区>/authoring-svg-flat --refresh-summary`、`svg_quality_checker.py <新工作区> --roundtrip --json` 和 `svg_to_pptx.py <新工作区> --roundtrip --primary-language zh-CN`。

用 PowerPoint 对独立输出执行 SaveAs 并嵌入字体，把第 15–19 页设为隐藏。发布前执行 PPTX 结构与版式检查、导入检查，并逐页查看 PowerPoint 渲染。本版保留 MiSans、Microsoft YaHei 和 Consolas，正文、流程图和权限矩阵均为可编辑对象。

本次只修改答辩材料，没有构建、烧录或对板端执行配置操作；PPT 中的测试结果引用已有证据。开发日志由采集器独立维护，未把其他会话的日志变更纳入本次提交。
