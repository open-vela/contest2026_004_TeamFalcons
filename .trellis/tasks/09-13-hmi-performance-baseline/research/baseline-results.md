# 基线结果（B4 实际采集）

日期：2026-09-13。实施会话：09-13-hmi-performance-baseline。
所有数据为本次实测；未测项如实标注，不补推测值。

## 构建身份

| 构建 | 配置 | 产物 md5 (nuttx.hex) | 说明 |
|---|---|---|---|
| 测量构建 | `bash scripts/build.sh --hmi-perf`，CONFIG_VG_HMI_PERF=y | bc60dbe805ba667a32bb0e3abcc44d72 | 2026-09-13 14:08，stats 开 |
| 默认构建 | `bash scripts/build.sh`（测量构建后自动复位） | ebb02784d7512e27980873b561e57351 | 统计关，`.config` 中 `# CONFIG_VG_HMI_PERF is not set` |

- 本仓 `7b7bc4b`（含未提交实施改动），NuttX `18bec160782263e7e2ed0f8b9ca06bbda69a5c9f`（编译自该树，`/proc/version` 显示 stm32h750b-dk:velaguard-lvgl-dirty，dirty 即本任务改动）。
- 固件 LVGL checkout `0f2a49f`。 板端实测帧缓冲：0xd0000000，fblen 522240，stride 960，bpp 16，双缓冲（mem2 yoffset=272），与父任务 E03/E04 一致。
- 板端 D1 现状：已确认点表 14 点（`vghmi: home fleet n=14`、`acq start ok points=14`）。
- 静态预算核对：map 中 `g_perf` = 0x1910 = 6416 字节（< 8 KiB 预算）；主机侧 sizeof 同为 6424（含锁初始化差异）。测量构建 sram 229724 B vs 默认 216908 B（+12.5 KB：统计 6.4K + 1 Hz 快照 2K 对象队列 + 代码段）。

## B-AC1：`vghmi perf` 三态（通过）

- running：测量固件开机 HMI autostart 后，`state=running uptime_ms=6890 clock_res_us=10000 gran_us=10000 dropped=0 budget=6416`（transcript 149 行）。
- disabled：默认固件上 `vghmi perf: state=disabled (built without CONFIG_VG_HMI_PERF)`（board 实测）。
- not_running：主机逻辑覆盖（init 前 read 返回 false 即输出该态）；板端因 autostart 无未初始化窗口，未在真机复现该输出——记为代码路径已验证、真机未复现。
- perf 命令不启动第二个 HMI：命令即发即回，HMI 继续运行（commits 持续增长），未触发 `lv_is_initialized` 重复启动路径。

## B-AC2：热路径与预算（通过）

- 记录路径无 malloc/文件 I/O/排序/printf；写入方 trylock 不阻塞（host 并发测试：20000 样本 全部记录或计入 dropped，窗口恒 ≤256）。
- 静态 6416 字节（map 实测）；`dropped=0` 全程（无读取冲突丢样）。
- heap 读数来自 mallinfo(uordblks/fordblks)；本平台 fordblks=579 MB > total 7 MB，与 `/proc/meminfo` 不一致，属平台分配器上报怪癖（两种构建一致）。资源核对优先用 `/proc/meminfo`（total 7089328 used 213488）。

## B-AC3：默认/测量构建业务一致（通过）

- 默认构建无 vg_hmi_perf 符号（map 无引用），UI 观察点编译为空操作；两种构建均开机正常：HMI autostart、采集启动、Agent daemon、网络管理器行为一致（boot log 对照）。
- 残留防护：测量构建后运行默认构建，`.config` 复位为关闭（未 savedefconfig，defconfig 源文件未改）。

## B-AC4：无头双缓冲（通过）

- 16 位 RGB565（固件 LVGL checkout）：alarm/trend/fixture 3 测试全过。
- 32 位 XRGB8888（PC checkout gui/lvgl）：3 测试全过。
- 板容量 profile（16 位 + VG_MODEL_BOARD_CAPACITY，64 点/128 历史）：3 测试全过，fixture 断言"当前 128 点"。
- 像素非空断言（直方图非主导色 >5%）、中文标签可见、真实 pointer indev 点击（行/按钮/下拉）均通过。缓冲按 color format + stride 分配，未用 sizeof(lv_color_t)。

## B-AC5：真机原始采集（部分完成，其余如实未测）

已完成（L0，D1=14 点，测量固件， transcript `.debug/hmi-baseline/perf_transcript.txt`）：

