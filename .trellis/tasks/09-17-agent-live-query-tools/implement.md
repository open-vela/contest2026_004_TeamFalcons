# 实现：让板载 ai_agent 回答点表实时值与运行报告

## 目标

```text
ask 告诉我UPS负载的值        -> 返回 ups_load 的工程值、单位、新鲜度与阈值
ask 给我截止目前的运行报告   -> 返回板上三节运行报告
```

## 根因：为什么原来答不出

`ask` 走消息总线 → ReAct → 工具，唯一数据入口是 `run_shell` 的 C 层白名单
（`packages/ai_agent/src/tools/tool_shell.c:112` 的 `s_vg_readonly`）：

- `vgpoint` 按 AGENTS.md V5 对 Agent 禁调，`vgdiscover` 同样禁调。
- `vgmodbus` 只读原始寄存器，不知道点表。演示点表 `scripts/vgpoint_scene_room.json:105`
  的 `ups_load` 是 `scale 0.1`，所以它会把 10 报成 10 而不是 1.0%，也无法把
  中文名 `UPS负载` 对到 `id=ups_load`。

所以缺的是一组只读工具，改提示词解决不了。

## 改动

| 内容 | 位置 |
|---|---|
| 包外注册工具的公开接口 | `packages/ai_agent/include/tools/tool_provider.h`（新增） |
| 复用该接口的 typedef | `packages/ai_agent/src/tools/tool_registry.h` |
| 点位匹配与渲染（纯逻辑，可主机测试） | `app/velaguard/vg_agent_query.{h,c}`（新增） |
| 两个只读工具的注册与实现 | `app/velaguard/vg_agent_tools.{h,c}`（新增） |
| 报告文案抽出为可复用 sink | `app/velaguard/vg_runtime.{h,c}` |
| Skill 文案：点位查询、口头报告与写日报分开 | `app/velaguard/vg_agent_seed.c` |
| 启动时注册 provider | `app/velaguard/velaguard.c` |
| 编译接入 | `app/velaguard/Makefile` |
| 主机测试 | `app/velaguard/host_tests/test_agent_query.c`、`test_agent_tools.c`、`stubs/` |
| 板端只读探针（不依赖模型） | `scripts/vg_agent_tools_probe.ps1`（新增） |
| 板端验收与探针 | `scripts/stage1_agent_ops_accept.ps1`、`scripts/serial_ask_probe.ps1` |
| 只读诊断子命令 `vgagent tools` / `vgagent tool` | `app/velaguard/vgagent.c` |
| 公开头补充 `tool_registry_get_tools_json` / `tool_registry_execute` | `packages/ai_agent/include/tools/tool_provider.h` |
| 协议说明 | `docs/velaguard-host-nsh-protocol.md`、`.trellis/spec/backend/host-nsh-protocol.md` |

### 两个工具

- `vg_point_read {query?}`：读已确认点表（`vg_point_table_read`）与 HMI 轮询快照
  （`vg_live_snapshot_read`），输出 `vgquery:` 行。匹配顺序是 `id` 精确 → `name`
  精确 → `name` 子串；子串命中多个返回候选清单而**不猜**，因为报错点位的值比
  反问更糟。取不到数据时用 `reason=no_table / no_sample / read_failed` 区分，
  绝不用 0 冒充缺失值。
- `vg_run_report {}`：调 `vg_runtime_format_report()`，与报告页兜底文件同一份文案。

### 为什么不碰 `vg_bus_try_lock`

`vg_point_read` 只读文件，和 NSH `vgpoint get` 一样（`docs/velaguard-host-nsh-protocol.md:14`
明确要求 `get` 不得取总线锁）。一轮 LLM 调用上限 120 秒，占着 RS485 会把轮询线程
拖住。

### 为什么报告文案要共用

报告页读 `runtime-report.md` 兜底，Agent 回答走缓冲区。若两处各写一份文案，
屏幕和回答会慢慢说不一样的话。抽出 `report_emit()` 后由
`test_runtime.c` 的逐字节比较断言钉住。

### 与既有设计一致的几点

- **flat 构建**：`ai_agent` 守护任务里的工具能读到 HMI 线程写入的同一份
  `vg_runtime` / `vg_frame_stats` / 点表静态数据。这不是新耦合，现有
  `vgruntime dump` 就是这个读法。
- **可审计**：`tool_registry_execute` 对 provider 的每次调用都写
  `/data/velaguard/logs/agent_tools.log`（工具名 + 返回码），已有验收脚本在读它，
  所以「模型确实调了工具」有机器证据，不靠模型自述。
- **限流在 C 里**：`tool_guard.c` 只对 `run_shell`/`write_file`/`edit_file` 限流，
  只读工具没有上限，所以按 AGENTS.md 要求补了 20 次/60 秒，规则放在
  `vg_agent_query.c` 以便用选定时钟测试（含 32 位单调时钟回绕）。
- **参数校验在 C 里**：`tool_guard.c` 不做 JSON Schema 运行时校验，所以
  `query` 的类型与长度在 `vg_agent_tools.c` 自己查；超长直接拒绝而不是截断，
  截断后的点名可能命中另一个点。

## 验证

### 已执行

- `make -C app/velaguard/host_tests test`：17 个测试全过，含新增
  `test_agent_query`（匹配、歧义、阈值、限流、时钟回绕）与 `test_agent_tools`
  （工具 JSON 可解析、缩放值、中文名、参数守卫、无表/无采样）。
