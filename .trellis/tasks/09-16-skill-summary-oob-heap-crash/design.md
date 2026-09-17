# 设计：skill 摘要越界写与同类偏移下溢

## 边界

改动全部在 `../packages/ai_agent`，三个文件加一个新测试目录：

| 文件 | 角色 |
|---|---|
| `src/tools/skill_loader.c` | 越界写现场（R1/R2/R3/R4） |
| `src/core/context_builder.c` | 同路径未触发的堆越界隐患（R4/R5/R7） |
| `src/core/memory_store.c` | 同类结构（R6） |
| `tests/` | host 回归测试（R8） |

不碰 `mm/`、不改 defconfig、不改 `AGENT_CONTEXT_BUF_SIZE`。不改任何公开函数签名，
`skill_loader_build_summary` 的返回语义由「本该写入长度」收紧为「实际写入长度」，两者在修复后相等。

## 契约

### 缓冲区偏移不变量

修复后以下不变量在整条提示词构建路径上成立，后续所有 `size - off - 1` 都靠它保证非负：

```
0 <= off <= size - 1        （size == 0 时 off 恒为 0，且不写任何字节）
```

### `skill_loader_build_summary(char *buf, size_t size)`

- `size == 0`：不写 `buf`，返回 0。
- 返回实际写入长度，恒满足 `<= size - 1`；`buf` 恒以 NUL 结尾（`size >= 1` 时）。
- 放不下的条目整条丢弃：不写半行，`off` 停在上一条目的末尾。
- 发生丢弃时打一条 `LOG_WARNING`（R4）。
- `opendir` 失败的早退路径语义不变：`buf[0] = '\0'`、返回 0、不 `closedir`。

### `context_builder.c` 局部助手

```c
static size_t ctx_append(char *buf, size_t size, size_t off,
                         const char *fmt, ...);
```

饱和语义：`off >= size - 1` 时不再写入，把 `buf[size - 1]` 置零并返回 `size - 1`；
`snprintf` 返回负值时返回原 `off`；返回值截断时返回 `size - 1`；否则返回 `off + n`。
因此「缓冲区已满」和「刚刚写满」都收敛到 `size - 1`，调用者无需再判断。

两个文件各自保留一份局部实现（`skill_loader.c` 内联、`context_builder.c` 用助手），
不抽公共头文件：截断策略不同，共用需要额外传出「是否截断」状态，可读性反而下降。

## 关键改动

### 1. `skill_loader.c` — 累加与收尾

```c
    size_t off = 0;
    bool truncated = false;
    struct dirent *ent;

    while ((ent = readdir(dir)) != NULL) {
        /* ... name/.md/隐藏文件过滤不变 ... */

        /* 剩余空间放不下下一条目时整条丢弃；此处 off <= size - 1，
         * 下面的 size - off 不会下溢。 */
        if (off >= size - 1) {
            truncated = true;
            break;
        }

        /* ... full_path / fopen / extract_title / extract_description 不变 ... */

        /* snprintf 返回的是「本该写入」的长度，不是实际写入长度。
         * 直接累加会让 off 越过 size，收尾的 buf[off] = '\0' 随即写到界外
         * （本次故障中它落在调用者 agent_loop_task 的栈帧里，写坏了 tools_json）。
         * 放不下的条目整条丢弃：半行会把 read_file <半截路径> 这种错误指令
         * 写进提示词，模型下一步就会去读一个不存在的文件。 */
        int n = snprintf(buf + off, size - off,
            "- **%s**: %s (read with: read_file %s)\n",
            title, desc, full_path);
        if (n < 0 || (size_t)n >= size - off) {
            truncated = true;
            break;
        }
        off += (size_t)n;
    }

    closedir(dir);

    buf[off] = '\0';      /* off <= size - 1，恒在界内 */
    if (truncated) {
        syslog(LOG_WARNING,
            "[%s] Skills summary truncated at %d/%d bytes; "
            "some skills omitted from the prompt\n",
            TAG, (int)off, (int)size);
    }
    syslog(LOG_INFO, "[%s] Skills summary: %d bytes\n", TAG, (int)off);
    return off;
```

`buf[0] = '\0'` 提到 `opendir` 之前，一并覆盖 `!dir` 与「循环一次都没跑」两种情况。
截断分支不再单独写终结符：`snprintf` 已在界内写过 NUL，`off` 也没有前进，
函数末尾那次 `buf[off] = '\0'` 就是同一个位置。

### 2. `context_builder.c` — 全量改走 `ctx_append`

`off += snprintf(buf + off, size - off, ...)` 在 `:105` `:130` `:140` `:152` `:157` `:162` `:167`
`:175` `:184` 全部换成 `off = ctx_append(buf, size, off, ...)`，格式串与实参原样保留。
函数入口补 `size == 0` 早退，让下面每处 `size - off - 1` 都有定义。

`append_file()` 增加入口夹取，`fread` 长度靠不变量成立：

```c
static size_t append_file(char *buf, size_t size, size_t offset,
                           const char *path, const char *header)
{
    if (size == 0) {
        return 0;
    }

    /* ctx_append 保证 offset <= size - 1；这里的夹取只为防御后续新增调用者，
     * 因为写错时下溢的长度会直接交给 fread。 */
    if (offset >= size) {
        return size - 1;
    }

    FILE *f = fopen(path, "r");
    if (!f) return offset;

    if (header && offset < size - 1) {
        offset = ctx_append(buf, size, offset, "\n## %s\n\n", header);
    }

    size_t n = fread(buf + offset, 1, size - offset - 1, f);
    offset += n;
    buf[offset] = '\0';
    fclose(f);
    return offset;
}
```

