# OTA 任务 03：FAT/BCB 启动选择验收

日期：2026-10-28。开发板：STM32H750B-DK，COM3，现有 eMMC FAT16，64 KiB 簇。接续 QoderCN 会话 `4a468347-f446-4838-b85b-faa34ee57293` 的未提交实现，本轮补充边界检查、重新板测并提交。以下结果来自本轮实际执行。

## 实现范围

- `boot/ota_core/` 为应用和片内 boot stub 共用的纯 C 模块，包含 BPB/FAT16/FAT32 读取、4 KiB BCB 编解码与 CRC32、双份选序、启动决策。
- BCB 格式版本为 1，头部 140 字节，CRC32 覆盖前 136 字节，文件总长必须为 4096 字节。字段包括 seq、状态、槽位、镜像大小、trial_count、flags、版本、task_id 和 SHA-256。
- 序号按 uint32 回绕比较；相差 `0x80000000` 时无法确定先后，回 A。单份失效时可选择另一份有效 BCB。
- boot stub 使用 SDMMC1 单线轮询读取；所有硬件等待都有次数上限，不写 eMMC。普通启动路径只读 QSPI，检查目标向量后跳转。保留原有显式请求的 scratch 探测入口。
- FAT 只接受卷首 BPB、512 字节扇区、8.3 文件名。检查实际块设备容量、FAT 表长度、28 位簇号、目录项和文件范围；循环簇链使用常量内存检测，单次目录查找最多扫描 256 个扇区，即 128 KiB，超限回 A。
- 本轮不安装镜像、不验签、不核对 B 镜像 SHA-256、不更新 trial 计数或启用 trial 看门狗。pending 回 A；confirmed/trial 的 B 选择用于 ST-Link 预写镜像验收。后续安装、自检和回滚分别见任务 04、05。

硬件核对使用本地官方 `mb1381-h750xb-b01-schematic.pdf` 第 3、10 页，并对照同资料包 `bsp/stm32h750b_discovery_mmc.c` 与 NuttX 板级配置：PC8 为 D0、PC12 为 CK、PD2 为 CMD，使用 AF12。本轮不改接线、不格式化 eMMC。

## 构建与主机验证

执行 `make -C app/velaguard/host_tests test`，完整套件通过。新增 `test_ota_bcb`、`test_ota_fat`、`test_ota_boot`；覆盖 FAT16/32、CRC/字段/文件长度错误、文件缺失、簇链提前结束和循环、块读失败、越界、FAT 表过短、目录扫描预算、序号回绕与歧义。FAT 和启动决策另以 AddressSanitizer/UBSan 运行，通过。当前主机环境链接 sanitizer 需要 `-lm`。

诊断构建执行 `bash scripts/build.sh --ota-fixtures`，生产构建执行 `bash scripts/build.sh`，两者均通过 A/B 镜像检查：

```text
诊断：check_fw_images: OK version=0.1.0 A=0x90000000 B=0x90800000 a_size=1517120 b_size=1517120
生产：check_fw_images: OK version=0.1.0 A=0x90000000 B=0x90800000 a_size=1512704 b_size=1512704
最终 boot stub：text=6184 data=0 bss=16；nm -u 无未解析符号
# CONFIG_VG_OTA_TEST_FIXTURES is not set
```

构建脚本每次重建 stub，链接器检查片内 Flash 容量和至少 32 KiB 栈空间。`scripts/flash.ps1 -WithSlotB` 显式写入诊断 A/B 与 stub；普通烧录只写 A 和 stub。两种路径均完成写入校验。

## 板级验收

诊断固件先由 ST-Link 预写 B，再通过测试 BCB 选择启动槽位。实际固件版本始终为 `0.1.0`；fixture 的 `0.2.0` 只是 BCB 测试字段。构建号为 `8aae0cad7c3c-dirty`，如实反映提交前构建。

| 条件 | 实际结果 |
|---|---|
| 两份 BCB 均缺失 | A，`a-no-bcb` |
| BCB0 confirmed B，seq=1 | B，`b-confirmed`，bcb=0 |
| BCB0 CRC 损坏，BCB1 confirmed B，seq=2 | B，bcb=1，valid0=0、valid1=1 |
| 两份 BCB CRC 均损坏 | A，`a-bcb-invalid` |
| BCB0 为 A/0xffffffff，BCB1 为 B/0 | B，选择 BCB1/seq=0 |
| 两份有效 BCB 序号相差 0x80000000 | A，`a-bcb-invalid` |
| 两份 BCB 都只有 4095 字节 | A，`a-bcb-invalid`，bcb_error=22 |
| pending B | A，`a-pending` |
| confirmed B，但 B 槽为全 0xFF | A，`a-image-invalid` |

串口摘录见 [t03-serial-excerpt.txt](t03-serial-excerpt.txt)。FAT32、损坏 BPB、循环簇链和超范围块读通过内存块设备主机测试验证；没有破坏真实 eMMC FAT 来模拟这些条件。

诊断板测前后分别回读完整 8 MiB A 槽，SHA-256 一致：

```text
before = a78676bf955272674216b96a6a029c43995f4a96f6a68bc78a5b8867a6b51ed3
after  = a78676bf955272674216b96a6a029c43995f4a96f6a68bc78a5b8867a6b51ed3
```

之后使用 ST-Link 将测试 B 槽完整恢复为 0xFF，回读全部 8 MiB 校验：

```text
SHA256 = 9f9b02f5ee6cbef5e018c1ee424095fc21a842ea6968c0d36114b5930dab2ba1
```

## 收尾状态与限制

清除两份测试 BCB 后，使用 ST-Link 烧录最终生产 A 镜像和 stub，B 保持空白。板上复核：`identity` 为 A/0x90000000，`bootinfo` 为 `a-no-bcb`，FAT/eMMC 错误均为 0，VGOTA 目录为空。生产固件拒绝 fixture 命令，使用说明不含该入口，二进制也不包含 fixture 使用字符串。

上述 A 槽摘要比较针对诊断板测区间；恢复生产固件时，由 ST-Link 主动替换 A 镜像。最终 `probe` 中的 QSPI scratch 成功状态来自之前留存的 mailbox，本轮没有重新执行 scratch 擦写，不能把它当作本轮新增探测证据。

本轮未验收完整在线 OTA、签名信任、本地安装确认、断电恢复或 trial 自检失败回滚，也未进行 LLM 对话及长期稳定性验收。

提交前运行官方日志校验：检查 323 份文件、76765 个事件，通过并带一条警告，指出 2026-09-16 的一份既有 Claude Code 日志未列入 manifest.sessions。本轮未改写日志或清单。
