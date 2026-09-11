# 无物理拔线的质量门禁

## 决策来源

实现侧无法方便地做实验室拔线验收。本轮完成条件是代码质量，不是现场 PHY 操作。

## 两层验证

### 1. 主机 gcc 单测（完成门禁）

把优先级、稳定窗口、退避、单出口、MQTT reconnect 请求抽成无 NuttX 依赖的 `vg_net_policy.c`。

用确定性时钟（测试传入 `now_ms`）覆盖：

- RJ45 优先
- RJ45 down → Wi-Fi
- 双 down → `NET_DOWN`
- 稳定窗口内不切回、窗口满切回
- link flap 不抖动
- 分 bearer 退避与立即尝试
- 失败阈值触发复位端口
- 出口变化产生 MQTT reconnect + `status.network`

### 2. 板上故障注入（编译进固件，供人工选用）

NSH `vgnet inject rj45 down|up` / `vgnet inject wifi down|up` 覆盖真实 PHY 采样。
Agent 不把「人去拔网线」列为 AC。用户若要演示物理世界，可自行拔线；策略应与注入走同一函数。

## 明确不作为完成条件

- Agent 到板旁拔/插 RJ45
- Agent 配置现场 Wi-Fi AP 并观察云 Broker
- LVGL 显示网络图标
