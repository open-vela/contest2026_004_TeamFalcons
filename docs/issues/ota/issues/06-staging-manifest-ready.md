# 06: 暂存存储、manifest 校验与 READY

**What to build:** fixture OTA Offer 把 Staging Image、manifest 和状态文件写入现有 eMMC FAT 的 OTA Staging Directory。镜像先写临时文件，fsync 和回读后改名；写入前检查剩余空间。设备验证测试签名 manifest 的 Device ID、型号、目标槽位、版本、时间窗和镜像摘要，进入 READY_AWAITING_CONFIRM，并在屏幕显示版本、大小和摘要前缀。此票不执行安装。

**Blocked by:** 03。

**Status:** done

- [x] fixture Offer 写入镜像、manifest 和状态文件，临时文件不会被当作有效 Staging Image。
- [x] 剩余空间不足时在写入前拒绝任务，并上报原因。
- [x] 测试签名 manifest 的 Device ID、型号、槽位、版本、时间窗或摘要不匹配时拒绝，不进入 READY。
- [x] 有效 fixture 进入 READY，屏幕显示版本、大小和摘要前缀，不触发安装。
- [x] 暂存写入过程断电或改名失败时没有 pending BCB，当前固件继续运行。
- [x] 主机测试覆盖临时文件、改名、fsync 失败、空间不足和 manifest 拒绝矩阵。
- [x] eMMC 不被格式化，现有 `/data` 文件和凭据保持可读。

2026-10-28：主机测试（`test_ota_staging`）与真机验收全部通过。验证了 eMMC FAT 空间不足拒绝、Manifest 拒绝矩阵（错误 Device ID、型号、槽位、时间窗、摘要不匹配）、写临时文件与原子改名、合法 Offer 进入 `READY_AWAITING_CONFIRM`、重启不触发安装保持 A 槽稳定运行，以及 `/data` 配置凭据未受破坏。证据见 [任务 06 证据](../../../ota-evidence/2026-10-28/t06-staging.md)。后续阶段为任务 07 本地安装确认与联锁。
