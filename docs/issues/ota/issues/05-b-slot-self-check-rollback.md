# 05: B 槽自检、confirmed 与回滚

**What to build:** B 槽首次启动后执行新固件自检。自检成功向另一份 BCB 写 confirmed，后续启动直接进入 B。自检失败写 failed 并重启到 A；B 启动挂起时由 trial 看门狗复位，trial 次数超限后 boot stub 写 failed 并回到 A。设备上报 self_check_failed 或 rolled_back，A 槽始终保持不变。

**Blocked by:** 04。

**Status:** done

- [x] B 自检成功时写 confirmed，重启后 boot stub 直接启动 B。
- [x] 自检失败时写 failed 并重启，boot stub 启动 A，状态和上报显示自检失败。
- [x] B 启动挂起时 trial 看门狗触发复位，trial 计数增加；超过默认次数后回滚 A。
- [x] A 槽内容和 A 槽启动能力在整个回滚过程中保持不变。
- [x] 回滚后设备回到可采集、可告警、可显示的稳定状态，不进入启动循环。
- [x] 主机测试覆盖 trial 次数、confirmed/failed BCB 写入和回滚选择。
- [x] 板级验收使用一个故意自检失败的 B 镜像完成回滚证据。

2026-10-28：完整主机测试（`test_ota_boot`、`test_ota_install`、`test_ota_self_check`、`test_mqtt_payload`）全部通过。板级真机验收覆盖：按键确认安装启动 B 首次自检通过并写入 `CONFIRMED`、重启后直接启动 B（`reason=b-confirmed`）、B 槽失败主动回滚到 A 槽（`reason=a-failed`）、以及硬件看门狗超时复位与 trial 计数自动步进 1->2->3 超限后降级回滚 A 槽。清除测试文件并擦除 B 槽，恢复生产 A 槽运行。证据见 [任务 05 证据](../../../ota-evidence/2026-10-28/t05-rollback.md)。后续阶段为任务 06 暂存存储与清单准备。
