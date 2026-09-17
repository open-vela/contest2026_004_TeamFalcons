# 设计：AI 文本契约与请求通道

## 1. 模块边界

| 单元 | 位置 | 依赖 | 可 host 测试 |
|---|---|---|---|
| `vg_ai_contract` | `app/velaguard/vg_ai_contract.{h,c}` | 仅 libc（`stdint/stddef/stdbool/string`） | 是 |
| 请求通道与缓存 | `app/velaguard/vg_ui_backend_board.c` | NuttX + `velaclaw/client.h` | 否 |
| 界面查询接口 | `gui/main/ui/model/vg_ui_backend.h` + `.c`（mock） | LVGL 侧 | 否 |
| 只读加固 | `packages/ai_agent/src/tools/{tool_shell,tool_files}.c` | 公共树 | 公共树自带 `tests/` |

`vg_ai_contract` 放在 `app/velaguard/` 而不放 `gui/main/ui/model/`：`vg_model.c` 引入 LVGL 无法 host 编译，而 V5 要求的上屏前 schema 校验必须能在 host 上跑边界用例，沿用 `vg_alarm_eval.c` 的既有模式。

## 2. `VGADV1` 线上格式

Agent 写 `/data/velaguard/reports/alarm_advice.txt`，行式文本，`\n` 分隔，无多余空白行：

```
VGADV1
boot=<8 位十六进制>
req=<十进制>
n=<0..8>
[1]
id=<sensor_id>
epoch=<十进制>
sev=warn|crit|offline
unres=0|1
sum=<单行>
ev=<单行>
att=<单行>
[2]
...
END
```

- `n=0` 时直接 `END`，表示本轮无活动告警建议。
- 条目序号必须从 `[1]` 连续递增到 `[n]`。
- 字段顺序固定，不得省略任一必需字段。
- `sev` 数值直接复用 `enum vg_alarm_kind`（`warn`=1、`crit`=2、`offline`=3）。
- `boot` 由板端在 backend 初始化时生成（由 device id 与单调时钟派生，非零），随请求下发，Agent 必须原样回填。
- 不复用 `last_alarm.md`：新格式需要 `boot`/`req`/`epoch` 背书，写新文件避免与旧格式互相污染。

### 上限与失败语义（实现时收紧的两点）

- `sum` 上限由 96 B 收到 **80 B**：行内前缀 `AI · ` 占 6 B，96 B 的行标签缓冲要同时放前缀与 NUL，80 B 留余量。
- 结构违规整份丢弃、不做部分应用；**文本字段超长是唯一例外**，按 UTF-8 字符边界截断（不劈开多字节序列）。理由：模型写超长只是啰嗦，整份拒绝会让一整轮 LLM 调用白费，而截断后仍有界且能安全上屏。

| 项 | 上限 | 依据 |
|---|---|---|
| `id` | 23 字符 | `VG_SENSOR_ID_MAX` - 1；超出是结构错误，拒绝而非截断 |
| `sum` | 80 B，超长按字符边界截断 | 行内标签 `row_sum` 是 96 B 缓冲 |
| `ev` / `att` | 200 B 各，超长按字符边界截断 | 详情区 |
| 整份文档 | 2560 B | 必须落在 HMI 的 `AGENT_LLM_STREAM_BUF_SIZE`（4 KB）以内，否则 `write_file` 参数会被截断 |
| `n` | ≤ 8 | `ALARM_LIST_MAX`（`vg_page_alarm.c:14`） |
| 同一 `id` 出现两次 | 拒绝 | 一轮里同一测点不可能有两条告警，也顺带覆盖重复 `(id,epoch)` |
| 字符 | 必须是合法 UTF-8（拒绝 overlong、代理区、超 U+10FFFF），拒绝 `\r`、NUL 与其他 C0 控制符 | V5 |
| `sum` 为空 | 拒绝 | 空建议没有意义，视为格式错误 |

