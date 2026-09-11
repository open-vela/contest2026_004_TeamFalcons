# Design — 竞赛提交与公共仓 PR 迁移

## 1. 提交拓扑

```text
┌─────────────────────────────────────────────────────────────────┐
│  仓外（邮件/表单）                                                │
│  · 作品介绍 .docx/.pdf/.pptx                                     │
│  · 演示视频 ≤5min                                                │
│  · 专属仓 URL                                                    │
└─────────────────────────────────────────────────────────────────┘

┌──────────────────────────┐     PR → dev-ai-contest-2026
│ FoLeaf/contest2026_004   │ ──────────────────────────────► open-vela/contest2026_004_TeamFalcons
│  · app/velaguard         │     （自行 review 合入）
│  · board/ quickapp/      │
│  · scripts/ (build 入口) │
│  · logs/Foleaf/          │
│  · README.md             │
└──────────────────────────┘

┌──────────────────────────┐     PR → dev-ai-contest-2026
│ FoLeaf/nuttx             │ ──────────────────────────────► open-vela/nuttx
│  · stm32h750b-dk 板级    │     （组委会 review）
│  · velaguard-min/net     │
│  · QSPI / ETH / LTDC     │
└──────────────────────────┘

┌──────────────────────────┐     PR → dev-ai-contest-2026
│ FoLeaf/nuttx-apps        │ ──────────────────────────────► open-vela/nuttx-apps
│  · netinit carrier poll  │
│  · esp8266 LESP compat   │
└──────────────────────────┘

┌──────────────────────────┐     PR → dev-ai-contest-2026
│ FoLeaf/apps_netutils_    │ ──────────────────────────────► open-vela/apps_netutils_mqttc_MQTT-C
│   mqttc_MQTT-C           │
│  · mqtt_pal hook         │
└──────────────────────────┘
```

manifest linkfile（不变）：

```xml
<linkfile src="app/velaguard" dest="packages/demos/contest2026_004_hello_app"/>
<linkfile src="quickapp/hello_quickapp" dest="packages/apps/contest2026_004_hello_quickapp"/>
<linkfile src="board/contest_board" dest="vendor/openvela/boards/contest2026_004_board"/>
```

## 2. 本地工作区现状（2026-08-28 审计）

### nuttx（`open-vela/nuttx`）

| 状态 | 路径 |
|------|------|
| 已修改 | `arch/arm/src/stm32h7/{stm32_allocateheap,stm32_ethernet,stm32_ltdc,stm32_mpuinit,stm32_pwm}.c` |
| 已修改 | `boards/.../stm32h750b-dk/{Kconfig,board.h,Make.defs,src/*}` |
| 已修改 | `boards/Kconfig`, `drivers/input/ft5x06.c` |
| 未跟踪 | `configs/velaguard-min/`, `configs/velaguard-net/`, `scripts/qspi_flash.ld`, `src/stm32_pwm.c` |
| 分支 | detached HEAD；remote 仅 `openvela` |

### apps（`open-vela/nuttx-apps`）

| 状态 | 路径 |
|------|------|
| 已修改 | `include/netutils/netinit.h`, `netutils/netinit/*`, `netutils/netlib/netlib_setdripv4addr.c` |
| 忽略 | `testing/drivers/nist-sts/` 下无关 untracked（不纳入 PR） |

### MQTT-C

| 状态 | 路径 |
|------|------|
| 已修改 | `src/mqtt_pal.c`（vg_mqtt_pal hook） |

## 3. PR 分组策略

**已确认：nuttx 拆成多个 PR**（共 **5 个 nuttx PR** + apps + MQTT-C + 专属仓）。

合并顺序建议：N1 → N2 → N3（net 路径）；N4/N5 仅 LVGL 路径需要，可与 N2 并行 review。

### PR-N1：`open-vela/nuttx` — QSPI XIP boot（`velaguard/qspi-boot-stm32h750b-dk`）

| 来源 patch | 说明 |
|------------|------|
| `openvela-qspi-boot-stm32h750b-dk.patch` | MPU、linker、boards Kconfig |

### PR-N2：`open-vela/nuttx` — VelaGuard 板级与 defconfig（`velaguard/board-and-defconfigs`）

| 来源 patch | 说明 |
|------------|------|
| `openvela-velaguard-board-pins.patch` | USART2/RS485/ESP/TIM15 pinmux |
| `openvela-pwm-tim15-fix.patch` | TIM15 CH2OUT 守卫笔误 |
| `openvela-velaguard-min-defconfig.patch` | `velaguard-min` preset |
| `openvela-velaguard-net-defconfig.patch` | `velaguard-net` preset |
| `openvela-velaguard-net-esp8266.patch` | net defconfig ESP8266 选项 |

