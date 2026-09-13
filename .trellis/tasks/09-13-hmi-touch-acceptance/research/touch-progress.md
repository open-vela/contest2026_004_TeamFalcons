# 触摸验收进度（自动部分）

日期：2026-09-13。任务：`09-13-hmi-touch-acceptance`。

## 已完成

| 项 | 证据 |
|---|---|
| T1 10 ms indev + sleep 1..10 | 板端 `vghmi: pointer indev read=10ms n=1`；`test_hmi_sched` |
| T2 值类型导航恢复 | 首页筛选/滚动、趋势窗口、告警选中、报告滚动；切页 `wait_release` |
| T3 手势冻结 + 按下身份 | 首页/告警 SCROLL_BEGIN/END；结构 pending + 锚点恢复；热区返回 48×28、chip 44×28 |
| T4 无头交互子集 | `interaction_check`：I01/I02/I11/I12/I17/I18/I19；16/32 位 ctest 5/5 |
| 产品 HMI accept | `stage1_lvgl_hmi_accept.ps1` pass=16 fail=0（fleet n 与 acq 对齐；无从站 live SKIP） |
| 优化后 L0（父任务对照） | `.debug/hmi-post-opt/`；commits 158→9；见父任务 `final-results.md` |

## 复跑命令

```bash
make -C app/velaguard/host_tests test
ctest --test-dir .debug/hmi-headless --output-on-failure
ctest --test-dir .debug/hmi-headless-32 --output-on-failure
powershell.exe -ExecutionPolicy Bypass -File scripts/stage1_lvgl_hmi_accept.ps1
```

## 仍待 T5 / 人工

- 实体触摸录像与 L1/L2 P95/FPS（统一验收契约）
- D2/D3 数据组与 30 分钟资源记录
- L3 Agent 同固件压力（关联 `09-12-heartbeat-llm-round-system-wedge`）
- 人工 LCD 目视（home / alarm AI 块等 accept NOTE）