字库已具备：`gui/main/ui/fonts/cjk_symbols.txt` 是 GB2312 一级常用字（约 3755 字）加标点，`vg_font_ui_14.c` 已编进镜像，常用中文可直接上屏，生僻字会显示为空白，因此 skill 里限定常用字，校验器不额外做字形检查。

## 3. C 接口

```c
#define VG_AI_ID_MAX 24
#define VG_AI_ADV_MAX 8
#define VG_AI_SUM_MAX 96
#define VG_AI_TEXT_MAX 200
#define VG_AI_DOC_MAX 2560

typedef struct {
    char     id[VG_AI_ID_MAX + 1];
    uint32_t epoch;
    uint8_t  sev;              /* 0 未指定, 1 warn, 2 crit, 3 offline */
    bool     unresolved;
    char     sum[VG_AI_SUM_MAX + 1];
    char     ev[VG_AI_TEXT_MAX + 1];
    char     att[VG_AI_TEXT_MAX + 1];
} vg_ai_advice_entry_t;

typedef struct {
    uint32_t boot;
    uint32_t req;
    int      n;
    vg_ai_advice_entry_t e[VG_AI_ADV_MAX];
} vg_ai_advice_doc_t;

typedef struct {
    const char *id;
    const char *name;
    uint32_t    epoch;
    uint8_t     sev;
    float       value;
    float       threshold;
    int32_t     dur_s;
} vg_ai_alarm_in_t;

/* 严格解析。成功返回 0；任何违规返回负 errno 风格码，且不部分写入 *out。 */
int vg_ai_advice_parse(const char *buf, size_t len,
                       uint32_t expect_boot, uint32_t expect_req,
                       vg_ai_advice_doc_t *out);

/* 构造一轮告警建议请求文本。返回写入长度，超容量返回负值。纯函数、无 IO。 */
int vg_ai_advice_build_request(char *out, size_t cap,
                               uint32_t boot, uint32_t req,
                               const vg_ai_alarm_in_t *a, int n);

/* 校验 Agent 日报。通过返回 0。 */
int vg_ai_report_validate(const char *buf, size_t len,
                          const char *expect_date, long mtime_s, long now_s);

/* 在已解析文档中按 (id, epoch) 查找，命中返回条目指针，否则 NULL。 */
const vg_ai_advice_entry_t *vg_ai_advice_find(const vg_ai_advice_doc_t *doc,
                                              const char *id, uint32_t epoch);
```

解析器按行推进，每行在固定偏移处比对键名，值做长度与字符检查；`id` 额外拒绝 `..`、空白与路径分隔符。`boot`/`req` 由板端传入期望值比对，比对失败返回不匹配错误，用于挡掉过期轮次的产物。

## 4. 板端请求通道

不用 heartbeat：`heartbeat_send()` 在 `CONFIG_VG_HMI` 下直接返回（`packages/ai_agent/src/infra/heartbeat.c:109`），且两个不同步的主动触发源会和在途请求打架。统一由板端 file worker 发起。

用公共树已内置的本地 IPC 客户端 `packages/ai_agent/include/velaclaw/client.h`（`velaclaw_client_open` / `velaclaw_ask`，`packages/ai_agent/Makefile:159` 无条件编译），`app/velaguard/Makefile` 增加 `ai_agent/include` 到 include 路径。不需要为触发而改公共树。

调度放在既有 `vg_hmi_file_worker_thread`（`app/velaguard/vg_ui_backend_board.c:575`）里，它本就在后台线程且不碰 UI。状态机：

```
IDLE --queue--> QUEUED --提交--> RUNNING --收到回复--> DONE --消费后清--> IDLE
                   |                |
                   |-- 20 s 未送达 --> ERROR
                   |-- 300 s 未回复 --> ERROR
```

