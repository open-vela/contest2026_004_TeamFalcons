# OTA 任务 04：暂存镜像安装到 B

日期：2026-10-28。状态：代码和主机验证完成；真机已完成暂存和未确认请求拒绝，等待用户本机按键确认后继续擦写、B 启动及 A 摘要复核。不能将本记录视为任务 04 已完成或完整 OTA 已实现。

## 已实现

- 固定 512 字节 manifest、CRC/字段绑定、流式 SHA-256、镜像范围和向量校验。
- pending 安装核心通过端口访问块设备和 QSPI。擦写前持久化较新 FAILED 并回读，成功回读镜像摘要后再提交 TRIAL，保留失败恢复记录。
- eMMC 只写已有 BCB 首扇区，不修改 FAT 或目录；写后等待卡就绪，缓存开启时执行 FLUSH_CACHE。
- QSPI 核心和硬件适配层分别限制只写 B，按 DFM 使用 8192 字节逻辑擦除步长、256 字节编程；安装前核对成对 ID、WEL 和忙状态。实际擦写路径仍待本机确认后的板测验证，不能当作已经验证的硬件几何结论。
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

`nm -u` 无未解析符号。`-fstack-usage -Os` 测得安装函数 18008 字节、BCB 写入函数 4632 字节；两者嵌套约 23 KiB，链接器仍要求至少 32 KiB 栈余量。构建号为 `72fd0e1d9d84-dirty`，未把提交前镜像描述为干净构建。生产 stub 已单独构建，完整生产固件及最终恢复待板测收尾时执行。

## 当前真机证据

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

随后重新 stage `T04-INSTALL`、seq=30，恢复两份 FAILED，等待本机按键确认。安装前完整 8 MiB A 槽摘要：

```text
3836c587f8e77ffd27b6661da7574bf78e12e5100acda4bbde32720375498ee4
```

串口摘录见 [t04-serial-excerpt.txt](t04-serial-excerpt.txt)。提交前官方日志校验通过，保留一条既有 orphan 日志警告；未改写日志或清单。

## 待完成

用户本机确认后执行安装，检查实际成对 ID/擦写与 B 启动、TRIAL BCB、完整 B 回读摘要和完整 A 槽不变；收尾清除 fixture 并恢复生产 A、空白 B。真实电源中断和后续 trial 自检回滚仍须分别记录证据，不以主机模拟冒充。
