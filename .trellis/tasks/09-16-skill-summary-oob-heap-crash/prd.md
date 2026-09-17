# 修复 skill 摘要越界写导致的 ai_agent 堆损坏必崩

堆布局、MPU 配置与分配器行为以 `../nuttx` 实际构建产物为准（`.debug/nuttx.elf` 反汇编 + `.config`）；
本文件不重复抄写寄存器表。改动落在 `../packages/ai_agent`，按 C2 在该树功能分支提交。

## 目标

消除 `ai_agent` 每轮 ReAct 必崩的根因，并让提示词构建路径上同一类「按 `snprintf` 返回值累加偏移、不封顶」
的写法不再可能越界，同时留下一条可重复运行、不依赖硬件的回归证据。

## 背景

板端 `ai_agent` 收到 `ask hello` 必崩，现场：

```
[skills] Skills summary: 1086 bytes
[context] System prompt built: 3489 bytes
HARDFAULT: CFSR=00000482 HFSR=40000000 BFAR=900a3469 MMFAR=900a3469
HARDFAULT: stacked PC=90009c70 LR=90009c3d
```

已逐环定死的部分（反汇编 `.debug/nuttx.elf`，符号化全部命中）：

1. `packages/ai_agent/src/tools/skill_loader.c:372` 用 `snprintf` 的返回值累加 `off`。该返回值是
   「本该写入的长度」，不是截断后的实际长度；`off` 也没有上限截断，循环条件只在每轮开头判断，
   所以单轮就能把 `off` 顶过 `size`。
2. 调用点 `packages/ai_agent/src/core/context_builder.c:172-173` 传的是 `char skills_buf[1024]`。
   当前 10 个技能的摘要是 **1086 字节**，于是 `skill_loader.c:379` 的 `buf[off] = '\0'` 写了 `buf[1086]`。
3. `context_build_system_prompt` 反汇编：`subw sp, sp, #1156`，`skills_buf` 在 `sp+128`
   （与 `tool_names[1024]` 复用槽位），帧只到 `sp+1155`。`&skills_buf[1086]` = 调用者
   `agent_loop_task` 帧偏移 **+22**。
4. `agent_loop_task` 帧 1620 字节；`str r0, [sp, #20]` 紧跟 `bl tool_registry_get_tools_json`，
   即 `[sp,#20]` 是 `tools_json`。偏移 +22 是该 32 位值 bit16-23，一个 `0x00` 把
   `0x2404XXXX` 变成 `0x2400XXXX`，落进 `.bss`。
5. `context_build_system_prompt` 返回后第一个堆操作就是 `free(tools_json)`
   （`agent_loop.c:1610`）。`.config` 未开 `CONFIG_DEBUG_ASSERTIONS`，`mm_free` 的
   `DEBUGASSERT(mm_heapmember(heap, mem))` 被编译掉，野指针直接进 `mm_forcefree`。
6. `mm_forcefree` 拿 `.bss` 里的垃圾值当 `node->size`，算出 `next = 0x240040b8`，
   落在 `.bss` 的 `g_irqvector[]` 里。异常帧 `R0 = 0x240040b8` 就是它。
7. `next->flink` 读 `[next+8] = [0x240040c0]`。`g_irqvector` 基址 `0x24003d80`，
   `STM32_IRQ_LTDC = 16 + 88 = 104`，`0x24003d80 + 104*8 = 0x240040c0` 正是
   `g_irqvector[104].handler`，`stm32_ltdc.c:1653` 在此 `irq_attach(..., stm32_ltdcirq, ...)`
   存了 `0x900a345d`。
8. 出错指令 `str r7, [r6, #12]`（`mm_free.c:219` 的 `next->flink->blink = next->blink`）
   向 `0x900a345d + 12 = 0x900a3469` 写。QSPI 窗口由 `mpu_priv_flash(0x90000000, 128M)` 配成
   特权只读且不带 XN，于是这是一次存到只读 flash 的 DACCVIOL：`CFSR` 报
   DACCVIOL|MMARVALID（0x82）加 IMPRECISERR（0x400），`MMFAR` 正是 0x900a3469。

每一轮对话都会重放这条链，所以现象必现。同路径上还有一类未触发的隐患：`context_builder.c`
整体沿用同一套不封顶的 `off` 累加，而 `sys_prompt` 是 4096 字节**堆**缓冲区（当前提示词 3489 字节，
离满只有约 600 字节）。一旦某次 `snprintf` 截断，`size - off` 会作为 `size_t` 下溢成极大值，
随后交给 `snprintf` / `fread` 就是一次无界堆写，比本次的 1 字节栈越界危害更大。

## 需求

R1. **`skill_loader_build_summary` 封顶**。`off` 在任何写入后都保证 `<= size - 1`；
   `snprintf` 返回负值立即结束；`buf[off] = '\0'` 因此始终在界内。

R2. **放不下的条目整条不写**。剩余空间不足一行时结束循环，不留半行。
   半行会把 `read_file <半截路径>` 这种错误指令写进提示词，模型下一步就去读一个不存在的文件。

R3. **返回值等于实际写入长度**。返回值改为实际写入的字节数，调用者看到的数字与缓冲区内容一致。

