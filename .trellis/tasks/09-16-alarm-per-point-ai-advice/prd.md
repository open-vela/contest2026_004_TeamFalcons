# 告警页逐点 AI 建议

## Goal

告警页每条活动告警都显示 OPENVELACLAW 给出的一行短建议；选中某条时，详情区展示该点的完整解释（摘要 / 依据 / 建议关注），并明确标注这是 AI 推测。离线、生成中、生成失败时退回既有的确定性规则摘要。

## Requirements

- **R1** 每条活动告警行的第二行显示短建议，前缀 `AI · `，内容来自 board 校验通过的缓存。
- **R2** 选中行在详情区展示完整解释，头部标注来源为 OPENVELACLAW，并附 AI 推测提示。
- **R3** 建议按 `(sensor_id, al_epoch)` 命中；同一测点重新起一轮告警时旧建议不得命中。
- **R4** 未命中、在途、失败、离线时行内与详情都退回规则摘要（本地），并标注 AI 建议生成中或不可用。任何情况下都不合成 AI 文本。
- **R5** 页面每 tick 只读内存缓存，不做文件 IO、不发请求、不阻塞；对标签的写入要做去重，避免 1 s tick 反复重设文本。
- **R6** 行结构重建条件保持只依赖 id 与严重级，不因建议变化触发重建。
- **R7** PC 模拟器路径不受影响。

## Acceptance Criteria

- [ ] headless：给定带 `(boot, id, epoch)` 的预制建议文档，主告警行文本以 `AI · ` 开头，详情头含 `OPENVELACLAW`，未命中的第二行仍是规则文本。
- [ ] headless：保留旧文档但 bump `al_epoch`，两行都降级，详情头为规则摘要（本地）。
- [ ] 板测：注入至少 2 条告警后，行内出现 `AI · ` 短建议，选中行详情头含 `OPENVELACLAW`。
- [ ] 板测：断网后重新注入告警，行内与详情都回到规则摘要（本地），全程无伪造 AI 文本。
- [ ] 板测：同一测点重新起一轮告警时旧建议不再出现。
- [ ] `bash scripts/build.sh` 通过，`scripts/stage1_lvgl_hmi_accept.ps1` 通过。
- [ ] 收尾按 `velaguard-board-inner-loop` 内环跑完并汇报。

## 依赖

- 依赖子任务 A 的 `vg_ai_contract` 与请求通道；A 未完成时本任务只做页面与 mock 侧，不自行发请求。

## Notes

- 详细设计见 `design.md`，执行顺序见 `implement.md`。
