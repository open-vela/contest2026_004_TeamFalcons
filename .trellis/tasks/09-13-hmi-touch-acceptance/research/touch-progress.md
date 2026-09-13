# 触摸验收进度

日期：2026-09-13。任务：`09-13-hmi-touch-acceptance`。

## 已完成

| 项 | 证据 |
|---|---|
| T1–T3 | 调度、导航恢复、手势冻结、按下身份、热区 |
| T4 无头扩展 | I01/I02/I11–I13/I16–I19/I22/I24；16/32 位 ctest 5/5 |
| 产品 HMI accept | pass=16 fail=0 |
| 默认固件复核 | `vghmi perf: state=disabled`；见 `final-results.md` |
| 优化后 L0 | 父任务对照 commits 158→9 |

## 复跑命令

```bash
make -C app/velaguard/host_tests test
ctest --test-dir .debug/hmi-headless --output-on-failure
ctest --test-dir .debug/hmi-headless-32 --output-on-failure
powershell.exe -ExecutionPolicy Bypass -File scripts/stage1_lvgl_hmi_accept.ps1
```

## 仍待 T5 / 人工（阻塞关闭）

- 实体触摸录像与 L1/L2 P95/FPS
- D2/D3 数据组与 30 分钟资源记录
- L3 Agent 同固件压力
- 人工 LCD 目视

详见 `research/final-results.md`。
