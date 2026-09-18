# 让板载 ai_agent 回答点表实时值与运行报告

命令与稳定应答以 [`docs/velaguard-host-nsh-protocol.md`](../../../docs/velaguard-host-nsh-protocol.md) 为唯一口径。
设备端 Agent 的只读边界以 [`AGENTS.md`](../../../AGENTS.md) 的 V5 和「人工确认与设备端工具」为准。本文件不另开命令表。

## 目标

在 NSH 上让 MiMo 读懂并回答两类自然语言提问：

1. `ask 告诉我UPS负载的值` → 返回已确认点表中该点的真实工程值、单位与新鲜度
2. `ask 给我截止目前的运行报告` → 返回板上实时生成的三节运行报告

## 现状为什么答不出

`ask` 走消息总线 → ReAct → 工具。当前唯一数据入口是 `run_shell` 的 C 层白名单
（`packages/ai_agent/src/tools/tool_shell.c:112` 的 `s_vg_readonly`）：`vgpoint`、`vgdiscover`
明确禁调，`vgmodbus` 只读原始寄存器。

- 点表里 `ups_load` 的 `scale=0.1`（`scripts/vgpoint_scene_room.json:105`），`vgmodbus` 读回的是
  未缩放的寄存器原值，会把 10 报成 10 而不是 1.0%。
- `vgmodbus` 不知道点表，无法把中文名 `UPS负载` 映射到 `id=ups_load`。
- `vgpoint get` 逻辑正确（`app/velaguard/vgpoint.c:745`），但按 V5 对 Agent 不可达，不能解禁。

所以缺的是一个只读工具，改提示词解决不了。

## 支撑这个方案的两个既有事实

- 本构建是 flat（`CONFIG_BUILD_FLAT`，单地址空间），跑在 `ai_agent` 守护任务里的工具能读到
  HMI 线程写入的同一份 `vg_runtime` / `vg_frame_stats` / 点表静态数据。这正是现在
  `vgruntime dump` 能报出实时数字的原因，不是新引入的耦合。
- `tool_registry_execute` 对 provider 工具的每次调用都会写审计行（工具名 + 返回码）到
  `/data/velaguard/logs/agent_tools.log`，已有 `scripts/stage1_agent_ops_accept.ps1:206` 在读它。
  验收因此有不依赖模型自述的机器证据。

## 需求

| 编号 | 要求 |
|---|---|
| R1 | 新增只读工具 `vg_point_read {query?}`：按 `id` 或中文名定位已确认点表中的一个点，返回工程值、单位、新鲜度与阈值 |
| R2 | 点位匹配按 `id` 精确 → `name` 精确 → `name` 子串；子串命中多个返回候选清单，不猜 |
| R3 | 取不到数据时明确区分 `no_table` / `no_sample` / `not_found`，不产出数字 |
| R4 | 新增只读工具 `vg_run_report {}`：返回板上 `vg_runtime` 的三节报告正文 |
| R5 | 报告文案与报告页兜底文件同源，两者不得说法不一致 |
| R6 | 只回答不落屏：本次 ask 不改写 `/data/velaguard/reports/` 下任何文件，不触发定时日报 |
| R7 | 两个工具都不碰 `vg_bus_try_lock`，不打开 RS485（沿用 host-nsh-protocol 对 `get` 的规定）|
| R8 | 工具注册与参数校验落在 C 实现里，不依赖提示词（`tool_guard.c` 不提供 JSON Schema 运行时校验）|
| R9 | `vgpoint` / `vgdiscover` 仍对 Agent 禁调，`runtime-report.md` 仍受 `tool_files.c` 保护 |

## 不做

- 不加 `handle_nl_fast_path` 关键词：那会绕过 MiMo，与「让 MiMo 回答」相反。
- 不改点表写入路径、不改定时日报与告警建议流程、不改告警页。
- 不给 `vg_runtime` 的 `g_pts` 加锁：本次沿用既有读法，限制写在交付说明里。

## 验收

主机：`make -C app/velaguard/host_tests test`，含新增 `test_agent_query.c`。
板端：`scripts/serial_ask_probe.ps1` 加 `point` / `runreport` preset，`scripts/stage1_agent_ops_accept.ps1`
加对应断言，含反例（改点表被拒）与「报告页文件未被本次 ask 改写」。
