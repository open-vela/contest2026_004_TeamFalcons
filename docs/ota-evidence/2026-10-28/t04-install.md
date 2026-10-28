# OTA 任务 04：暂存镜像安装到 B

日期：2026-10-28。任务 04 已完成：用户本机按键确认后，boot stub 将暂存镜像安装到 B，回读摘要并持久化 TRIAL，成功启动 B；完整 8 MiB A 槽摘要前后一致。完整 OTA、正式应用验签/屏幕联锁、trial 自检与回滚仍未完成。

## 已实现

- 固定 512 字节 manifest、CRC/字段绑定、流式 SHA-256、镜像范围和向量校验。
- pending 安装核心通过端口访问块设备和 QSPI。擦写前持久化较新 FAILED 并回读，成功回读镜像摘要后再提交 TRIAL，保留失败恢复记录。
- eMMC 只写已有 BCB 首扇区，不修改 FAT 或目录；写后等待卡就绪，缓存开启时执行 FLUSH_CACHE。
- QSPI 核心和硬件适配层分别限制只写 B，按 DFM 使用 8192 字节逻辑擦除步长、256 字节编程；成对 ID、WEL 和忙状态检查、实际安装与完整镜像回读已通过。该结果只验证当前映射下的安装路径，不证明封装数量或总容量。
- 诊断 stage 从独立测试传输窗口复制 B 镜像到 eMMC，生成真实摘要；arm 只接受先释放再长按 USER 键 2 秒的本地事件。生产应用排除 fixture，生产 stub 拒绝 fixture manifest。

格式、信任范围及持久化顺序见 [启动安装格式](../../velaguard-ota-boot-format.md)。正式应用验签、屏幕确认、联锁以及 trial 自检/看门狗尚未实现，分别属于后续任务。

## 主机测试

执行 `make -C app/velaguard/host_tests test`，完整套件通过。新增 `test_ota_install` 验证：

- SHA-256 空串、abc 和一百万个 a 标准向量。
- FAT16/32 安装成功、最后一个不完整编程块、TRIAL 持久化后重新选择 B。
- manifest CRC、字段绑定、镜像摘要、设备身份、范围、本地确认标志和生产拒绝 fixture。
- 擦除、编程、回读、摘要不一致、同步失败时回 A。
- 59 个操作边界，分别在操作前断电及部分写入后断电，合计 118 个模拟场景；完整 A 槽始终不变，安装不完整时不选 B。

同一测试使用 AddressSanitizer/UBSan 通过。模拟不替代真实断电验收。

## 构建与栈

诊断构建命令为 `bash scripts/build.sh --ota-fixtures`：

```text
check_fw_images: OK version=0.1.0 A=0x90000000 B=0x90800000 a_size=1520904 b_size=1520904
boot stub (fixture): text=11052 data=0 bss=20
boot stub (production): text=11048 data=0 bss=20
```

`nm -u` 无未解析符号。`-fstack-usage -Os` 测得安装函数 18008 字节、BCB 写入函数 4632 字节；两者嵌套约 23 KiB，链接器仍要求至少 32 KiB 栈余量。诊断构建号为 `72fd0e1d9d84-dirty`，未把提交前镜像描述为干净构建。

最终完整生产构建执行 `bash scripts/build.sh`，从代码提交 `c6c163af5172` 的干净工作区构建，检查通过：

```text
check_fw_images: OK version=0.1.0 A=0x90000000 B=0x90800000 a_size=1512800 b_size=1512800
# CONFIG_VG_OTA_TEST_FIXTURES is not set
```

生产应用二进制不含 fixture 使用字符串，板上调用 fixture 只返回正式命令使用说明。恢复使用普通 `scripts/flash.ps1`，只写 A 和 stub。

## 真机证据

ST-Link 写入诊断 A 和 stub；B 沿用任务 03 收尾的空白状态。另将传输副本写到 `0x91000000`，没有预写 B。暂存镜像摘要为：

```text
82ecec13519e8878dacf183de2f44e2fc6a1fb4a44e27e2af2532834f2e5b2b2
```

