# 竞赛最终提交整理与 NuttX 公共仓 PR 迁移

## Goal

在 **2026-09-20** 截止前，按 openvela 竞赛官方规则完成 VelaGuard 全部可提交物整理；将当前 `scripts/*.patch` 工作流迁移为 **直接在公共仓修改 → fork → PR 到 `dev-ai-contest-2026`**，并用 GitHub CLI 完成 commit / push / PR 编排。

## Background

- 竞赛产品代码只能在 `contest2026_004_TeamFalcons/` 内开发（manifest `<linkfile>` 映射到 openvela 树）。
- **nuttx / apps 等公共仓**不允许作为日常提交路径；需 fork 对应仓库并向 `dev-ai-contest-2026` 提 PR，由组委会 review 合入。
- 当前 VelaGuard 在 nuttx/apps/MQTT-C 侧已有 patch 固化 + `build.sh` 自动 apply；本地工作区已存在未提交改动，用户希望**弃用 patch**，改为公共仓直改 + PR。
- `gh` 当前未登录，需先完成认证与 fork 准备。

## Requirements

### R1 — 竞赛提交物清单（官方）

| # | 交付物 | 位置 / 方式 | 当前状态 |
|---|--------|-------------|----------|
| 1 | 作品代码 | `FoLeaf/contest2026_004_TeamFalcons` → PR 合入 `dev-ai-contest-2026` | `learn_vela` 分支 41+ 未提交变更 |
| 2 | AI Coding 日志 | `logs/Foleaf/*.jsonl` 随仓提交 | 已有 ~50 个会话文件 |
| 3 | 作品 README | 根 `README.md`：产品名、赛道、构建/运行、简介 | 现为 Mermaid 手册，需补充评委向说明 |
| 4 | nuttx 公共仓改动 | fork `open-vela/nuttx` → PR `dev-ai-contest-2026` | 本地 15+ 文件已改，未 fork/PR |
| 5 | apps 公共仓改动 | fork `open-vela/nuttx-apps` → PR `dev-ai-contest-2026` | netinit 4 文件已改，未 fork/PR |
| 6 | MQTT-C 公共仓改动 | fork `open-vela/apps_netutils_mqttc_MQTT-C` → PR | `mqtt_pal.c` 已改，未 fork/PR |
| 7 | 作品介绍文档 | .docx / .pdf / .pptx（仓外） | 待用户准备 |
| 8 | 演示视频 | ≤5 分钟 mp4/mov（仓外） | 待用户准备 |
| 9 | CLA | 首次 PR 前签署 | 按需 |

### R2 — AI 硬件赛道最低证据（BOUNDARY C8–C9）

- [ ] 固件在 STM32H750B-DK（或支持板型）上可构建/运行
- [ ] ≥1 自定义 Skill（`/data/agent/skills/`）+ 演示
- [ ] ≥1 主动+执行场景（非纯问答）+ 书面说明
- [ ] 使用 openvela 系统能力，落地图形 / AI / 多媒体至少一项

### R3 — NuttX 侧 patch 迁移范围

将以下 patch 内容合并为 nuttx 公共仓 PR（按逻辑分组，见 `design.md`）：

| Patch 文件 | 主要内容 |
|------------|----------|
| `openvela-qspi-boot-stm32h750b-dk.patch` | QSPI 启动、MPU、linker |
| `openvela-velaguard-board-pins.patch` | 扩展板 pinmux / bringup / PWM |
| `openvela-pwm-tim15-fix.patch` | TIM15 CH2 守卫笔误修复 |
| `openvela-velaguard-min-defconfig.patch` | `velaguard-min` 预设 |
| `openvela-velaguard-net-defconfig.patch` | `velaguard-net` 预设 |
| `openvela-velaguard-net-esp8266.patch` | net defconfig ESP8266 选项 |
| `openvela-eth-mii-stm32h750b-dk.patch` | 以太网 MII/PHY |
| `openvela-display-acceleration-stm32h750b-dk.patch` | LTDC 显示加速 |
| `openvela-ui-performance-stm32h750b-dk.patch` | 触摸/UI 性能 |
| `openvela-netinit-carrier-poll.patch` | **apps** netinit carrier/DHCP |
| `openvela-esp8266-lesp-compat.patch` | **apps** ESP8266 宏兼容 |
| `openvela-mqttc-pal-hook.patch` | **MQTT-C** pal hook |

### R4 — 构建脚本迁移

- `scripts/build.sh` 在公共仓 PR 合入前：可保留 patch apply 作为 fallback。
- PR 合入后：移除对应 patch + apply 脚本，`build.sh` 直接依赖上游/defconfig。
- 文档 `docs/velaguard-bringup-known-issues.md` 更新为 PR 引用，不再写「必须 patch」。

### R5 — GitHub 操作约束

- 使用报名时 GitHub 账号（`Foleaf`）操作 fork / PR。
- 专属仓：fork → commit → push → PR → 自行 review 合入。
- 公共仓：fork → commit → push → PR 到 `open-vela/<repo>` 的 `dev-ai-contest-2026`。
- 不 force-push `main`/`dev-ai-contest-2026`；不篡改 AI 日志 JSONL 内容。

## Out of Scope

- 仓外作品介绍文档与演示视频的制作（仅提醒截止时间）。
- 获奖后向上游 openvela 二次 PR（截止后另议）。
- 修改 openvela 树内 `packages/`、`vendor/` 等未映射目录（产品代码走 contest 仓 linkfile）。

## Acceptance Criteria

- [ ] **AC1** `gh auth status` 通过，`FoLeaf` 已 fork `nuttx`、`nuttx-apps`、`apps_netutils_mqttc_MQTT-C`（或等价远程已配置）。
- [ ] **AC2** nuttx PR 已创建，包含全部 VelaGuard 板级/defconfig/驱动改动，目标分支 `dev-ai-contest-2026`，PR 描述含构建验证命令。
- [ ] **AC3** apps PR 已创建（netinit + esp8266），目标 `dev-ai-contest-2026`。
- [ ] **AC4** MQTT-C PR 已创建（mqtt_pal hook），目标 `dev-ai-contest-2026`。
- [ ] **AC5** 三仓 PR 合入后（或 PR 待审期间 README 注明 fork 分支 pin），`bash scripts/build.sh` 与 `min` 在无 patch apply 情况下构建通过。
- [ ] **AC6** contest 仓 `learn_vela`（或约定分支）已 commit + push + PR 合入 `dev-ai-contest-2026`，含产品代码、`logs/Foleaf/`、更新后 README。
- [ ] **AC7** 提交清单文档（任务内 `submission-checklist.md`）逐项打勾，含仓外交付物提醒。
- [ ] **AC8** 已弃用或归档的 patch 脚本有明确说明，不会与 PR 内容双轨漂移。

## Open Questions

1. nuttx/apps PR 是否拆分为多个（板级 / 网络 / UI）以利 review，还是单 PR？
2. PR 未合入前，评委复现是否 pin 到 `FoLeaf/nuttx@<branch>` 并在 README 写明？
3. MQTT-C hook 是否接受上游合入，还是 contest 仓 vendor 兜底？

## Notes

- 官方来源：`docs/zh-cn/contest_2026/code_submission_guide.md`、`contest_overview.md`；本地 `docs/agents/BOUNDARY.md`。
- 当前 nuttx 处于 detached HEAD；实施前需基于 `dev-ai-contest-2026` 建 feature 分支。
- `gh` 未登录是当前阻塞项，Phase 2 第一步处理。
