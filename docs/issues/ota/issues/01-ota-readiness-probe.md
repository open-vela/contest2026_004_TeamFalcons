# 01: OTA 就绪探测与测试 fixture 通道

**What to build:** 板上可以直接报告 OTA 所需硬件事实，并能在测试构建中准备 fixture 暂存内容。报告内容包括 eMMC CID、CSD、EXT_CSD 推导出的实际容量、FAT 类型、剩余空间，以及 QSPI JEDEC ID、实际容量、擦除粒度。QSPI scratch 区执行一次擦除、写入和回读校验，不触碰 A 槽和 B 槽。测试构建提供 fixture 通道，把测试镜像、manifest 和 BCB 写入 OTA Staging Directory；生产构建不包含该通道。整个过程不格式化 eMMC，不擦写 A/B 槽，不输出凭据。

**Blocked by:** 无，可以立即开始。

**Status:** ready-for-agent

- [ ] 板上命令或状态输出显示 eMMC 实测容量、FAT 类型和剩余空间，容量与 CID、CSD、EXT_CSD 的读取结果一致。
- [ ] 输出显示 QSPI JEDEC ID、实际容量和擦除粒度，scratch 擦除、写入、回读校验通过。
- [ ] 探测过程不格式化 eMMC，不清除 `/data`，现有 AI 凭据、点表和运行配置保持可读。
- [ ] fixture 通道只在测试构建中存在，生产 defconfig 构建无法调用。
- [ ] 探测和 fixture 输出不包含 MQTT 密码、API key、完整下载 URL 或签名私钥。
- [ ] 主机测试覆盖容量和 FAT 信息格式化、错误输入和输出边界；板级记录真实命令输出。
