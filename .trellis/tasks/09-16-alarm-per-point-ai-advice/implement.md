# 执行计划：告警页逐点 AI 建议

前置：子任务 A 的 `vg_ai_contract.h` 与 backend 缓存接口已就位。`gui/` 下用 LVGL 风格（4 空格、`if(`）；`app/velaguard/` 下用 NuttX 风格（2 空格、大括号换行）。

## 步骤

### S1. 接口（`gui/main/ui/model/vg_ui_backend.h` / `.c`）

1. `vg_ui_backend.h` 包含 `../../../../app/velaguard/vg_ai_contract.h`（按实际相对路径写，或依赖已有的 `-I$(CURDIR)` 与 CMake 路径直接 `#include "vg_ai_contract.h"`，与 `gui/CMakeLists.txt:43`、`app/velaguard/Makefile:124` 一致即可）。
2. 加 `vg_ui_advice_state_t`、`vg_ui_alarm_advice_request`、`vg_ui_alarm_advice_state`、`vg_ui_alarm_advice_get` 声明。
3. `vg_ui_backend.c`（PC mock）给三个函数加空实现 / `VG_UI_ADV_IDLE` / `false`。

### S2. 板端缓存（`app/velaguard/vg_ui_backend_board.c`）

1. 持有 `vg_ai_advice_doc_t` 与状态，加互斥保护；`vg_ui_alarm_advice_get` 按 `(id, epoch)` 加 boot 匹配做命中判断。
2. 从 A 的 worker 调度接状态变化：装载成功置 READY 并换文档，超时或校验失败置 ERROR，其中清空还是保留上一份文档要与 A 对齐（建议保留上一份，避免页面闪回）。
3. 只在 `CONFIG_VG_HMI` 与 `CONFIG_VG_AGENT_OPS` 生效的路径上编译，非板端保持 mock 行为。

### S3. 行结构（`gui/main/ui/pages/vg_page_alarm.c`）

1. `alarm_ctx_t` 加 `row_ai[ALARM_LIST_MAX]`。
2. `make_alarm_row` 追加第三个标签：`LV_LABEL_LONG_DOT`、满宽、`vg_font_small()`、`vg_color_info()`、初值空、默认加 `LV_OBJ_FLAG_HIDDEN`。
3. `set_row_texts` 末尾按 `design.md` 3.2 的片段追加行内建议；未命中隐藏。
4. `build_sig` 不动。

### S4. 详情区（同文件）

1. 把 `set_ai_text` 扩成 `set_ai_block(head, body)`，替换现有全部调用点。
2. `render_detail` 按 `design.md` 3.3 的状态表分派头部文本与正文；新增 `char ai[560]` 组装 AI 正文，`a == NULL` 分支只改头部措辞。

### S5. headless 与单测

1. 在 headless 构建下加测试钩子注入预制文档（板端不编译）。
2. 扩展 `gui/headless/alarm_check_main.c`：断言主告警行文本以 `AI · ` 开头、详情头含 `OPENVELACLAW`、未命中的第二行仍是规则文本；再 bump `al_epoch` 保留旧文档，断言两行都降级、详情头为规则摘要（本地）。

### S6. 验收脚本

按 `design.md` 与 `prd.md` 的序列扩展 `scripts/stage1_lvgl_hmi_accept.ps1`（skill 对照表里「HMI / LVGL 页面」就是它），不新加脚本，避免改 skill 的对照表。若确需新脚本，同步在 `.claude/skills/velaguard-board-inner-loop/references/commands.md` 与 `.agents/skills/` 镜像加一行。

## 验证门禁

| 门 | 命令 | 通过标准 |
|---|---|---|
| G1 | headless 构建与运行 | 行文本、详情头、epoch 失效三项断言通过 |
| G2 | `make -C app/velaguard/host_tests test` | 无回归 |
| G3 | `bash scripts/build.sh` | 默认 `velaguard-lvgl` 通过；PC 模拟器 `gui/` 构建不受影响 |
| G4 | `scripts/stage1_flash_and_accept.ps1` | COM3 开机正常 |
| G5 | `scripts/stage1_lvgl_hmi_accept.ps1` | 注入 ≥2 条告警后行内出现 `AI · `，详情头含 `OPENVELACLAW` |
| G6 | 板上断网序列 | 拔网重新注入告警，行内与详情回到规则摘要（本地），无伪造 AI 文本 |

