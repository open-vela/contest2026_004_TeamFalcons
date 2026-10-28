# 07: 本地 Install Confirmation 与安装交接

**What to build:** READY 状态只接受板载触摸或按键产生的 Install Confirmation。用户长按安装或点击取消；超时后丢弃暂存并上报 expired。确认时再次检查高等级活动告警、存储可写、摘要匹配、设备空闲和 RJ45 链路。通过后写 pending BCB、同步并重启，由 boot stub 安装 B，B 自检成功后写 confirmed。使用 fixture 完成无网络的全链路本地 OTA。

**Blocked by:** 05、06。

**Status:** ready-for-agent

- [ ] 只有板载触摸或按键可以触发安装，MQTT、手机、语音识别结果和设备端 Agent 都没有调用路径。
- [ ] 长按执行安装，点击取消进入 declined，超时进入 expired；相同 task_id 不再重复弹窗。
- [ ] 联锁检查失败时留在 READY 并显示原因，不写 pending BCB，不重启。
- [ ] 确认成功后写 pending BCB、重启、安装 B，并在 B 自检成功后显示 confirmed。
- [ ] pending BCB 写入失败时留在 READY 并显示错误，不执行重启。
- [ ] 主机测试证明只有本地确认端口可以触发安装，控制通道和 Agent 路径不能。
- [ ] 板级验收用 fixture 完成 READY、长按、重启、B 启动和 confirmed 的完整证据。
