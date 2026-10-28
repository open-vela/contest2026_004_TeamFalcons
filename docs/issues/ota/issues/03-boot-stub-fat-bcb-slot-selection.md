# 03: boot stub 读取 FAT/BCB 并选择槽位

**What to build:** boot stub 能从现有 eMMC FAT 中读取 OTA Staging Directory，解析只读 FAT16/32，读取两份 BCB，并按最高有效 seq 选择启动槽位。测试时用 ST-Link 预写一个 B 镜像，再由 fixture 写入有效 B BCB，设备能启动 B；BCB 缺失、CRC 错误、seq 无效、FAT 不支持或簇链损坏时启动 A。此票只读启动控制，不擦写 QSPI。

**Blocked by:** 01、02。

**Status:** ready-for-agent

- [ ] 有效 B BCB 且 B 镜像存在时，boot stub 启动 B，状态显示槽位 B。
- [ ] BCB 缺失、CRC 错误或两份都无效时，boot stub 启动 A，不挂起。
- [ ] FAT 类型不支持、目录项异常、簇链损坏、文件缺失或超出槽位时，boot stub 启动 A。
- [ ] boot stub 选择最高有效 seq，并能处理 seq 回绕和一份 BCB 损坏。
- [ ] 此阶段 boot stub 不擦除或写入 QSPI，A 槽内容保持不变。
- [ ] 主机测试覆盖 FAT16、FAT32、BCB 选序、损坏输入和越界读取。
- [ ] 板级验收记录 B 启动和各类损坏回到 A 的串口证据。
