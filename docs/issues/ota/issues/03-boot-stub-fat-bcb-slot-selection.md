# 03: boot stub 读取 FAT/BCB 并选择槽位

**What to build:** boot stub 能从现有 eMMC FAT 中读取 OTA Staging Directory，解析只读 FAT16/32，读取两份 BCB，并按最高有效 seq 选择启动槽位。测试时用 ST-Link 预写一个 B 镜像，再由 fixture 写入有效 B BCB，设备能启动 B；BCB 缺失、CRC 错误、seq 无效、FAT 不支持或簇链损坏时启动 A。此票只读启动控制，不擦写 QSPI。

**Blocked by:** 01、02。

**Status:** done

- [x] 有效 B BCB 且 B 镜像存在时，boot stub 启动 B，状态显示槽位 B。
- [x] BCB 缺失、CRC 错误或两份都无效时，boot stub 启动 A，不挂起。
- [x] FAT 类型不支持、目录项异常、簇链损坏、文件缺失或超出槽位时，boot stub 启动 A。
- [x] boot stub 选择最高有效 seq，并能处理 seq 回绕和一份 BCB 损坏。
- [x] 此阶段 boot stub 普通启动路径不擦除或写入 QSPI，A 槽内容保持不变；原有显式 scratch 探测入口保留。
- [x] 主机测试覆盖 FAT16、FAT32、BCB 选序、损坏输入和越界读取。
- [x] 板级验收记录 B 启动和各类损坏回到 A 的串口证据。

2026-10-28 完成，见 [验收证据](../../../ota-evidence/2026-10-28/t03-boot-selection.md)。FAT32/损坏 BPB/簇链通过主机测试验证；真机验证 FAT16、BCB 损坏、序号回绕、无效 B 镜像回 A，完整 8 MiB A 槽摘要前后一致。最终恢复生产 A 固件、空白 B 和空 VGOTA 目录。

pending 安装、镜像摘要验证、trial 计数和自检回滚不在本票范围，继续由任务 04、05实现。
