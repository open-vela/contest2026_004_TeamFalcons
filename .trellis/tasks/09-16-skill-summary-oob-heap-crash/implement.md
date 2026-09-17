# 执行计划

## 步骤

1. **先立回归证据（改代码之前）**
   写 `tests/stub/nuttx/config.h`、`tests/tool_registry_stub.c`、`tests/test_skill_summary.c`，
   在**未修改**的 `skill_loader.c` 上编译运行，确认 FAIL 且报出越界字节位置。
   同时写 `tests/test_context_prompt.c` 与桩，在未修改的 `context_builder.c` 上运行，
   确认 4096 用例越界。这一步的输出是根因的独立证据，必须留下原始日志。

2. **修 `src/tools/skill_loader.c`**（R1/R2/R3/R4）
   按 `design.md` 第 1 节改 `skill_loader_build_summary`：`buf[0]` 提前、封顶累加、
   丢整条、实际长度返回、截断告警。

3. **修 `src/core/context_builder.c`**（R4/R5/R7）
   加 `<stdarg.h>` 与 `ctx_append`；函数入口补 `size == 0` 早退；
   九处 `off += snprintf(...)` 改 `off = ctx_append(...)`；`append_file` 补夹取；
   `build_tool_names` 改走助手并加饱和告警；两个 memory 块补注释；
   `skills_buf[1024]` 改 `1280`；函数末尾补截断告警。

4. **修 `src/core/memory_store.c`**（R6）
   `memory_read_recent` 分隔符分支改判返回值并允许提前结束，两个读函数补 `size == 0` 早退。

5. **重跑两个 host 用例**，确认全部 PASS 且无告警输出中的越界提示。

6. **编译固件**：在选手仓根目录 `bash scripts/build.sh`。

7. **上板验收**：`stage1_flash_and_accept.ps1` 烧写，`serial_net_test_ask.ps1` 跑 `ask`。

## 验证门

| 门 | 命令 | 通过条件 |
|---|---|---|
| G1 根因证据 | 步骤 1 的两个用例（改前） | 都 FAIL，并指出越界发生在被测缓冲区之后 |
| G2 修复有效 | 同一命令（改后） | 两个用例全部 PASS |
| G3 无回归 | `make -C app/velaguard/host_tests test` | 与改动前一致通过（本次不碰该目录） |
| G4 可编译 | `bash scripts/build.sh` | 构建成功，三个改动文件无新增告警 |
| G5 板端 | `serial_net_test_ask.ps1` | 不出现 `HARDFAULT`，NSH 不掉线，该轮继续走到 LLM 请求或 LLM 报错 |
| G6 稳定性 | 连发 >= 3 轮 `ask` | 均无 `HARDFAULT`；`ifconfig` 正常 |

G5 不要求可用 LLM key：崩溃点在 `free(tools_json)`，位于 `run_react_loop` 之前。

## 回滚点

- R0：步骤 1 之后。测试文件是新增，删掉即回到改动前状态，`packages` 源码未动。
- R1：步骤 2 之后。`git checkout -- src/tools/skill_loader.c`。
- R2：步骤 3 / 4 之后。按路径回滚三个源码文件；`git checkout .` 与 `git stash` 会把同分支上
  未提交的 HMI 缓解改动（`agent_config.h`、`agent_loop.c`、`agent_mem.h`、`heartbeat.c`、
  `llm_router.c`、`tool_shell.c`）一并丢掉，禁止使用。
- R3：复现原故障需要先回到改前代码再重编，`.debug/nuttx.hex` 已被本次构建覆盖，旧镜像没有留档。
  改前版本的三个文件可从 git 取回（`git -C ../packages/ai_agent show HEAD:<路径>`），
  按步骤 1 里记录的命令重编即可。若还要再验证一次，先另存当时的 `.debug/nuttx.hex`。

## 提交切分

