# 01: OTA 就绪探测与测试 fixture 通道

**What to build:** 板上可以直接报告 OTA 所需硬件事实，并能在测试构建中准备 fixture 暂存内容。报告内容包括 eMMC CID、CSD、EXT_CSD 推导出的实际容量、FAT 类型、剩余空间，以及 QSPI JEDEC ID、实际容量、擦除粒度。QSPI scratch 区执行一次擦除、写入和回读校验，不触碰 A 槽和 B 槽。测试构建提供 fixture 通道，把测试镜像、manifest 和 BCB 写入 OTA Staging Directory；生产构建不包含该通道。整个过程不格式化 eMMC，不擦写 A/B 槽，不输出凭据。

**Blocked by:** 无，可以立即开始。

**Status:** done

- [x] 板上命令或状态输出显示 eMMC 实测容量、FAT 类型和剩余空间，容量与 CID、CSD、EXT_CSD 的读取结果一致。
- [x] 输出显示 QSPI JEDEC ID、实际容量和擦除粒度，scratch 擦除、写入、回读校验通过。
- [x] 探测过程不格式化 eMMC，不清除 `/data`，现有 AI 凭据、点表和运行配置保持可读。
- [x] fixture 通道只在测试构建中存在，生产 defconfig 构建无法调用。
- [x] 探测和 fixture 输出不包含 MQTT 密码、API key、完整下载 URL 或签名私钥。
- [x] 主机测试覆盖容量和 FAT 信息格式化、错误输入和输出边界；板级记录真实命令输出。

## Evidence

2026-10-28 板级验收，详见 [OTA P0 证据](../../../ota-evidence/2026-10-28/p0-probe.md)。

- eMMC：3,909,091,328 字节（4GB 级），EXT_CSD SEC_COUNT、geometry 和 FAT 总扇区一致；FAT16、512 字节扇区、64 KiB 簇。
- QSPI：单颗 MT25QL512ABB，JEDEC `0x20BA20`，64MB；第二片未响应；4 KiB 擦除、256 B 页写；flash1 的 `0x03FF0000` scratch 擦除、写入、回读通过。
- 地址窗口已按 64MB 修正：链接脚本、MPU XIP、boot stub 的 APP_LIMIT、flash 脚本范围检查。
- 测试构建 `--ota-fixtures` 下，板上写入 IMAGE.BIN、MANIFEST.BIN、BCB0.BIN、STATE.BIN，状态列出正确，clear 后全部缺失；未格式化 eMMC。
- 生产 `--clean` 构建中 `CONFIG_VG_OTA_TEST_FIXTURES` 未设置，固件不含 fixture 字符串；板上 `vgota fixture status` 只打印通用 usage，不能执行 fixture 操作。
- 主机测试 `test_ota_probe` 覆盖 CID、CSD、EXT_CSD、FAT16/FAT32、损坏签名、exFAT 和不支持的扇区/簇大小。
