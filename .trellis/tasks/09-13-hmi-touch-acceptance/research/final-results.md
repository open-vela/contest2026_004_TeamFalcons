# 触摸验收结果（部分完成）

日期：2026-09-13。任务：`09-13-hmi-touch-acceptance`。  
结论：**代码与自动回归可交付；T5 实体触摸 / L1+ 性能门槛未测，任务保持未完成。**

## 构建与板端身份

| 项 | 值 |
|---|---|
| 默认产品固件 | `bash scripts/build.sh`，hex `3cd82ff4…`（本会话复位烧录） |
| 板端 `vghmi perf` | `state=disabled (built without CONFIG_VG_HMI_PERF)` |
| `/proc/version` | `18bec160782 Sep 13 2026 18:21:25 stm32h750b-dk:velaguard-lvgl` |
| 证据 | `.debug/hmi-post-opt/default_perf_check.txt` |
| HMI accept | `stage1_lvgl_hmi_accept.ps1` pass=16 fail=0（先于本轮扩展） |

## T-AC 映射

| 编号 | 状态 | 证据 |
|---|---|---|
| T-AC1 | 通过（自动+板日志） | `test_hmi_sched`；板端 `pointer indev read=10ms n=1` |
| T-AC2 | 部分（无头） | I01/I02/I16；**实体手指滑动/惯性未测** |
| T-AC3 | 部分（代码路径） | 按下身份/al_epoch/手势冻结已落地；I05–I09 无头未全覆盖，板端未测 |
| T-AC4 | 部分（无头） | I11/I12/I13/I18/I19 |
| T-AC5 | 部分（代码） | 返回 48×28、chip 44×28；人工目视未测 |
| T-AC6 | 通过（前置 runtime） | report 后台快照 + lifecycle_render_check |
| T-AC7 | **未完成** | L0 静止对照见父任务 `final-results.md`；触摸 P95/FPS/切页 P95 缺实体样本 |
| T-AC8 | **未完成** | host/headless/构建/accept 有；缺 200 点击、200 切页录像、30 分钟资源 |

## 无头 `interaction_check`（本轮扩展后）

覆盖：I01 I02 I11 I12 I13 I16 I17 I18 I19 I22 I24。

| 命令 | 结果 |
|---|---|
| `ctest --test-dir .debug/hmi-headless` | 5/5 pass |
| `ctest --test-dir .debug/hmi-headless-32` | 5/5 pass |

未纳入无头（需板端或更强 fixture）：I03–I10、I14–I15、I20–I21、I23（I23 由 `test_hmi_sched` 覆盖）。

## 父任务 HMI-AC 简表

| 范围 | 状态 |
|---|---|
| L0 静止对照（优化后） | 已有（commits 158→9） |
| L1/L2 触摸与滚动 | 未测 |
| L3 Agent 同固件 | 未测（关联 `09-12-heartbeat-llm-round-system-wedge`） |
| D2/D3 数据组 | 未测（需人工 `vgpoint apply --confirm`） |

## 仍阻塞关闭本任务

1. 实体触摸录像与统一契约样本量（200 点击 / 200 切页 / 指定滚动）。
2. 触摸与切页 P95、滚动 FPS 的板端测量固件采样。
3. 30 分钟混合操作资源曲线。
4. L3 真实 Agent 压力，或与心跳挂死任务的明确归因。
5. 人工 LCD 目视（热区、告警 AI 块、报告页）。

在以上完成前，不得将本子任务或父任务标为验收通过。