- **防回归是反向验证过的**，不是只跑绿：
  - 把候选清单在 `set_match` 之后才写入 → 6 条断言失败；
  - 删掉 buffer 版的 provenance 行 → 2 条逐字节断言失败；
  - 破坏工具 JSON 的收尾括号 → 2 条失败；
  - 把值乘 10（模拟 vgmodbus 的未缩放错误）→ 2 条失败；
  - 把限流哨兵改回 `window_start_ms == 0` → 1 条失败。
- `bash scripts/build.sh`：velaguard-lvgl 编译通过，`nuttx.hex`/`nuttx.bin` 已产出。
- 符号确认已进镜像：`vg_agent_tools_register`、`vg_tools_execute`、
  `vg_runtime_format_report`、`vg_agent_query_match`、`vg_agent_rate_allow`、
  `tool_registry_register_provider`。
- **编译开关的四种组合都过 `-Wall -Werror -Wextra`**：只有报告、只有点位、
  只有 HMI discover、两者都没有（`velaguard-ai-probe` 的形态）。这一步查出了
  两个真实缺陷：`parse_query` 与它的缓冲区在只编报告时是未使用变量，
  `vg_tools_get_json` 的 `first` 标志在只编一种工具时是 set-but-unused。
  两次都是先让组合编不过、再改代码，不是猜的。
- 两个 PowerShell 脚本用 `Parser::ParseFile` 解析通过；新增的断言模式用样例数据
  在 Windows PowerShell 上逐条验证过正反例。这一轮查出一个真实的漏洞：原先用
  `[regex]::Escape($refValue)` 做子串匹配，而 `ups_load` 的参考值可能是 `1`，
  它会匹配到 `10`——正是未缩放寄存器会打印的值，等于让这条断言形同虚设。
  现在改成把两侧都取数字按容差比较（`Assert-ValueQuoted`），并验证了 14 个
  正反例，包括 `1` 不接受 `10`、`10` 接受 `10.0`、`0` 不接受 `0.1`。

### 板端（已执行）

烧录后做过两轮板测，结果如下。

**工具层（不依赖模型，`scripts/vg_agent_tools_probe.ps1`，18/18 通过）**

```text
vgagent tools  → vg_point_read / vg_run_report 都已下发给模型
vgagent tool vg_point_read ups_load   → rc=0  id=ups_load reason=read_failed warn=ge70 crit=ge90
vgagent tool vg_point_read UPS负载     → rc=0  id=ups_load          （中文名解析）
vgagent tool vg_point_read UPS        → rc=0  ambiguous n=3         （不猜，列候选）
vgagent tool vg_point_read 负载        → rc=0  id=ups_load          （唯一子串）
vgagent tool vg_point_read 不存在的点位 → rc=0  reason=not_found
vgagent tool vg_run_report {}         → rc=0  通信质量 / 点位在线 / 异常时间线 三节齐全
vgagent tool no_such_tool_xyz {}      → rc=-1（反例：工具名不对必须失败）
```

`values.txt` 里 `ups_load` 是 `ok=0`（这台台架没接 RS485 从站，轮询 0/14 全失败），
工具如实报 `reason=read_failed` 而不是印一个数字，这正是设计要的行为。

**模型层（`ask`，需要模型后端可用）**

干净审计日志（测前删除该文件）下的实测：

```text
548.030 tool=vg_point_read  rc=0 args={"query":"ups_load"}     ← 模型为「告诉我UPS负载的值」自己选的
953.160 tool=vg_run_report  rc=0 args={}                       ← 模型为「给我截止目前的运行报告」自己选的
```

控制台同一轮也打印了 `[agent] Tool call: vg_point_read args={"query": "ups_load"}`
与 `llm=ok backend=0`。回答内容如实说明 RS485 读不到值。

**`scripts/stage1_agent_ops_accept.ps1`：pass=37 fail=0**（模型可用时）。

### 未执行 / 未能稳定复现

- 模型后端可用性在这台台架上是间歇的（见下），上面那轮干净日志的 `ask` 是成功的一次；
  另一次在 LLM 失败窗口内跑，模型什么都没调用，审计日志为空。这个断言现在不会
  因为失败窗口而误判（先清日志、先探测后端），但「每次都成功」没有被证明。
- 数值与 `vgpoint get ups_load` 的逐值比对未生效：台架没接从站，参考值是 `-`，
  脚本转为断言 `reason=read_failed|no_sample`。接入从站后应重跑，让比对真正生效。


## 已知限制

- `vg_runtime` 的 `g_pts` 由 HMI 线程更新、无锁，工具从 Agent 任务读，读法与今天
  `vgruntime dump` 相同。数值可能短暂不一致，本次不引入加锁改动。
- `values.txt` 只有整份快照的 `tick_ms`，没有逐点时间，所以新鲜度按整份快照计
  （`VG_AGENT_QUERY_STALE_MS` 为 5 秒）。
- 交互式 `ask` 与板上定时日报/告警建议共用单轮通道，日报在飞时 ask 会排队（既有行为）。
- 系统提示在 HMI 构建下只有 4 KB，已有 14 个 Skill 共享摘要，所以本次没有新增
  Skill 文件，改动并入 `modbus_query.md` 与 `operations_report.md`；板测时应确认
  提示词未溢出。
- `packages/ai_agent` 的工作树还带着另一个任务的未提交改动（LLM TLS send-retry）。
  本次只新增 `include/tools/tool_provider.h` 并改 `src/tools/tool_registry.h` 的
  include，没有切分支、没有提交，避免把他人的改动混进来。公共头的 PR 按 C2/C3
  后续补。
