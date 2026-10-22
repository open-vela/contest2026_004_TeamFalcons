# BVO-10 三意图查询、点位解析与完整回答

日期：2026-10-22。状态：主机实现与测试完成，已接入板端构建；语音链路接线与真机验证在 BVO-11/BVO-14。

## 1. 交付

| 文件 | 内容 |
|---|---|
| `app/velaguard/vg_voice_intent.h/.c` | 三意图确定性分类、填充词剥离、数字读音归一、点位解析 |
| `app/velaguard/vg_voice_facts.h/.c` | 一轮回答的单一快照，带 seq 与配置版本 |
| `app/velaguard/vg_voice_answer.h/.c` | 同一快照渲染完整文字与播报摘要，模型文本数值一致性校验 |
| `app/velaguard/vg_agent_tools.c` | 新增 `vg_anomaly_read`、`vg_alarm_recovery`；`vg_point_read` 命中后发布快照 |
| `app/velaguard/host_tests/test_voice_intent.c` | 意图、词元、读音归一、歧义与不猜测；30 条固定问法矩阵（三类各 10 条） |
| `app/velaguard/host_tests/test_voice_answer.c` | 点值、异常全量、恢复四态、一致性、播报截断 |
| `app/velaguard/host_tests/test_agent_tools.c` | 四个工具的定义、参数护栏、异常与恢复输出 |
| `app/velaguard/Makefile` / `host_tests/Makefile` | 构建接入 |

## 2. 已实现的合同

- 意图集合封闭：当前异常、单点读数、告警恢复；写入、配置、清除、输出控制一律 `VG_VI_REFUSED`，在工具之前拒绝，不依赖提示词。
- 空识别不进入 Agent（由 BVO-08 状态机保证），本层对空文本返回 UNKNOWN。
- 点位解析顺序：精确 id、精确名称、唯一子串、数字归一后的 id。两个点同名返回 AMBIG，不选第一项。
- 读音归一（零/〇→0、幺/一→1 等）只作用于 id。归一后没有匹配就是 NOT_FOUND，不改写成另一个存在的点位。
- 异常回答使用运行层全量接口，不经过报告页的 8 点截断；total 统计点位数，多类别重叠只算一项。
- 恢复回答只依据事件环：进行中、已恢复、恢复但缺 RAISE、无法确认四种状态分开表述。
- 文字与播报来自同一 `struct vg_voice_facts`；播报只取前三项，顺序与文字一致。
- 模型文本只有通过 `vg_va_text_facts_consistent()` 才可展示；快照里没有的数字一律拒绝，标识符内的数字不当作数值。

## 3. 工具面

| 工具 | 参数 | 输出 |
|---|---|---|
| `vg_point_read` | `query?` | 既有点位文本；命中后发布点值快照 |
| `vg_run_report` | 无 | 既有运行报告 |
| `vg_anomaly_read` | 无 | `total/shown/boot` 与按严重度排序的条目；发布异常快照 |
| `vg_alarm_recovery` | `point_id`, `kind?` | `state/history_complete/duration_ms/episode`；发布恢复快照 |

四个工具都只读，不打开 RS485，不修改点表。参数长度、类型与枚举在 C 中校验；调用速率沿用每分钟 20 次的窗口。

## 4. 测试

```
cd app/velaguard/host_tests
make test_voice_intent test_voice_answer test_agent_tools
./test_voice_intent && ./test_voice_answer && ./test_agent_tools
```

结果见 `host-tests.log`：三项全部通过。

构建：竞赛仓根目录 `bash scripts/build.sh`，TARGET=velaguard-lvgl，退出码 0，生成 `.debug/nuttx.hex`。构建日志中的 `vg_runtime.c:1216` 截断告警在本次改动前已存在，未修改该行。

## 5. 未完成

- 未烧录，未在真机观察工具输出与浮层文本。
- 语音链路尚未把 `vg_vi_classify`/`vg_vi_resolve_point` 接到实际 ASR 文本，接线在 BVO-11。
- 模型回答与确定性回答的取舍目前由调用方决定；BVO-11 接入时必须先查一致性再用模型文本，否则用渲染文本。
- 配置版本尚未由采集侧写入 `vg_voice_facts_set_config_version()`，当前发布值为 0，渲染时省略该行。