板端 stage 报告相同前缀 `82ecec13`、大小 1520904，生成 512 字节 manifest 和两份 4096 字节 FAILED BCB。未发生安装授权。测试用未确认 pending 的结果：

```text
vgota: identity version=0.1.0 slot=A build=72fd0e1d9d84-dirty link=90000000
vgota: bootinfo slot=A reason=a-install-failed bcb=0 state=pending seq=20 bcb_slot=B valid0=1 valid1=1 bcb_error=0 fat=FAT16 fat_error=0 emmc_error=0
vgota: install stage=1 error=22 bytes=0
```

随后重新 stage `T04-INSTALL`、seq=30，恢复两份 FAILED。用户明确表示已到板旁，并在 60 秒窗口内实际长按 USER 键，设备写入与该镜像绑定的 pending：

```text
vgota: fixture local confirmation version=0.1.0 task=T04-INSTALL bytes=1520904
vgota: fixture pending locally confirmed seq=30
```

执行 `vgota reboot` 后，安装完成并启动 B：

```text
vgota: identity version=0.1.0 slot=B build=72fd0e1d9d84-dirty link=90800000
vgota: bootinfo slot=B reason=b-installed bcb=1 state=trial seq=32 bcb_slot=B valid0=1 valid1=1 bcb_error=0 fat=FAT16 fat_error=0 emmc_error=0
vgota: install stage=8 error=0 bytes=1520904
vgota: fixture BCB0.BIN valid seq=31 state=failed slot=B size=1520904 version=0.1.0 task=T04-INSTALL trials=0
vgota: fixture BCB1.BIN valid seq=32 state=trial slot=B size=1520904 version=0.1.0 task=T04-INSTALL trials=1
```

ST-Link 回读完整 8 MiB B：前 1520904 字节与暂存镜像逐字节相同，SHA-256 为 `82ecec13519e8878dacf183de2f44e2fc6a1fb4a44e27e2af2532834f2e5b2b2`，余下全部为 0xFF。完整 8 MiB A 槽在安装前后的摘要均为：

```text
3836c587f8e77ffd27b6661da7574bf78e12e5100acda4bbde32720375498ee4
```

再次重启后 `reason=b-trial`、seq=32，证明 TRIAL 来自 eMMC 持久化记录；此时 `install stage=0`，没有重新安装。未自动写 confirmed，trial 计数递增和自检仍属于任务 05。

P0 曾把交错输出的前三字节及 FSELECT 行为解释为单器件/第二片缺失。本次适配层按 DFM 要求六字节 `20 20 ba ba 20 20`，并通过实际擦写与镜像回读；因此更正活动文档中的旧解释，保留 P0 原始证据。软件地址上限仍为 64 MiB，不据此扩大总容量预算或槽位。

串口摘录见 [t04-serial-excerpt.txt](t04-serial-excerpt.txt)。提交前官方日志校验通过，保留一条既有 orphan 日志警告；未改写日志或清单。

## 收尾与限制

从 B 清除六个固定 fixture 文件后重启，回到 A、`a-no-bcb`，VGOTA 目录为空。ST-Link 清空测试 B 和传输副本；完整回读 8 MiB B 验证为全 0xFF，SHA-256 为 `9f9b02f5ee6cbef5e018c1ee424095fc21a842ea6968c0d36114b5930dab2ba1`。随后恢复生产 A 和 stub，最终身份为 `0.1.0/A`、构建号 `c6c163af5172`，FAT/eMMC 错误为 0，fixture 入口排除。

A 摘要比较针对诊断安装区间；最后由 ST-Link 主动替换为生产 A。没有格式化 eMMC，没有修改凭据、已确认点表或原始 AI Coding 日志。

真实电源中断、trial 看门狗/自检失败回滚、正式签名信任链、屏幕联锁及网络 OTA 均未在本票验收；断电和撕裂写入结论来自 118 个主机模拟场景，不冒充电源实测。后续故障注入和长测按任务 10 记录。