> 依赖 N1（QSPI linker 被 defconfig 引用）。PR 描述中注明 **depends on #N1**。

### PR-N3：`open-vela/nuttx` — Ethernet MII（`velaguard/eth-mii-stm32h750b-dk`）

| 来源 patch | 说明 |
|------------|------|
| `openvela-eth-mii-stm32h750b-dk.patch` | RJ45 PHY 轮询 |

> `board.h` 与 N2 有重叠 hunk；分支基于 `dev-ai-contest-2026`，合入顺序 N2 先于 N3，或 rebase 后提交。

### PR-N4：`open-vela/nuttx` — LTDC 显示加速（`velaguard/display-acceleration-stm32h750b-dk`）

| 来源 patch | 说明 |
|------------|------|
| `openvela-display-acceleration-stm32h750b-dk.patch` | LVGL 帧缓冲加速 |

### PR-N5：`open-vela/nuttx` — 触摸性能（`velaguard/ui-performance-stm32h750b-dk`）

| 来源 patch | 说明 |
|------------|------|
| `openvela-ui-performance-stm32h750b-dk.patch` | ft5x06 轮询优化 |

### PR-A1：`open-vela/nuttx-apps` — netinit + ESP8266（`velaguard/netinit-esp8266`）

| Commit 主题 | 来源 patch |
|-------------|-----------|
| `netutils/netinit: carrier detect and DHCP renew polling` | netinit-carrier-poll |
| `netutils/esp8266: LESP_* macro compatibility` | esp8266-lesp-compat |

### PR-M1：`open-vela/apps_netutils_mqttc_MQTT-C` — PAL hook for LESP transport

| Commit 主题 | 来源 patch |
|-------------|-----------|
| `mqtt_pal: weak hooks for tagged LESP send/recv` | mqttc-pal-hook |

### PR-C1：`FoLeaf/contest2026_004_TeamFalcons` — final submission

- 产品代码增量（`app/`, `board/`, `quickapp/`）
- `logs/Foleaf/` 最新会话
- README 评委向说明 + 公共仓 PR 链接
- `build.sh` 更新（PR 合入后去 patch）
- `submission-checklist.md`

## 4. Patch 弃用策略

```text
Phase A（PR 待审）          Phase B（PR 合入后）
─────────────────          ───────────────────
build.sh 仍 apply patch    删除 patch + apply-*.sh
README 注明 PR 链接        manifest/repo 指向上游
本地开发可 pin fork        build.sh 仅 configure + make
```

**幂等迁移检查**：对每个 patch，在 nuttx/apps 树执行 `git diff` 与 patch 内容比对，确认无遗漏后再删脚本。

## 5. 复现路径（评委 / 自测）

### 方案 A — PR 已合入 upstream

```bash
repo sync -b dev-ai-contest-2026
cd nuttx && tools/configure.sh -e stm32h750b-dk:velaguard-net
cd ../contest2026_004_TeamFalcons && bash scripts/build.sh
```

### 方案 B — PR 待审（pin fork）

```bash
# .repo/manifest 或 local_manifest 将 nuttx project revision 改为 FoLeaf/nuttx:<branch>
repo sync
# README 写明分支名与 PR URL
```

## 6. 风险与缓解

| 风险 | 缓解 |
|------|------|
| `gh` 未登录 | 实施第一步 `gh auth login` |
| detached HEAD 丢改动 | 先 `git checkout -b velaguard/upstream-pr` 再 commit |
| patch 与 working tree 漂移 | `git diff` 对照每个 patch；以 working tree 为准 |
| MQTT-C 上游不接受 hook | contest README 记录 fork pin；或 Kconfig 可选弱符号 |
| 双轨（patch + PR）漂移 | PR 合入后立即删 patch；CI 用无 patch 构建验证 |
| apps 无关 untracked 污染 PR | `git add -p` 精确暂存；排除 nist-sts |

## 7. 专属仓 README 最小结构（评委向）

1. 产品名 / 赛道（AI 硬件产品创新）
2. 硬件：STM32H750B-DK + 扩展板
3. 构建：`bash scripts/build.sh`（注明 openvela 根目录）
4. 公共仓 PR 链接表格（nuttx / apps / MQTT-C）
5. Skill + 主动场景简述 + 演示步骤
6. AI 日志位置 `logs/Foleaf/`