一次提交三个源码文件 + 一个测试目录，压成一条信息说明机理（`snprintf` 返回本该长度 →
`off` 不封顶 → `buf[1086]` 落到调用者栈帧 → `tools_json` 指针被清零 → `free()` 写坏堆 →
`mm_forcefree` HardFault）。与工作树上其他 HMI 改动分开提交。

## 验证结果（2026-09-16）

| 门 | 结果 | 证据 |
|---|---|---|
| G1 | 通过 | 从 git HEAD 取出改前 `skill_loader.c`，同一份用例 `cap=1024 rear=BAD`；改前 `context_build_system_prompt` 7 个 cap 全部 `rear=BAD`。`research/oob-repro-before.txt` |
| G2 | 通过 | 改后两个用例全部 PASS。`research/oob-repro-after.txt` |
| G3 | 通过 | `host_tests` 9 个测试 0 失败。`research/host_tests_baseline.txt` |
| G4 | 通过 | `bash scripts/build.sh` 成功；三个文件无新增告警（`skill_loader.c:482` 的 `%08x` 在 HEAD 里已有）；反汇编确认 `mov.w r1, #1280`、帧 1156 → 1420；新告警字符串已进镜像 |
| G5 | 通过 | `stage1_agent_accept` pass=8 fail=0；`serial_net_test_ask.ps1` 与 3 轮 `ask hello` 中 `Skills summary: 1086` / `System prompt built: 3552` 之后 **0 条 `HARDFAULT`**，该轮越过崩溃点走到 `[llm] OpenAI API with tools`。`research/board_ask_after_fix.txt`、`research/board_ask_3rounds_after_fix.txt` |
| G6 | 通过 | 3 轮 `ask hello` 全部完成提示词构建，无 fault；随后 `ifconfig` 正常、NSH 存活 |

R7 补充验证：1134 字节摘要（大于板上 1086）在 1280 缓冲区里 14 条全部保留、无越界、无饱和。

## 检查阶段更正

- `memory_read_recent` 的 `offset > 0 && offset < size - 4` **在 `size == 3` 时确实会走到下溢比较**，
  「size < 6 不可达」的说法是错的。该分支仍然安全，但依据来自新增的
  `n < 0 || (size_t)n >= size - off` 判断，不是来自不可达。后续若有人觉得这个邻接判断多余而删掉它，
  请先读这段。
- `skill_loader.h:47` 的注释本来就写的是「实际写入字节数」，修复让实现与自己的文档一致，
  不需要改头文件。
- `skill_loader_init` 写入的内置技能内容在 `# 标题` 后紧跟空行，而 `extract_description()` 遇空行即停，
  所以**内置技能的描述全部为空**。崩溃现场回溯里能直接看到 `- **New Digest**: (read with: ...)`
  冒号后没有描述。不影响内存安全，属提示词质量问题，另开任务。
- 检查阶段全包扫描发现同类写法约 10 个文件（清单见 `prd.md` 范围外）。

## 遗留

- `packages/ai_agent` 内约 10 处同类 `snprintf` 累加写法统一收口，另开任务（清单见 `prd.md` 范围外）。
- 内置技能描述为空导致技能摘要信息量偏低，另开任务。
- LLM 传输：实测 `[vela_tls] Write request failed: -3` 使 LLM 调用失败（agent 回 Sorry, I encountered
  an error）。2026-09-02 的 `.debug/ask_lvgl_result.txt` 里已是同一现象，与本次越界无关，另开任务。
  当前演示的 LLM 轮次受此影响，无法给出真实回答。
- 恢复为此崩溃临时关掉的 HMI 缓解措施，另开任务。
- `mm/` 侧诊断基线（`CONFIG_DEBUG_ASSERTIONS` / `CONFIG_MM_RECORD_STACK` /
  `CONFIG_MM_RECORD_PID`），另开任务。没有这些，下次同类故障仍然只能看到一个 HardFault。
- `../nuttx` 工作树里 `boards/arm/stm32h7/stm32h750b-dk/configs/velaguard-lvgl/defconfig`
  有未提交改动，含明文 `CONFIG_VG_MQTT_*` 口令。提交到公共树前需要处理，另开任务。
