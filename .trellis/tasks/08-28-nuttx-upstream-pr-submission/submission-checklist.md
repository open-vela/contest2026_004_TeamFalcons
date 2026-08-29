# VelaGuard 竞赛提交清单

> 截止：**2026-09-20**。完成后逐项打勾。

## 仓内（GitHub）

### 专属仓 `contest2026_004_TeamFalcons`

- [ ] 产品代码（`app/velaguard`, `board/`, `quickapp/`）已 push
- [x] `logs/Foleaf/` 已提交且 `validate-log.py` 通过（⚠️ 含 Cursor/Grok orphan 警告）
- [x] `README.md` 含产品说明、构建步骤、公共仓 PR 链接
- [ ] PR → `dev-ai-contest-2026` 已合入

### 公共仓 PR（nuttx 拆 5 个）

| 仓库 | 分支 | PR | 状态 |
|------|------|-----|------|
| open-vela/nuttx | `velaguard/qspi-boot-stm32h750b-dk` | https://github.com/open-vela/nuttx/pull/350 | ✅ CI 全绿 |
| open-vela/nuttx | `velaguard/board-and-defconfigs` | https://github.com/open-vela/nuttx/pull/351 | ✅ CI 全绿 |
| open-vela/nuttx | `velaguard/eth-mii-stm32h750b-dk` | https://github.com/open-vela/nuttx/pull/352 | ✅ CI 全绿 |
| open-vela/nuttx | `velaguard/display-acceleration-stm32h750b-dk` | https://github.com/open-vela/nuttx/pull/353 | ✅ CI 全绿 |
| open-vela/nuttx | `velaguard/ui-performance-stm32h750b-dk` | https://github.com/open-vela/nuttx/pull/354 | ✅ CI 全绿 |
| open-vela/nuttx-apps | `velaguard/netinit-esp8266` | https://github.com/open-vela/nuttx-apps/pull/119 | ✅ CI 全绿 |
| open-vela/apps_netutils_mqttc_MQTT-C | `velaguard/mqtt-pal-hook` | https://github.com/open-vela/apps_netutils_mqttc_MQTT-C/pull/1 | ✅ CI 全绿 |

## PR Review 状态（2026-08-29）

### 贡献者必过项（全部 7 PR）

| 检查 | 状态 |
|------|------|
| checkpatch | 7/7 pass |
| CLA / cla-check | 7/7 pass |
| ci / setup | 7/7 pass |
| ci-tasks 矩阵（5 平台） | 7/7 pass |
| post-processing | 7/7 pass |
| commit 邮箱 | 全部 `id19y@outlook.com` |

### 已修复

| PR | 问题 | 修复 |
|----|------|------|
| 全部 | CLA 失败（`hello19y@local`） | amend 作者 + force-push |
| #351 | checkpatch: 行长 + stm32_pwm.c 缺文件头 | force-push |
| #119 | CI 多平台 `#ifdef` / unused 符号 | netinit.c 条件编译 |
| #352 | cherry-pick 双 commit 失败 | squash 为单 commit |

## 仓外

- [ ] 作品介绍文档（.docx / .pdf / .pptx）
- [ ] 演示视频（≤5 分钟）
- [ ] 专属仓地址已提交组委会

## AI 硬件赛道证据

- [ ] 设备/板型上固件可运行
- [ ] ≥1 自定义 Skill + 演示
- [ ] ≥1 主动+执行场景 + 书面说明
- [ ] 图形 / AI / 多媒体至少一项落地

## 工具链

- [x] `gh auth status` OK
- [x] CLA 已签 + `/check-cla` 通过（7/7 PR）
- [ ] `bash scripts/build.sh min` 通过
- [ ] `bash scripts/build.sh net` 通过
