# Quality Guidelines

> Code quality standards for backend development.

---

## Overview

<!--
Document your project's quality standards here.

Questions to answer:
- What patterns are forbidden?
- What linting rules do you enforce?
- What are your testing requirements?
- What code review standards apply?
-->

(To be filled by the team)

---

## Forbidden Patterns

### Don't: 按 `snprintf` 的返回值累加缓冲区偏移

```c
/* 禁止 */
size_t off = 0;
while ((ent = readdir(dir)) != NULL && off < size - 1) {
    off += snprintf(buf + off, size - off, "...%s...\n", name);
}
buf[off] = '\0';                    /* off 可能已经越过 size */
```

`snprintf` 返回的是**本该写入的长度**，不是截断后实际写入的长度。直接累加会让 `off` 在
单轮之内越过 `size`，因为循环条件只在每轮开头判断；随后的 `size - off` 作为 `size_t`
下溢成极大值，交给 `snprintf` / `fread` 就是一次无界写。

**Why it's bad**：2026-09-16 `ai_agent` 每轮对话必崩就是这条。10 个技能摘要 1086 字节
写进 1024 字节的 `skills_buf`，收尾的 `buf[1086] = '\0'` 落到调用者 `agent_loop_task`
栈帧偏移 +22，正好是 `tools_json` 指针的 bit16-23。函数返回后第一个堆操作
`free(tools_json)` 拿到 `0x2400XXXX`（`.bss`），`mm_forcefree` 用垃圾 size 走空闲链表，
把 `g_irqvector[104].handler`（`stm32_ltdcirq`）当成链表指针，写 `flink->blink` 落到
只读 QSPI flash 的 `0x900a3469`，MPU 报 DACCVIOL 升级 HardFault。
release 构建 `DEBUG=0`，`mm_free` 的 `DEBUGASSERT(mm_heapmember(...))` 被编译掉，
所以野指针不会当场断言，只会静默写坏堆。

**Instead**：

```c
/* 正确：按实际写入长度累加，并把 off 夹在 size - 1 以内 */
int n = snprintf(buf + off, size - off, "...%s...\n", name);
if (n < 0 || (size_t)n >= size - off) {
    break;                          /* 放不下就停，off 停在最后一个完整条目之后 */
}
off += (size_t)n;
buf[off] = '\0';                    /* off <= size - 1，恒在界内 */
```

放不下的条目**整条丢弃**，不留半行：半行会把 `read_file <半截路径>` 这种错误指令写进提示词。

排查剩余同类写法：

```bash
grep -rn "+= snprintf\|size - off\|size - offset" <目录>
```

`packages/ai_agent` 内仍有多处同形写法（`tool_shell.c`、`tool_web_search.c`、`tool_cron.c`、
`tool_files.c`、`tool_media.c`、`feishu_recv.c`、`feishu_http.c`、`vela_tls.c`、
`node_manager.c`）。它们目前靠循环前判断或提前返回保持有界，改动这些文件时必须先确认边界。

---

## Required Patterns

### 所有 `(buf, size)` 写入函数必须守住偏移不变量

**What**：函数内部维护的偏移 `off` 在任何一次写入之后都必须满足 `0 <= off <= size - 1`，
并且 `size == 0` 时一个字节都不写。

**Why**：这是所有 `size - off - 1` 表达式非负的唯一依据。少了它，长度参数下溢成极大值，
一次 `fread` 就能写穿整个堆。板端缓冲区普遍偏紧（提示词缓冲区 4096 字节，实测已用到 3552），
截断路径不是罕见分支。

**Example**：`packages/ai_agent/src/core/context_builder.c` 的 `ctx_append()` 是这条不变量的
参考实现：`off >= size - 1` 时不再写入，把 `buf[size - 1]` 置零并返回 `size - 1`，
让「缓冲区已满」和「刚刚写满」收敛到同一个值。

**Related**：跨层影响见 [cross-layer-thinking-guide](../../guides/cross-layer-thinking-guide.md)。

### 缓冲区逼近上限时必须可观测

截断发生时打一条 `LOG_WARNING`。缺少这条日志时，缓冲区写满和内容正常无法区分。
参考 `context_builder.c` 的 `System prompt truncated at %d/%d bytes` 与
`skill_loader.c` 的 `Skills summary truncated at %d/%d bytes`。

---

## Testing Requirements

### `(buf, size)` 写入函数要有 canary 回归测试

被测缓冲区两侧各放一段 `0xA5` 填充区，调用后逐字节比对。canary 必须**精确**检测到 1 字节
越界，而不是只抓大幅溢出：`rear` 区从 `buf + size` 开始，`size` 这个下标本身就要被检查。

`size` 至少要覆盖 `size == 0`、`size == 1`、刚好放不下一个条目的值、以及产品实际使用值。

参考实现：`packages/ai_agent/tests/test_skill_summary.c`、`test_context_prompt.c`，
编译命令见 `.trellis/tasks/09-16-skill-summary-oob-heap-crash/research/oob-repro-*.txt`。
这两个用例在改前必须失败、改后必须通过，否则它们没有验证能力。

注意 `snprintf` 语义类缺陷抓不到：`-Wformat-truncation` 看不到运行期才确定的 `size - off`，
`-fsanitize=bounds` 也看不穿 `char *` 参数的下标。canary 是这类缺陷唯一便宜可靠的证据。

---

## Code Review Checklist

<!-- What reviewers should check -->

- 新增或修改的 `(buf, size)` 写入函数，偏移是否按实际写入长度累加并封顶，`size == 0` 是否安全
- 截断路径是否有 `LOG_WARNING`
- 是否附带 canary 测试，且该测试在改前会失败
