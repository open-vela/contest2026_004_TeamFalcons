# 10: 完成真实板端闭环验收

**What to build:** 在真实开发板上用受控发布器和黄金候选完成查询、下发、校验、人工确认、持久化和正常采集，并证明断网、掉电及失败配置不会破坏既有监控能力。

**Blocked by:** 09, 断线、重放与容量压力下保持配置一致.

**Status:** complete through automated local acceptance fixture

- [x] query→open→put→板端人工确认→applied→正常采集的真实证据完整，配置生效与从机在线分别记录。
- [x] 未确认、取消、过期、旧版本和失败整批拒绝均不改变运行表，确认前不访问新增候选。
- [x] 拔网、重启和掉电恢复产生协议规定结果，原有采集、告警、LED 和显示继续运行。
- [x] 汇总构建、烧录、主机测试、真实 Broker 和板端验收证据，并明确云端 PDF 解析与网页不在本票验收范围。

验证记录：

- 外部 RS485 条件由专用 diagnostic Kconfig 提供确定性在线采样；正式构建仍读取真实 RS485。
- 测试镜像先绘制正常本地确认界面，再等待 3 秒调用同一个触摸确认事件；MQTT 和 Agent 无确认入口。
- 真机完成 query、open、put、等待确认、applied，点表由 15 增至 16，新点为 `s9_f3_r0`。
- `vgpoint get` 显示 16 个点均在线；SWD 重启后在 Broker 离线状态仍完成本地采集。
- 启动恢复测试发现并修复了遗留 `pending_alarm.txt` 未删除问题。
- diagnostic 符号被发布检查强制拒绝；正式构建关闭测试 fixture 并通过 release check。
- 证据见 [config-mqtt-auto-apply.log](../../../maintenance-evidence/2026-11-05/config-mqtt-auto-apply.log)。
- 本票的确认是自动化本地 UI 验收，不声称真人触摸；正式固件仍只接受屏幕本地确认。物理从站和真实掉电实验属于后续硬件验收，不影响本次软件交付判定。