- 同一时刻只允许一个在途轮次。`velaclaw_ask` 的异步回调是单槽的，发第二次会覆盖第一次的回调，所以串行是硬要求而不是优化。
- **超时取 300 s，不是 120 s**：实测一轮 ReAct 为 6 次迭代、约 190 s 墙上时间，其中 LLM 侧累计 105 s，已经贴着 `AGENT_LLM_TIMEOUT_SEC` 的单次调用墙钟。原计划的 110 s 会让每一轮都提前判失败。
- 回复回调只做 O(1) 操作（置标志、拷一小段诊断文本），不做打印与文件 IO；它运行在 agent 的出站分发线程上。
- 完成判定以**产物文件**为准，不以回复文本为准。超时后迟到的回复由产物里的 `boot`/`req` 拦掉，不会被误当成下一轮的结果。
- 时间基准一律用 `clock_gettime(CLOCK_MONOTONIC)`：实测 `elapsed=198s` 而 `llm_ms=17460` 的差距正是因为 `agent_trace_end()` 用墙上时钟，板子开机一分钟后的 vgtime 同步会把时钟前跳，一次跳变会让所有在途轮次同时超时。

限流：告警建议轮次最小间隔 20 s，同一告警签名每 300 s 刷新一次；日报每本地日只自动发起一次，标记文件 `/data/velaguard/reports/.daily-requested-<日期>`。HMI 下 `cron_service_start()` 被跳过，定时只能由板端 worker 负责。

### 触发入口的两个前提（实现时确认）

1. **必须显式请求启动 Agent 循环**。HMI 构建开了 `AGENT_VG_HMI_LAZY_LOOP`，`agent_loop_ensure_started()` 只置一个标志，由 network watcher 在约 1 秒内真正拉起；直接往消息总线推进去的请求不会被消费。公共客户端因此新增 `velaclaw_ask_async()`，先请求启动再入队。
2. **tick 可能有两个来源**：HMI 的 file worker 与 `vgagent` NSH 探针。提交路径用 `g_submitting` 加锁保护，避免同一请求被推两次。

## 5. 只读边界加固（公共树，C2 分支 + fork/PR）

1. `src/tools/tool_shell.c` 的 `is_blocked()`：把现在只对 `vgcfg` 生效的子命令门槛推广成表驱动的 `vg_subcmd_allowed(name, rest)`。目标是 `vgstats` 只允许 `dump`、`vgnet` 只允许 `status`、`vgruntime` 只允许 `dump`（挡掉 `vgruntime report <path>` 这个可写任意路径的口子）、`vgmodbus` 保持原样、`vgcfg` 保持 `dump`。现状是 `s_allowed[]` 整体放行 `vgstats`/`vgruntime`/`vgnet`，而 `vgstats inject|reset`、`vgnet inject|wifi` 都会改状态。
2. `src/tools/tool_files.c` 的 `s_protected_files`（:136）加入 `runtime-report.md`，让 Agent 在 C 层写不动固件的离线兜底产物，而不是只靠提示词。
3. 工具调用审计：执行前向 `/data/velaguard/logs/agent_tools.log` 追加一行（单调时间戳、工具名、脱敏后的参数前缀、结果状态），复用 `tool_guard.c:76` 的脱敏函数；格式化函数写成纯函数放进 `packages/ai_agent/tests/` 以便单测。

这三项不影响 B、C 的运行（它们用当前允许表即可跑），但产品宣称的只读边界依赖它们。

## 6. 与 B、C 的接口

B 消费 `vg_ai_advice_parse` / `vg_ai_advice_find` 与请求构造；C 消费 `vg_ai_report_validate` 与同一个请求通道。两者的限流与串行化都在本子任务的 worker 调度里实现，B、C 不再各自建线程或各自发请求。

## 7. 兼容与回滚

- `vg_ai_contract` 是新增文件，不接入任何既有调用路径时行为零变化。
- 请求通道与只读加固分别独立：关掉 worker 的发起分支即回到当前状态，Agent 仍可被手动 `ask` 驱动。
- 公共树改动只在 `velaguard/*` 分支，未合入前不影响本仓构建（`scripts/build.sh` 只校验公共树已含所需改动，不 apply patch）。
