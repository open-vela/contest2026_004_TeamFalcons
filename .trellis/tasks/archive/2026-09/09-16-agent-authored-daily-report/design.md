# 设计：OPENVELACLAW 真实生成日报

## 1. 现状与必须同时改掉的一点

`pick_latest_report()`（`app/velaguard/vg_ui_backend_board.c:400`）先试 `pick_runtime_report()`，只要 `runtime-report.md` 存在就直接返回，所以 `pick_latest_daily()` 今天根本不可达。更关键的是 worker 在每次报告请求时都会重写 `runtime-report.md`（同一文件 `:621-629` 的 `vg_runtime_write_report`），因此任何按 mtime 比较新鲜度的做法都会让固件报告永远胜出。

结论：**不做 mtime 比较，改为先判定来源**，见第 3 节。

## 2. skill 重写（`app/velaguard/vg_agent_seed.c`）

`g_operations_report_skill[]` 现有措辞是「屏幕报告由固件写入 runtime-report.md，不要用 dump 原文覆盖」「禁止 write_file 覆盖该文件」，整段替换为：

```
# 运行日报（VelaGuard）

## 何时使用
收到「生成今日运行日报」的请求时。只写日报，不写周报。

## 必做步骤（按顺序，工具调用不超过 4 次）
1. get_current_time 取当天日期 YYYY-MM-DD
2. run_shell vgruntime dump            （运行时长、通信质量、点位在线、异常时间线）
3. run_shell vgstats dump <最差从站>    （仅当上一步给出最差从站）
4. write_file /data/velaguard/reports/daily-<YYYY-MM-DD>.md

## 文件格式（纯文本，禁止 Markdown 标记）
第一行：AI-DAILY v1
第二行：date=<YYYY-MM-DD>
第三行：source=agent
第四行：---
随后固定三节，每节一行标题加两到四行正文：通信质量 / 点位在线 / 异常时间线
正文总长不超过 1400 字节，全部使用中文常用字

## 约束
- 数字只能来自工具输出，不得编造；工具失败时在对应节写「数据不足」
- 不得写 runtime-report.md，不得调用 vgpoint / vgdiscover apply / vgcfg commit
- 只读工具：vgruntime dump、vgstats dump、vgmodbus、read_file
```

同步改 `g_heartbeat_md[]` 里对应的那句，使两条路径指向同一个日报文件。正文上限 1400 B 是为了落在既有 `vg_ui_report_snapshot_t.body[1536]` 之内，不改结构体。

`write_seed_file()` 内容不同即重写，已 provisioned 的板子会自动拿到新 skill。

## 3. 来源判定与读取

`vg_ui_report_snapshot_t`（`gui/main/ui/model/vg_ui_backend.h:38`）加 `bool from_agent`，board 与 mock 两侧都填。

`board_read_latest_report` 的流程改为：

1. 先把 `runtime-report.md` 刷新一次（保持离线兜底是新鲜的），行为与今天一致。
2. 找当天的 Agent 产物：把 `pick_latest_daily()` 改成只认 `daily-<今天>.md`（今天日期由板端本地时间格式化），不再取字典序最大的 `daily-*`。历史文件一律不采纳。
3. 读到该文件后跑 `vg_ai_report_validate`（首行 `AI-DAILY v1`、`date=` 等于今天、长度 1..1800 B、mtime 在 26 h 内、UTF-8 合法）。
4. 校验通过 → 装入快照，`from_agent = true`，路径为该文件。
5. 文件不存在、校验不过或读取失败 → 读 `runtime-report.md`，`from_agent = false`。

这样 Agent 报告的胜出条件与文件时间戳无关，只与「今天、格式对、够新」有关，固件报告的即时重写不会把它挤掉。

## 4. 主动发起

`board_request_daily_report()`（`vg_ui_backend_board.c:544`）现在是直接 `return true;` 的空壳，注释写着 poke 后 ReAct 会 HardFault（2026-09-14）。该根因（`skill_loader_build_summary` 的偏移累加越界写）已在 09-16 修复并通过板测，因此把它改成真实发起，经子任务 A 的通道走 `velaclaw_ask`，不走 poke。

worker（`vg_hmi_file_worker_thread`，100 ms 周期）每 tick 检查：

- 网络可用：`vg_ui_backend_poll_net()` 的 `egress != 0`。
- 当天还没有新鲜的 Agent 日报。
- 当天标记文件 `/data/velaguard/reports/.daily-requested-<日期>` 不存在。
- 当前没有在途请求。

四条同时成立才发起，并写标记文件。报告页手动刷新走 `vg_ui_report_request(true)`，限流 300 s。

完成判定以产物文件为准：worker 在后续 tick 观察到当天文件出现且校验通过，才把快照从 `GENERATING` 切到 `READY`。超时（沿用 A 的 110 s deadline）或离线则切 `ERROR`/`EMPTY`，读取方回退固件报告。

由于 HMI 下 `cron_service_start()` 被跳过，定时只能由本 worker 负责，这也是把调度放在这里而不是 cron 的原因。

## 5. 页面标注（`gui/main/ui/pages/vg_page_report.c`）

`apply_snapshot`（:40）目前用 `snap->path` 的 basename 组标题（`运行报告 · <文件名>`）。改为按 `from_agent` 分派：

| `from_agent` | 标题 | 正文末尾附加 |
|---|---|---|
| true | `AI 日报 · OPENVELACLAW` | `来源：板载 Agent（OPENVELACLAW），经只读工具生成` |
| false | `本地运行报告（规则统计）` | `来源：固件确定性统计，离线可用` |

正文仍走既有 `strip_markdown()`，Agent 产出是纯文本，不受影响。`GENERATING` 分支保持现有文案，超时或离线切到固件报告时标题自动翻成本地标签，正好对上演示脚本第 8 拍的断网降级。

## 6. 取证

框架已输出 `[%s] Executing tool: %s`（`packages/ai_agent/src/tools/tool_registry.c:568`）与 `END status=ok iters=N tools=M elapsed=Ns`。扩展 `scripts/stage1_agent_ops_accept.ps1` 按顺序断言这几行并确认当天文件落盘。再补一个 NSH 回读（新增 `vgagent` 或扩展 `vgruntime dump`）打印上一轮的 `status/iters/tools/elapsed`，让屏幕与验收证据不必依赖翻 syslog。

时长预算要注意：历史那轮 115 s 已经贴着 120 s 的 LLM 墙钟，所以 skill 明确限制工具调用不超过 4 次。

## 7. 兼容与回滚

- skill 文本可随固件回退到旧版本；旧版本只是让 Agent 复述固件报告，不影响页面。
- `from_agent` 为新增字段，mock 填 false 后页面行为等同今天。
- 来源判定与主动发起可分别回退：只回退发起分支时就退化成今天的「刷新才生成」，页面逻辑仍正确。
