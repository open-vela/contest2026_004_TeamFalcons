# OTA P0 板级探测

日期：2026-10-28。板：STM32H750B-DK。固件：`velaguard-lvgl`，片内 boot stub 加 `vgota` 探测命令。

## 命令

```text
vgota probe
vgota qspiprobe --confirm
# 板重启后
vgota qspiprobe
```

## eMMC 实测

```text
vgota: emmc cid mid=13 oid=4e pnm=Q2J54A prv=10 psn=0d8cddf6 mdt=b5 crc=7a
vgota: emmc csd struct=3 spec=4 csize=4095 mult=7 read_bl_len=9 capacity=0
vgota: emmc extcsd rev=7 sec_count=7634944 capacity=3909091328
vgota: emmc geometry sectors=7634944 sector_size=512 capacity=3909091328
vgota: emmc fat type=FAT16 bps=512 spc=128 reserved=1 fats=2 root_entries=512 clusters=59644 total_sectors=7634944
vgota: emmc statfs type=00004d44 bsize=65536 total=3908829184 free=3884580864
```

结论：eMMC 用户分区容量 3,909,091,328 字节，约 3.64 GiB，属于 4GB 级器件。EXT_CSD 的 SEC_COUNT、块设备 geometry 和 FAT 总扇区一致。FAT 为 FAT16，512 字节扇区，128 扇区每簇（64 KiB 簇）。手册原 8GB 记录不成立。

原理图 B-01 的 eMMC 型号为 KIOXIA `THGBMTG5D1LBAIL`，与 4GB 级实测一致。

## QSPI 实测

```text
vgota: qspi state=ok error=0 jedec1=20ba20 jedec2=000000 capacity=67108864 (64 MiB) erase=4096 write=256
```

结论：当前板只响应一颗 MT25QL512ABB，JEDEC ID `0x20BA20`，64MB。第二片未响应，`jedec2=0`，容量按单颗 64MB 计算。4 KiB 扇区擦除和 256 字节页写在 flash1 的 `0x03FF0000` scratch 区完成擦除、写入、回读比较，未触碰 A/B 槽。

QSPI 地址窗口已从 128MB 修正为 64MB，涉及链接脚本、MPU XIP 窗口、boot stub 的 APP_LIMIT 和 flash 脚本范围检查。A 槽 `0x90000000` 和 B 槽 `0x90800000` 各 8MB，均在 64MB 内。

## fixture 通道与生产排除

诊断构建 `bash scripts/build.sh --ota-fixtures` 打开 `CONFIG_VG_OTA_TEST_FIXTURES`。板上执行：

```text
vgota fixture write IMAGE.BIN 1024 1
vgota fixture write MANIFEST.BIN 512 2
vgota fixture write BCB0.BIN 4096 3
vgota fixture write STATE.BIN 512 5
vgota fixture status
vgota fixture clear
vgota fixture status
```

写入后状态显示 IMAGE.BIN、MANIFEST.BIN、BCB0.BIN、STATE.BIN 存在且大小正确，clear 后全部缺失。整个过程只创建和使用 `/mnt/emmc/VGOTA/`，未格式化 eMMC。

随后执行 `bash scripts/build.sh --clean` 回到生产配置：`CONFIG_VG_OTA_TEST_FIXTURES` 未设置，固件二进制不含 fixture 字符串；板上 `vgota fixture status` 只打印通用 usage，不能执行 fixture 操作。

## 限制与后续

- scratch 探测使用固定延时，不轮询 WIP 状态寄存器；P1 的正式安装器仍须实现可靠的忙状态轮询或自动轮询。
- 第二颗 Flash 未响应可能是未贴片或该板只装一颗；P1 不按 128MB 设计，容量按实测 64MB。
- 探测只写 flash1 最后一个 64KB 扇区，未修改 A/B 槽和 eMMC 文件系统。
- 原始串口日志保存在本地 `.scratch/ota-probe/`，未提交；本文件只保留与验收相关的去敏输出。
