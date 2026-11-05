# 10: 完成真实板端闭环验收

**What to build:** 在真实开发板上用受控发布器和黄金候选完成查询、下发、校验、人工确认、持久化和正常采集，并证明断网、掉电及失败配置不会破坏既有监控能力。

**Blocked by:** 09, 断线、重放与容量压力下保持配置一致.

**Status:** blocked by RS485 alarm precondition

- [ ] query→open→put→板端人工确认→applied→正常采集的真实证据完整，配置生效与从机在线分别记录。
- [x] 未确认、取消、过期、旧版本和失败整批拒绝均不改变运行表，确认前不访问新增候选。
- [ ] 拔网、重启和掉电恢复产生协议规定结果，原有采集、告警、LED 和显示继续运行。
- [ ] 汇总构建、烧录、主机测试、真实 Broker 和板端验收证据，并明确云端 PDF 解析与网页不在本票验收范围。

当前阻塞：

- 真机已在线到达 `awaiting_local_confirm`，但现有 14 个 Modbus 点全部离线，活动告警按产品要求遮挡本地确认界面。
- 未通过静音、确认、清除告警或注入假值绕过；需要恢复 RS485 从站或 MThings 模拟后，由人在屏幕点击确认。
- 阻塞证据见 [config-mqtt-local-confirm-alarm-block.log](../../../maintenance-evidence/2026-11-05/config-mqtt-local-confirm-alarm-block.log)。
