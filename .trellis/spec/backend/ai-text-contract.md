# 板端 AI 文本契约（VGADV1 与日报）

板上 Agent（OPENVELACLAW）产出的一切上屏文本都先过这份契约再上屏。规则正文以 `AGENTS.md` V2/V5 为准，这里只落实写法。

## 为什么要有单独的契约模块

`AGENTS.md` V5 要求「只读权限、参数上限、限流、会话门控和工具调用审计必须落实在工具注册与 C 实现中，不能只依赖提示词」。模型输出是不可信输入，所以：

- 解析与校验放在 `app/velaguard/vg_ai_contract.{h,c}`，只依赖 libc，**不引入 LVGL、cJSON、NuttX 头**，这样边界用例能在 `app/velaguard/host_tests` 里跑。
- 不能放进 `gui/main/ui/model/`：那里的 `vg_model.c` 引入 LVGL，无法 host 编译，边界测试就没了。
- 页面对 AI 文本**只有一个来源**——已经过校验的缓存条目。页面自身不读文件、不发请求、不做降级判断以外的任何加工，因此不存在伪造 AI 文本的代码路径。

## VGADV1 行式格式

Agent 写 `/data/velaguard/reports/alarm_advice.txt`：

```
VGADV1
boot=<8 位十六进制>
req=<十进制>
n=<0..8>
[1]
id=<点名 id>
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

用行式文本而不是 JSON：校验器要手写，行语法容易做到严格且有界；JSON 还得在板上多引一个解析器。字段顺序固定，条目序号从 `[1]` 连续递增，`sev` 数值复用 `enum vg_alarm_kind`。

### 上限与失败语义

| 项 | 上限 | 依据 |
|---|---|---|
| `id` | 23 字符，`[A-Za-z0-9_]` | 与 `vg_point_validate_id` 同一取值空间 |
| `sum` | 80 B | 行内标签缓冲是 96 B，`AI · ` 前缀占 6 B |
| `ev` / `att` | 200 B 各 | 详情区 |
| 整份文档 | 2560 B | 必须落在 HMI 的 `AGENT_LLM_STREAM_BUF_SIZE`（4 KB）内，否则 `write_file` 参数会被截断 |
| `n` | ≤ 8 | `ALARM_LIST_MAX` |

- **结构错误整份丢弃**：格式错、未知键、序号不符、`id` 非法、同一 `id` 重复、`boot`/`req` 不匹配、控制符或非法 UTF-8、超长、空 `sum`。解析器不部分写入输出结构，先解到模块静态暂存、全部通过再整体拷贝。
- **文本字段超长按 UTF-8 字符边界截断**，不整份拒绝：模型啰嗦只是啰嗦，整份拒绝会让一整轮 LLM 调用白费，截断后仍然有界且能安全上屏。截断时绝不劈开多字节序列。

## 身份三元组：boot + id + epoch

告警建议必须绑定到具体的一次告警轮次，否则同一个点重新告警时会显示上一轮的建议。命中条件是三者同时成立：

- `boot`：板端每次上电生成的随机数（由墙上时钟与单调毫秒混合派生，非零）。只靠 `al_epoch` 不够——它每次上电从小整数重新开始，跨上电会误命中。
- `id`：点名 id。
- `epoch`：`vg_sensor_t.al_epoch`，每次告警开始递增。

`req` 是轮次序号，用来拦掉超时后迟到的产物：超时把轮次判失败后，迟到的回复会因为 `boot`/`req`/`epoch` 对不上被 `vg_ai_advice_parse` 拒绝，不会被误当成下一轮的结果。

副产品：同样的道理让 `daily-<日期>.md` 只按当天文件名采纳，不按 mtime 比较新鲜度——固件报告每次请求都会重写，按时间戳比它永远赢。

## 时基：一律用单调时钟

任何超时与限流间隔都用 `clock_gettime(CLOCK_MONOTONIC)`。板子开机约一分钟会 `vgtime` 同步一次北京时间，墙上时钟会前跳；一次跳变就能让所有在途轮次同时超时。参考现象：`agent_trace_end()` 用 `gettimeofday()` 求差，实测出现 `elapsed=198s` 而单调计时只有 `llm_ms=17460`。

## 板端主动轮次只有一条通道

`app/velaguard/vg_agent_round.{h,c}` 是唯一的发起口，由 HMI 的 file worker 每个 tick 驱动，或者在 `vgagent` NSH 探针里单独驱动。

- 同一个时刻只允许一个在途轮次。`velaclaw_ask` 的异步回调是单槽的，发第二次会覆盖第一次，串行是硬要求。
- 用 `velaclaw_ask_async()` 而不是 `velaclaw_ask()`：HMI 构建开了 `AGENT_VG_HMI_LAZY_LOOP`，`agent_loop_ensure_started()` 只置一个请求标志，由 network watcher 在约 1 秒内真正拉起；直接 `velaclaw_ask` 推进去的消息没有人消费。
- 不要打开 `heartbeat_send()` 在 `CONFIG_VG_HMI` 下的提前返回：两个不同步的触发源会和在途请求抢同一个回调槽。
- tick 可能来自两个线程（file worker 与 `vgagent`），提交路径用 `g_submitting` 加锁，避免同一请求被推两次。
- **未被消费的结果不是空位**。`vg_agent_round_queue_owned()` 在 state 不是 IDLE 时一律返回 -EBUSY。否则日报侧会在建议轮刚 DONE、其主人还没读产物的一瞬间把状态覆盖成自己的请求，那一份已经写在 eMMC 上的建议就没人采纳，页面要再等一整轮。代价是每个消费者都必须把自己的 DONE/ERROR 清掉：`vg_advice_tick()` 在活动告警清零、`vgagent ask` 在收尾时都要 `clear()`，漏一处就把通道卡死。
- **这份解析器的暂存量必须是静态的**。`vg_ai_advice_parse()` 的暂存文档约 4 KB，而板端从 HMI file worker（栈 8 KB）调用它。`vg_advice.c` 已经把 `s_raw`/`s_parsed` 移成静态，解析器内部的同类对象漏了就会把 8 KB 栈用到接近上限。往这个模块加任何大对象前先算一下调用者的栈。
- **「今天已有日报」按校验结果判定，不按文件名是否存在**。只看 `stat()` 会让一份日期行写错或缺少标记的日报把当天剩下的重试全部压掉，页面一直显示固件报告，直到第二天换名字才恢复。
- **模型回填的十六进制大小写都要收**。`boot` 是 8 位随机十六进制，通常含几个字母位，模型大小写都写过。只认小写会白丢一整轮，而且报的是 RANGE，看起来像格式坏了。
- **一个通道有多个消费者时必须标记归属**。告警建议与日报共用这条通道，而回复文本不是产物（产物是各自的文件）。`vg_agent_round_queue_owned()` 记录发起方，消费者先查 `vg_agent_round_owner()` 再决定是否读产物，读完调 `vg_agent_round_clear()`。没有这层归属时，日报轮次结束会让告警侧去读 `alarm_advice.txt`，用当前 `req` 比对上一轮写的文件，得到 `VG_AI_ERR_STALE`，看着像产物坏了，实际是读错了对象。
- 轮次预算取 300 s：实测一轮 ReAct 是 6 次迭代、约 190 s 墙上时间、LLM 侧累计约 105 s，已经贴着 120 s 的单次调用墙钟。

## 写 skill 时要避开单文件调用短路

`agent_loop.c` 有一处省时间的优化：某一轮迭代如果只调用了一个 `read_file` / `write_file` / `edit_file`，且目标不是 skills 目录或 `HEARTBEAT.md`，框架会直接把文件内容当成回复并结束本轮，不再走下一次 LLM 调用。省约 2 秒，代价是后续步骤全部不执行。

这不是可以靠提示词绕开的小事。写 skill 时按两条规则办：

- 请求里已经带上的数据不要再用 `read_file` 单独取一遍。告警建议的数据就在 `VGADV1` 请求里，skill 第一步写成 `run_shell vgstats dump` 而不是读 `pending_alarm.txt`。踩过一次：skill 第一步单独 `read_file pending_alarm.txt`，整轮在第一次工具调用后就被判完，`alarm_advice.txt` 从未写出，HMI worker 每 20 秒重问一次，白烧 LLM 轮次。
- 确实需要读文件时，把 `read_file` 和至少一个 `run_shell` 写在同一条消息里一起调用，`all_local` 就不成立。最后一步的 `write_file` 被短路是可以接受的：文件已经落盘，回复文本不参与正确性判断。

顺带一条限流规则：轮次结束了但没有产出可用的文档时，同一份告警签名要按退避（`VG_ADV_RETRY_MS`）等下一次，只有告警集合真的变了才可以立刻重问。否则「永远拿不到文件」的状态会变成每 20 秒一次 LLM 轮次的固定开销。

## 轮次开销按迭代次数算，不按工具个数

单次 LLM 调用有 120 s 墙钟，整轮的耗时约等于「迭代次数 × 单次调用耗时」。实测一轮 4 次迭代是 `llm_ms=191090 elapsed=194s`，也就是每次调用约 48 s，而且每多一次迭代就要把增长中的上下文重发一遍。

所以 Skill 里互不依赖的取证命令必须写在**同一条消息**里一次发出，让一轮落在两步（取证、写文件）。原来的写法把 `get_current_time`、`vgruntime dump`、`vgstats dump` 分成三条消息，直接多花了两倍时间，还挤占了同一通道上另一个流向的轮次预算。`vgstats dump` 不带从站参数就是全部从站，这样它也就不用等 `vgruntime dump` 的结果，可以一起发。

## 报告来源必须可辨

`vg_ui_report_snapshot_t.from_agent` 是页面上唯一的来源依据：true 显示 OPENVELACLAW 署名，false 显示本地统计来源。断网、超时、校验不过都回退到固件 `runtime-report.md`，且 `runtime-report.md` 在 C 层对 Agent 只读（`tool_files.c` 的 `s_protected_files`），这样兜底内容才是可信的。

## 写代码时的连带约束

- 所有 `(buf, size)` 写入遵循 `Quality Guidelines` 的偏移不变量，禁止按 `snprintf` 返回值累加偏移。
- 工具调用审计写 `/data/velaguard/logs/agent_tools.log`，超过 64 KB 轮转一次，写失败不能影响工具调用本身。
- 新增校验逻辑后，边界用例加到 `app/velaguard/host_tests/test_ai_contract.c`；页面行为改动加到 `gui/headless/alarm_check_main.c`。两者都不占串口，先跑它们再上板。