R4. **提示词截断可观测**。`skill_loader_build_summary` 与 `context_build_system_prompt` 在发生截断时
   各打一条 `LOG_WARNING`。当前构建里提示词离满只有约 600 字节，缺这条日志就看不到逼近上限。

R5. **`context_builder.c` 全量封顶**。所有 `off += snprintf(buf + off, size - off, ...)` 换成不会下溢的写法，
   覆盖 `:105` `:130` `:140` `:152` `:157` `:162` `:167` `:175` `:184`；
   `append_file()` 的 `fread` 长度、以及交给 `memory_read_long_term` / `memory_read_recent` 的
   `avail` 都必须先夹到 0；`build_tool_names()` 同样按实际写入长度累加。

R6. **`memory_store.c` 同类写法封顶**。`memory_read_recent()`（`memory_store.c:213/215`）
   与本次根因结构完全相同，且唯一调用者就在被修路径上，一并加固。

R7. **`skills_buf` 扩到 1280**（`context_builder.c:172`，经用户确认）。10 个技能摘要是 1086 字节，
   扩到 1280 后提示词内容与今天一致，不会因为封顶而掉条目；帧从 1156 增到约 1412 字节，
   32 KiB 的 `AGENT_AI_AGENT_STACK` 足够。

R8. **host 回归测试**。新增一份不依赖硬件的测试，能在本机把越界复现出来（改前 FAIL、改后 PASS），
   并覆盖 `size` 取 1024 / 1086 / 64 / 2 / 0 的边界。

## 验收标准

A1. host 测试在当前树上运行 **FAIL** 并指出越界（作为根因证据），打上修复后 **PASS**。
A2. host 测试对 `context_build_system_prompt` 用 canary 覆盖 4096 / 2048 / 1024 / 512 / 64 / 1 / 0，
   改前能在 4096 用例上打出界，改后全部通过且 `strlen(buf) < size`。
A3. `bash scripts/build.sh` 通过，`skill_loader.c` / `context_builder.c` / `memory_store.c` 无新增告警。
A4. 板端：`scripts/serial_net_test_ask.ps1` 跑通，`[skills] Skills summary` 与
   `[context] System prompt built` 之后**不再出现 `HARDFAULT`**，NSH 不掉线，该轮继续往下走到 LLM 请求或
   LLM 报错。崩溃点在 `free(tools_json)`、位于 `run_react_loop` 之前，所以**没有可用 LLM key 也能判定**。
A5. 连发不少于 3 轮 `ask`，确认不再复现；`ifconfig`、HMI 状态正常。
A6. `make -C app/velaguard/host_tests test` 保持通过（本次不碰该目录，作为无回归对照）。

## 范围外

- 不恢复为此崩溃临时关掉的 HMI 缓解措施（心跳触发 ReAct、`mallinfo`/heapscan、心跳间隔 3 分钟）。按用户
  决定暂不动，恢复另开任务。
- 不调整 `AGENT_CONTEXT_BUF_SIZE`，也不通过扩大栈来回避问题（本次故障与栈耗尽无关）。
- 不修包内其余的同类写法。检查阶段的全包扫描列出约 10 个文件：
  `tools/tool_shell.c`、`tools/tool_web_search.c`、`tools/tool_cron.c`、`tools/tool_files.c`、
  `tools/tool_media.c`、`channels/feishu_recv.c`、`channels/feishu_http.c`、`infra/vela_tls.c`、
  `node/node_manager.c`。它们目前靠循环前判断或提前返回保持有界（`node_manager.c` 只在
  `CONFIG_AI_AGENT_NODE` 下编译，本板未开），不是缺陷，但要另开任务统一收口。
- 不修 `context_build_messages`（`context_builder.c:284-285`）对 `size == 0` 的下溢。属既有代码，
  且在包内、`../nuttx`、`../apps` 全树都没有调用者。
- 不处理本次实测暴露的 LLM 传输问题：`[vela_tls] Write request failed: -3` 使 LLM 调用失败
  （agent 回 Sorry, I encountered an error）。`skills summary 1086` / `prompt 3489` /
  `16506 bytes` 加同一句回复在 2026-09-02 的 `.debug/ask_lvgl_result.txt` 里就存在，
  与本次越界无关，另开任务。
- 不改 `mm/` 侧配置（不开 `CONFIG_DEBUG_ASSERTIONS`、`CONFIG_MM_RECORD_STACK`、`CONFIG_MM_RECORD_PID`）。
  这属于诊断基线调整，另开任务。
- 不压缩每个技能条目末尾的 ` (read with: read_file ...)` 样板（约占摘要一半）。这会改变提示词语义，
  需要单独的 LLM 行为评估。

## 关键决定

- D1. 放不下的条目**整条丢弃**而不是保留半行（经用户确认由 R2 承担该后果，实际由 R7 消除）。
- D2. `skills_buf` 扩到 1280，让修复不改变提示词内容（经用户确认）。
- D3. 两个文件各自保留一份局部封顶助手，不抽公共头文件：两处的截断策略不同
  （提示词侧饱和、技能列表侧丢条目），共用需要额外传出「是否截断」，反而难读。
- D4. host 测试放在 `packages/ai_agent/tests/`（该包 `Makefile:27` 已把 `tests` 放进 include 路径）。