`build_tool_names()` 改为 `off = ctx_append(...)`，`off < size - 1` 的判断移进助手；
饱和时打一条 `LOG_WARNING`，因为能力边界被截断会让模型拒绝本来可用的工具。

两个 memory 块保持结构，`avail` 靠不变量非负，加注释标明依据：

```c
    off = ctx_append(buf, size, off, "\n## Long-term Memory\n\n");
    {
        /* off <= size - 1 由 ctx_append 保证，故 avail 恒非负。 */
        size_t avail = size - off - 1;
        if (avail > 0 && memory_read_long_term(buf + off, avail) == OK
            && buf[off]) {
            off += strlen(buf + off);
            off = ctx_append(buf, size, off, "\n");
        }
    }
```

函数末尾按 R4 补截断告警，再打原有的长度日志。

### 3. `context_builder.c` — `skills_buf` 扩到 1280

10 个技能摘要 1086 字节，封顶后若仍用 1024 会整条丢掉一个（且丢哪个取决于 `readdir` 顺序，不固定）。
扩到 1280 让提示词内容与今天一致。该槽位与 `tool_names[1024]` 复用，帧从 1156 增到约 1412 字节。

### 4. `memory_store.c` — 同类收尾

`memory_read_recent()` 有两处界外写：`offset < size - 4` 只保证分隔符调用前有余量，
`snprintf` 返回本该长度后 `offset` 可越过 `size`，紧接着的
`fread(buf + offset, 1, size - offset - 1, f)` 下溢成无界读入，`buf[offset] = '\0'` 也落到界外。

```c
    size_t offset = 0;
    buf[0] = '\0';

    for (int i = 0; i < days && offset < size - 1; i++) {
        /* ... date_str / path / fopen 不变 ... */

        if (offset > 0 && offset < size - 4) {
            int n = snprintf(buf + offset, size - offset, "\n---\n");
            if (n < 0 || (size_t)n >= size - offset) {
                break;      /* 放不下分隔符就停在已写入的内容上 */
            }
            offset += (size_t)n;
        }
        size_t n = fread(buf + offset, 1, size - offset - 1, f);
        offset += n;
        buf[offset] = '\0';
        fclose(f);
    }
```

改后 `offset <= size - 1` 恒成立：分隔符分支只在 `offset + n < size` 时前进，
`fread` 分支最多前进到 `size - 1`。同时给 `memory_read_recent` 与 `memory_read_long_term`
补 `size == 0` 早退，因为两者的 `size - 1` / `buf[0]` 在 `size == 0` 时本身就是界外写。

### 5. `tests/` — host 回归测试

```
tests/stub/nuttx/config.h          空文件，让 agent_config.h 走 #ifndef 回退
tests/tool_registry_stub.c         tool_registry_invalidate() 空实现
tests/test_skill_summary.c         用例 1：skill 摘要 canary
tests/test_context_prompt.c        用例 2：提示词构建 canary + 桩
```

两个用例共用同一套 canary 手法：把被测缓冲区夹在两个 `0xA5` 填充区之间，调用后用
逐字节比对判断越界，刚好对应板上的故障形态（越界写落在相邻对象上）。

用例 1 用 `cap` 取 1024 / 1086 / 64 / 2 / 0 跑真实 `skill_loader.c`，
断言 canary 完好、`n < cap`、`n == strlen(buf)`。

用例 2 链接真实 `context_builder.c` 与 `apps/netutils/cjson/cJSON/cJSON.c`，
桩掉 `tool_registry_get_tools_json`（返回约 30 个工具名）与两个 memory 读函数
（长记忆桩按 `avail - 1` 写满），再用 4096 / 2048 / 1024 / 512 / 64 / 1 / 0 跑，
断言 canary 完好且 `strlen(buf) < cap`。这组桩会把 `off` 顶到 `size`，
正是原代码 `avail = size - off - 1` 下溢的触发条件。

`packages/ai_agent/Makefile:27` 已把 `tests` 放进 include 路径，测试目录不需要接入 NuttX 构建。

## 兼容性

- 无签名、无 ABI、无配置变更。`skill_loader_build_summary` 返回值由「本该长度」收紧为「实际长度」，
  三个调用者中两个只用 `> 0` 判断，第三个把它当长度用但不做算术，均兼容。
- 提示词内容在 R7 之后与今天一致（1086 字节技能块全部保留），因此不改变 LLM 行为与
  prompt cache 前缀。
- 新增的截断告警会在缓冲区接近写满时出现，属于新增可观测性，不影响功能。

## 风险与回滚

- 最坏情况是技能块被截断而丢掉一个能力条目。R7 之后不会发生；若将来技能继续增长，
  告警会先出现，再由人工决定扩缓冲还是压缩条目样板。
- 饱和语义让 Long-term Memory / Recent Notes 在写满时安静地少写一段。修复前同样少写，
  区别是修复前会越界，所以这是严格改善；新增告警让这种情况可见。
- `build_tool_names` 饱和时模型可能拒绝本来可用的工具，故单独加告警。
- 本次改动与工作树上其他未提交的 HMI 缓解改动无关。回滚只按路径
  `git checkout -- src/tools/skill_loader.c src/core/context_builder.c src/core/memory_store.c`，
  **禁止** `git checkout .` 或 `git stash`，那会连带丢掉同分支上未提交的 HMI 改动。
- 回滚后板端行为回到「每轮必崩」，可用于对照复现。