| 指标 | t0 (uptime 6.9 s) | t1 (46.2 s) | t2 (81.4 s) |
|---|---|---|---|
| loop n / max | 195 / 330 ms | 1506 / 330 ms | 2690 / 330 ms |
| render n / max / p95 | 7 / 140 ms / 140 ms | 46 / 140 ms / 140 ms | 81 / 140 ms / 20 ms |
| flush n（=commits） | 12 | 88 | 158 |
| model_tick n / max | 6 / 30 ms | 45 / 30 ms | 80 / 30 ms |
| nav n / max | 1 / 190 ms | 1 / 190 ms | 1 / 190 ms |
| page objects / listeners / timers | 71 / 2 / 6 | 71 / 2 / 6 | 71 / 2 / 6 |
| heap used（mallinfo） | 263616 | 263656 | 263656 |
| cpu（/proc/cpuload） | 57.6% | 43.0% | 44.1% |

时钟粒度 10 ms（CONFIG_USEC_PER_TICK=10000 的实测体现）：所有亚 10 ms 段记录为 0，这是粒度限制不是数据缺失。由此已可确认的 L0 基线事实：

1. **静止首页仍有约 2 次提交/秒**（12→88→158 commits，Δ≈70/35 s）：无触摸、无数据变化时状态栏时钟标签每秒重绘。这是后续增量刷新任务（子任务 2）的直接优化目标。
2. 首次 render max 140 ms、首次 nav（首页构建）190 ms、单次 loop max 330 ms：均为开机首帧段（字体/布局冷启动），之后 render p95 降至 20 ms 量级。冷启动段与稳态段需分开对待。
3. 81 秒内 objects/listeners/timers/heap 稳定，无单向增长。
4. render/flush 事件配对完整（incomplete=0），提交计数与 flush 样本数一致。

统计开/关 L0 对照：开 42.0-57.6%（三次读数），关 53.9%/60.8%（两次读数）——单读数方差内无可辨差异；逐帧统计未在测量期间打印。

未测（阻塞项与复现条件）：

- L1/L2/L3 交互样本（切页、滚动、筛选、报告）：需要实体手指触摸与 >=120 FPS 拍摄（统一验收契约实测方法），当前会话无人手条件。软体段计时不能替代。
- D2 32 点板端表、D3 告警数据组：需按 `velaguard-candidate-confirm` 走候选表 + `vgpoint test` + 人工 `apply --confirm`，不可自动代答。
- L3 Agent 同固件负载：Agent 当前 lazy 模式（首次 ask 才起 loop）；系统挂死风险归口 `09-12-heartbeat-llm-round-system-wedge`。
- 无头 fixture 的 1/8 告警构造：PC 路径模型不评估阈值（set_live 阈值评估在 VG_HMI_BOARD 分支），0/2/3 已覆盖；8 告警由板端 D3 覆盖。

## B-AC6：统计开关同动作对照（L0 完成，交互未测）

L0 CPU 对照见上；卡顿无可辨差异。含触摸动作的对照属 L1 未测范围。

## B-AC7：后续任务可用性（就绪）

- 新增入口：`vg_hmi_perf.h`（六指标 + 资源快照 API，未启用时为空操作内联）；`vghmi perf` 只读命令。
- 构建入口：`scripts/build.sh --hmi-perf`；无头：`cmake -S gui/headless -B .debug/hmi-headless[-32|-board] ...` + `ctest`。
- 测试 fixture：`gui/headless/fixture_check_main.c`（空表/32 点/重复名/23 字符 ID/历史深度/场景告警），共享 harness `harness_common.c`。
- 后续 Agent 注意：行号以当前 diff 为准（vg_shell.c 导航计时、vg_hmi.c 观察块、vg_model.c 监听查询）；trend 页默认 60 点窗口，全窗口断言前需先点"全部"。

## 工具与命令

```bash
bash scripts/build.sh --hmi-perf     # 测量固件（用后默认构建即复位）
powershell.exe -ExecutionPolicy Bypass -File scripts/flash.ps1
powershell.exe -ExecutionPolicy Bypass -File .debug/hmi-baseline/collect_perf.ps1
cmake -S gui/headless -B .debug/hmi-headless -DVG_LVGL_DIR="$PWD/../apps/graphics/lvgl/lvgl" -DVG_HEADLESS_COLOR_DEPTH=16
cmake --build .debug/hmi-headless && ctest --test-dir .debug/hmi-headless --output-on-failure
make -C app/velaguard/host_tests test
```

COM3 备注：flash.ps1 烧录结束紧接 SerialPort.Open 可能偶发 UnauthorizedAccess（ST-LINK VCP 状态未稳定），等待数秒重试即可；本会话已复现并按此恢复，不需人工介入。