每个子任务收尾按 `velaguard-board-inner-loop` 的内环跑完再汇报，格式为命令 / pass-fail / 关键日志一行，失败先自行修一轮；COM3 抢占失败按该 skill 的用户介入条款停下问用户，不假装板测通过。

## 回滚点

- **R0** S1、S2 退掉后页面读不到建议，自动走规则摘要分支，行为等同今天。
- **R1** S3、S4 的页面改动可单独 `git checkout` 回退，A 的契约与通道保留不影响其他功能。

## 执行结果（2026-09-16）

### 落地位置

| 内容 | 文件 |
|---|---|
| 后端缓存与查表 | 新增 `app/velaguard/vg_advice.{h,c}`，查询接口声明在 `gui/main/ui/model/vg_ui_backend.h` |
| 行内建议与详情 | `gui/main/ui/pages/vg_page_alarm.c`（新增 `row_ai` 行、`set_ai_block`、`render_ai_detail`） |
| PC mock 与 headless 注入 | `gui/main/ui/model/vg_ui_backend.c` 的 `vg_ui_backend_mock_set_advice()` |
| skill | `app/velaguard/vg_agent_seed.c` 的 `g_alarm_interpretation_skill[]` 改为产出 VGADV1 |

### 门禁

| 门 | 结果 | 证据 |
|---|---|---|
| G1 | 通过 | `gui/headless/alarm_check` 输出 `ALL PASS (0 failures)`，含新增的 4 条建议断言 |
| G2 | 通过 | `make -C app/velaguard/host_tests test` 全绿 |
| G3 | 通过 | `bash scripts/build.sh` 通过 |
| G4 | 通过 | `scripts/stage1_flash_and_accept.ps1` |
| G5 | 板端通道已验证 | `research/vgagent-round-20260916-r2.txt`：`round: state=running gen=3` |
| G6 | 待板测复现告警条件 | 见下方说明 |

### 视觉确认

把 headless 的渲染帧转成 PNG 直接看了两帧：

- `07_alarm_ai_advice`：`温度-01` 行在规则摘要下多出一行 `AI · 先确认现场积水并检查排水`（info 色），其余两条没有建议的行不受影响，行高变化没有裁切。
- `08_alarm_ai_stale_epoch`：同一测点 epoch 加一后，AI 行消失、行高回落，详情头回到「规则摘要（本地）」。

### 实现中发现并修掉的一点

页面只在 `vg_model_on_change` 通知时重绘。板端每秒的 `vg_model_tick` 会递增告警持续秒数并通知，所以建议加载后一秒内就会上屏；这一点在 headless 里需要给足帧数才能观察到，最初用 `hg_pump(10)` 观察不到刷新，改为 40 帧后稳定复现。

### 板端观察到的两点（22:20 的板端记录）

1. **产物确实落盘了**。`/data/velaguard/reports/alarm_advice.txt` 1840 B，`boot=30bda344 req=15 n=8`，8 条全部 `sev=offline`（从站 1 离线），`sum`/`ev`/`att` 都填了中文内容，`ev` 里有 `vgstats` 与 `vgmodbus` 的实际观测。这证明整条链路能通到「写出合规文档」这一步。
2. **skill 第一步踩到框架的单文件短路**。`alarm_interpretation.md` 原来第一步单独 `read_file pending_alarm.txt`，命中 `agent_loop.c` 的本地文件短路，框架把文件内容直接当回复并结束本轮，后续 `write_file` 不执行。日志形如 `[agent] Local tool shortcut: skip LLM round` → `[vgagent] round finished: reply` → `[vgadvice] no advice file (2)`；同一份告警签名下每 20 s 重问一次，白烧 LLM 轮次。已把 skill 改成：告警数据用请求里带的那些，第一步直接 `run_shell vgstats dump`；确需读文件时与 `run_shell` 写在同一条消息里。同时给「轮次结束但没有可用产物」加了 `VG_ADV_RETRY_MS` 退避，同一签名不再每 20 s 重问，只有告警集合真的变化才立即重试。退避初值取过 120 s，后来改成 300 s：一轮要跑约 195 s，120 s 的间隔会让这条单通道几乎一直忙在建议轮上，把日报轮挤掉。同一次修正里还把 file worker 的调用顺序改成 `daily_maybe_request()` 先于 `vg_advice_tick()`，日报每天只问一次，让它在通道上优先。

