# Implement — 竞赛提交与公共仓 PR

> 执行前确认：`task.py start` 已批准。`gh auth login` 由用户完成（需浏览器）。

## Phase 0 — 前置与清单

- [ ] **0.1** `gh auth login` 并验证 `gh auth status`
- [ ] **0.2** 确认 CLA 状态（若曾 PR 过可跳过）
- [ ] **0.3** 创建 `submission-checklist.md` 跟踪全部 AC
- [ ] **0.4** 审计本地改动 vs patch 列表（见 design §2）

```bash
# 审计命令
cd $OPENVELA_ROOT/nuttx && git status && git diff --stat
cd $OPENVELA_ROOT/apps && git status && git diff --stat
cd $OPENVELA_ROOT/apps/netutils/mqttc/MQTT-C && git status && git diff --stat
cd $OPENVELA_ROOT/contest2026_004_TeamFalcons && git status
```

## Phase 1 — Fork 与分支

- [ ] **1.1** Fork 公共仓（若尚未 fork）：

```bash
gh repo fork open-vela/nuttx --clone=false
gh repo fork open-vela/nuttx-apps --clone=false
gh repo fork open-vela/apps_netutils_mqttc_MQTT-C --clone=false
```

- [ ] **1.2** nuttx：基于 upstream 建分支

```bash
cd $OPENVELA_ROOT/nuttx
git fetch openvela dev-ai-contest-2026
git checkout -b velaguard/stm32h750b-dk-support openvela/dev-ai-contest-2026
# 若本地改动在 detached HEAD，用 stash 或 cherry-pick 保留
git add -A   # 仔细排除无关文件
git status   # 人工确认
```

- [ ] **1.3** apps：同上，`velaguard/netinit-esp8266`
- [ ] **1.4** MQTT-C：同上，`velaguard/mqtt-pal-hook`
- [ ] **1.5** 为各仓添加 fork remote：

```bash
git remote add foleaf https://github.com/FoLeaf/<repo>.git
# 或 gh repo set-default
```

## Phase 2 — 公共仓 commit + push + PR（nuttx 5 PR）

本地分支已创建（基于 `openvela/dev-ai-contest-2026` + patch 内容）：

| PR | 分支 | commit |
|----|------|--------|
| N1 QSPI | `velaguard/qspi-boot-stm32h750b-dk` | done |
| N2 板级+defconfig | `velaguard/board-and-defconfigs` | done |
| N3 Ethernet | `velaguard/eth-mii-stm32h750b-dk` | done |
| N4 LTDC | `velaguard/display-acceleration-stm32h750b-dk` | done |
| N5 触摸 | `velaguard/ui-performance-stm32h750b-dk` | done |
| A1 apps | `velaguard/netinit-esp8266` | done |
| M1 MQTT-C | `velaguard/mqtt-pal-hook` | done |

集成分支：`nuttx/velaguard/integration`（本地 build 用）。

- [ ] **2.1** `gh auth login`
- [ ] **2.2** 运行一键脚本：

```bash
cd contest2026_004_TeamFalcons
bash scripts/push-upstream-prs.sh
```

- [ ] **2.3** 在 GitHub 为 N2/N3 添加 PR 依赖说明（N2 depends on N1 合入后 rebase 若冲突）
- [ ] **2.4** 记录 PR URL 到 `submission-checklist.md`

### 手动 push（若不用脚本）

```bash
# 示例：nuttx N1
cd $OPENVELA_ROOT/nuttx
git remote add foleaf https://github.com/FoLeaf/nuttx.git  # 首次
git push -u foleaf velaguard/qspi-boot-stm32h750b-dk
gh pr create --repo open-vela/nuttx --base dev-ai-contest-2026 \
  --head FoLeaf:velaguard/qspi-boot-stm32h750b-dk \
  --title "boards/stm32h750b-dk: add QSPI XIP boot support" \
  --body "Contest: contest2026_004 Team Falcons / VelaGuard"
# 对其余 6 个分支重复
```

## Phase 3 — 构建脚本迁移（可在 PR 待审期间并行）

- [ ] **3.1** 在 contest 仓新建分支 `chore/drop-nuttx-patches`（或合入 learn_vela）
- [ ] **3.2** 验证无 patch 构建：

```bash
# 临时注释 build.sh 中 apply_target_patches 调用后：
bash scripts/build.sh min
bash scripts/build.sh net
```

- [ ] **3.3** PR 合入后删除：

```text
scripts/openvela-*.patch
scripts/apply-openvela-*-patch.sh
```

- [ ] **3.4** 更新 `docs/velaguard-bringup-known-issues.md`：patch 表 → PR 链接表
- [ ] **3.5** 若 PR 未合入：README 写 pin fork 说明，保留 patch 至合入日

## Phase 4 — 专属仓最终提交

- [ ] **4.1** 整理 `learn_vela` 变更：排除 Trellis 模板噪声（按需分拆 commit）
- [ ] **4.2** 验证 AI 日志：

```bash
python3 ../.claude/skills/contest-log-collector/tools/validate-log.py logs/
```

- [ ] **4.3** 更新根 `README.md`（评委向，见 design §7）
- [ ] **4.4** commit + push

```bash
cd contest2026_004_TeamFalcons
git push foleaf learn_vela
gh pr create --repo open-vela/contest2026_004_TeamFalcons \
  --base dev-ai-contest-2026 \
  --head FoLeaf:learn_vela \
  --title "feat(velaguard): final contest submission" \
  --body "..."
```

- [ ] **4.5** 自行 review 合入 PR
- [ ] **4.6** 提醒仓外交付物：作品介绍 + 演示视频

## Phase 5 — 验证门禁

| 检查 | 命令 |
|------|------|
| min 构建 | `bash scripts/build.sh min` |
| net 构建 | `bash scripts/build.sh net` |
| 日志校验 | `validate-log.py logs/` |
| checklist | 全部 AC 打勾 |

## Rollback

- 公共仓：关闭 PR，删除 fork 分支；本地恢复 patch apply 路径
- 专属仓：`git revert` 或关闭 PR 不合入
- 勿 `git push --force` 到 `dev-ai-contest-2026`

## 人工决策点（实施前确认）

1. nuttx 单 PR vs 多 PR → 默认单 PR（design §3）
2. PR 待审期间是否保留 patch fallback → 默认保留至合入
3. contest 分支名：`learn_vela` 还是新建 `contest-final` → 与用户确认
