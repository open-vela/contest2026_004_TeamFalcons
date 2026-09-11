# 实施计划：阶段 1 剩余闭环

> 父任务不写产品代码。6 个子任务已归档。本目录停工，勿 `task.py start`。  
> 9/20 下一步：`09-09-demo-threshold-alarm`（父任务 `09-09-pre-920-score-play`）。

## 0. 规划卫生（本步，无固件）

- [x] 父 PRD 对齐 2026-09-01 板测事实
- [x] 父 design / implement 写剩余闭环
- [x] 勾选并归档 `08-25-agent-capability-rescope`（文档已落地）
- [x] 归档 `08-30-stage1-modbus-discovery`（NSH AC 全绿；D3 父勾选已在本 PRD）

## 1. HMI 板端剩余（`08-30-stage1-lvgl-hmi`）

- [x] C1 目视：裁剪首页、扫描开关默认关（2026-09-09 NSH：无自动 scan）
- [x] C2 探查页 ON → scan → 列表 ≥1（MThings mock @9600）
- [x] C3 屏上 confirm → `vgcfg dump` / `points.json`（代码+NSH 证据；屏上点按待目视）
- [x] C4 首页出现刚确认的从站（非 mock 24 路；冷启动 `fleet n=32`）
- [x] C5 报告页：有日报则摘要，无则 empty 态
- [x] C6 告警页可见「AI 推测」区块（占位或 pending 文件）
- [x] C7 扩展 `stage1_lvgl_hmi_accept.ps1` — 2026-09-09 **14/14 PASS**

**验证**

```bash
bash scripts/build.sh velaguard-lvgl
powershell.exe -File scripts/flash.ps1
powershell.exe -File scripts/stage1_lvgl_hmi_accept.ps1
# 目视探查/确认；nsh> vgcfg dump
```

## 2. Agent 与 HMI（Q1=B：尽力，不阻塞出口）

- [x] D1（尽力）HMI 已跑：`ai_agent &` → assert（笔记已记；保持 HMI skip autostart）
- [ ] D2（非出口）恢复 `VG_AGENT_AUTOSTART`

另：`net` 单固件 agent 启动 panic 已修（mallinfo）；`ai_agent --daemon &` + attach 可进 `vela>`。autostart 仍关，待网络稳定后再开。

## 3. CLI 查数板测（`velaguard-net`）

- [x] E1 `vela> ask` 查从站读数 — **2026-09-02 板测**：`stage1_agent_accept.ps1` **6/8 PASS**（`vela>` + `ask response` + `persist write`）；Wi-Fi 已 `vgnet: joined ASUS`；剩余 **net_test TLS/HTTP 2 项**（非 ask 主路径）

## 4. 演示 MVP（已移交，本父任务不要再平行开工）

2026-09-10 起点表改由上位机写入，不再用 CSV 编译期 codegen。F1 / 手册对照 / 提交材料由 `09-09-pre-920-score-play` 承接。本父任务保持 planning，等 9/20 闭环后再归档。

推进方案 §10「最小可演示闭环」仍有效：**本地告警必须看得见**。执行清单见子任务，不要在这里再写一套：

- F1a/F1b → `09-09-demo-threshold-alarm`（下一步）
- F1c 同机 Agent → `09-09-samefw-agent-spike`（告警板测通过后再开）
- 提交材料 → `09-09-judge-submit-pack`

演示点表由上位机 JSON 下发（规范第 4 节 temp/flood 一类），不写进固件。

## 5. 父出口

对照手册与归档仍等 `09-09-pre-920-score-play` 的告警板测 + 评委包有结论后再做。不要为了清待办提前归档本父任务。

- [ ] 对照手册 §14.1 逐条（演示子集；未做项标明阶段）— 执行在 `09-09-judge-submit-pack`
- [ ] 日报 §14.2 沿用 2026-08-30 证据；写操作 = 0；告警解释比例未板测则笔记跳过
- [ ] `task.py archive 08-30-stage1-min-product`（9/20 闭环后；**不要**在告警上屏完成前归档）

## 6. 明确不做（已另立待办）

| 项 | 去向 |
|----|------|
| NSH `vgpoint` / COM3 上位机加点改阈值 | 编码已在 `09-10-host-nsh-vgpoint`；告警比较在 `09-09-demo-threshold-alarm` |
| Windows 配置 GUI、屏上编辑点表 | 9/20 后 |
| HMI `VG_AGENT_AUTOSTART` | D2，非出口；同机试验见 `09-09-samefw-agent-spike` |
| 阶段 2 规则库、阶段 3 OTA、Bridge | 手册已裁出 9/20 |

## Rollback

- 修同一套 `velaguard-lvgl`；开机仍不自动启动 Agent
- HMI confirm 只写 inactive slot；`vgcfg damage` 可回 factory