### 线程边界（实现时纠正）

`vg_advice_tick()` 原本在 file worker 线程里直接调 `vg_model_collect_alarms()`，而 `vg_model` 只由 UI 线程维护，会和实时采集更新竞争。已改成既有 `pending_alarm.txt` 的同一模式：UI 线程在 `vg_ui_backend_apply_live()` 末尾调 `vg_advice_note_alarms()` 发布一份带字符串拷贝的快照，worker 只读快照。

## 执行结果补充（2026-09-17）

### 构建与单测

- headless：`ctest --test-dir .debug/hmi-headless` 5/5 通过，含告警页 AI 建议渲染与 epoch 失效两项断言。
- `bash scripts/build.sh` 退出码 0，0 个 error、0 个 warning，产物 `.debug/nuttx.hex` 为 3508039 字节（2026-09-17 08:09）。本子任务文件的 warning：`vg_advice.c` 的 snprintf 截断警告改为 strncpy 加显式补 NUL。
- `make -C app/velaguard/host_tests test` 13 个测试二进制全绿。

### 板端产物

在 `scripts/stage1_agent_ops_accept.ps1` 这一轮（pass=19 fail=1）中，逐点告警建议文件 `/data/velaguard/reports/alarm_advice.txt` 出现，内容为 `VGADV1`、`boot=30bd1132`、`req=1`、`n=8`，8 条全部 `sev=offline`、`unres=0`，`sum`/`ev`/`att` 三段齐全。`ev` 里写的是工具观测到的真实帧统计：从站 1 共 7 台设备 203 帧全部超时、一次成功都没有，从站 2 是 14 帧。

### 待办

- 屏幕肉眼确认没有做：告警页行内 `AI · ` 与详情头由代码与 headless 覆盖。
- 断网降级已在板端复现：`vgnet inject` 置两条链路不可用后删掉建议文件，五分钟内 `net_connect ret=0x42` 与 `LLM call failed` 反复出现，文件没有重新生成，页面读不到缓存就走规则摘要。证据见子任务 C 的 `research/offline-20260917-r*.log`。
- `alarm_advice.txt` 的板端内容里中文在串口日志中是乱码，那是控制台编码问题，文件本身是 UTF-8 且已过板端校验。

## 请求体的字节预算（2026-09-17）

板端构造的 VGADV1 请求与最多 8 条告警行共用 `VG_ADV_REQ_MAX`（1536 B），构造器在放不下时整份返回 `VG_AI_ERR_RANGE`，本轮流就完全不会发起。原先的请求头写得很啰嗦，加上「必须用 write_file 写出结果」这句之后，用八个较长的点名一测是 1592 B，已经超了。已把请求头压紧，保留身份三元组与逐行格式，字段含义交给 skill 文档；同时把「必须写出文件、只回文字不算完成、信息不足写 unres=1」并进同一句。

实测（host，8 条、点名 8 个汉字）：1410 B，余量 126 B。`test_ai_contract` 里补了一条最坏情况的断言，防止请求头再长回去。

## 模型侧的行为波动（2026-09-17）

板端记录到过一轮 Agent 只回了文字、没有调用 `write_file`：`round finished: reply` 之后 `advice_load` 读到的是上一轮留下的文件，`boot` 对得上而 `req` 差一，解析器返回 `VG_AI_ERR_STALE`，整份拒绝并进入退避。这是设计要的行为（宁可不显示，也不显示对不上号的建议），不是缺陷。两处应对：

- 请求文本里明确「这一轮的产物是文件」，降低漏写概率。
- 验收脚本给建议轮留 900 s：一轮约 195 s，一次失败加退避 300 s 再加一轮，窗口太短会把能用的流程判成失败。

## 第二轮校验与三处修正（2026-09-17）

独立校验确认前面几处改对了，并指出三处新问题，都已修，明细写在子任务 A 的 implement.md（未认领结果占住通道、请求格式示例与解析器矛盾、建议流把 -EBUSY 当长退避）。

板端两侧的配合改动：请求里的条目模板恢复成四个字段各占一行（实测 1410 B，仍在 1536 以内），避免模型照着一行示例写出无法解析的文档。
