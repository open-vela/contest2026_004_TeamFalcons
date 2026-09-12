# Agent 开机自启与部署规范更新

## Goal

用户裁定：ai_agent 应当开机自启。此前规范要求 HMI 起来后手动 `ai_agent --daemon`，导致复位/上电后心跳不跑、日报停留在旧文件（板上实测只剩 daily-20260228.md）。

## Requirements

1. `velaguard.c`：删除 HMI 场景下跳过 Agent 自启的分支；自启顺序调整为 NSH → net_mgr → HMI task → 3s 后 `ai_agent --daemon`（保留 eMMC/net settle 的 sleep(3)，Agent 自身会重试网络）。
2. `CONFIG_VG_AGENT_AUTOSTART=y`：写入 `scripts/configs/velaguard-lvgl.defconfig` 与运行 `.config`；Kconfig help 更新（自启含 HMI 场景）。
3. 部署规范 `.trellis/spec/backend/firmware-mainline.md` 更新 boot 链：不再指导手动拉起；注明重跑 `--daemon` 有防重护栏。
4. README 演示命令、demo-video-script 前置说明、plan-917-submission 板端清单同步更新。

## Constraints / Non-goals

- 不改 ai_agent 本体（packages/ai_agent，含防重护栏与心跳实现，只读依赖）。
- 验收脚本 `stage1_agent_accept.ps1` 不改：其 `ai_agent --daemon &` 步骤依赖防重护栏，自启后仍安全。

## Acceptance Criteria

- [x] `bash scripts/build.sh` 编译通过
- [x] 板上 boot 日志出现 `vgagent: ai_agent autostart ok`，ps 可见 ai_agent（SWD 复位实测，daemon PID 带工作线程常驻）
- [ ] 心跳消化 pending_alarm.txt 并生成 daily-20260912.md —— 被新任务 09-12-heartbeat-llm-round-system-wedge 阻塞：心跳 LLM 轮次静默挂死（见该任务 PRD），自启本身已验证通过

## 已知现象（记录）

- 经串口盲目驱动 `ai_agent` attach（不等 vela> 提示符）再发 `heartbeat_trigger` 后，console 完全无响应（SWD 复位恢复）。验收脚本 `stage1_agent_accept.ps1` 用 `-WantPrompt vela` 同步等待后同样操作历史上正常。后续上板驱动 CLI 必须等提示符，或干脆等自然心跳。
