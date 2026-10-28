# OTA 启动安装格式 v1

本文件定义任务 04 的本地二进制交接格式。MQTTS、HTTPS、应用验签和正式安装确认入口仍按后续任务实现。boot stub 不接受网络命令，应用运行时不擦写 QSPI。

## Manifest

`/mnt/emmc/VGOTA/MANIFEST.BIN` 固定 512 字节，整数小端。固定字符串包含 NUL 结束符，不允许空串或非 ASCII 可打印字符。签名为 ECDSA P-256 的固定 `r || s`，各 32 字节；应用后续对前 220 字节进行签名验证，boot stub v1 只验证 CRC 和绑定关系。

| 偏移 | 字节 | 内容 |
|---|---|---|
| 0 | 4 | magic，`VGMF` |
| 4 | 2 | 格式版本 1 |
| 6 | 2 | 固定长度 512 |
| 8 | 4 | slot，必须为 B=1 |
| 12 | 4 | image_size，8..8380416 |
| 16 | 32 | version |
| 48 | 40 | task_id |
| 88 | 32 | 镜像 SHA-256 |
| 120 | 32 | device_id，和板载 96 位 UID 派生值绑定 |
| 152 | 32 | model，`STM32H750B-DK` |
| 184 | 8 | issued_at，Unix 秒 |
| 192 | 8 | expires_at，必须大于 issued_at |
| 200 | 16 | key_id |
| 216 | 4 | flags，bit0 表示诊断 fixture，其余位必须为 0 |
| 220 | 64 | 签名 |
| 284 | 224 | 保留，必须为 0 |
| 508 | 4 | CRC32，覆盖前 508 字节 |

fixture 使用 bit0=1 和空签名；只有 `--ota-fixtures` 同时启用的应用与 stub 能使用它。生产 stub 拒绝 fixture 和空签名。非空签名检查不等于验签，应用验签尚未实现，不能据此宣称生产信任链已完成。时间窗的当前有效性由后续应用预检负责，stub 只校验时间字段的内部顺序。

BCB 的版本、task_id、slot、大小和 SHA-256 必须与 manifest 一致。pending 的 flags 必须恰为 `VG_OTA_BCB_LOCAL_CONFIRMED=1`，由本地确认路径写入；该标志本身不是密码学授权。当前生产固件没有写 pending 的业务入口。

## 安装与持久化顺序

两个 BCB 必须已分配为独立的 4096 字节文件，簇链可完整读取。stub 不分配文件、不修改 FAT 或目录，只更新已有 BCB 的首个 512 字节扇区，头部和 CRC 均在此扇区内。写后等待 eMMC 完成，启用缓存时发送 FLUSH_CACHE，再回读比较。

1. 选择最高有效 seq 的 pending，校验 manifest、绑定、镜像大小、向量表和完整 SHA-256。
2. 在另一份 BCB 写入 `FAILED(seq+1)`，同步并回读；失败时不擦写 QSPI。
3. 按实际 QSPI 映射对齐擦除 B 所需区间，流式编程，并对本次读取的镜像重新计算摘要。
4. 回读 B 的全部有效镜像字节，核对 SHA-256。
5. 在原 pending 文件写入 `TRIAL(seq+2, trial_count=1)`，同步并回读后才跳转 B。

擦写或回读中断时，较新 FAILED 防止再次选择旧 confirmed B 或自动重新安装；trial 提交撕裂时也保留该 FAILED。trial 的下一次启动计数、看门狗、自检和自动回滚属于任务 05。

## QSPI 映射

当前启动路径保持原有 DFM 交错模式。安装适配层要求成对 ID 为 `20 20 ba ba 20 20`，两个忙状态均完成且两个 WEL 均有效才发送擦写命令。逻辑擦除单位使用 8192 字节，编程按 256 字节对齐，尾块填充 0xFF；末尾保留 8192 字节余量。核心与硬件适配层都检查地址仅位于 B 的 `[0x90800000,0x91000000)`，禁止跨槽或写 A。

P0 曾按单器件口径记录 4 KiB/256 B，单次 scratch 成功不证明交错映射下的逻辑擦除步长。本票按 DFM 重新核对安装路径，实际板测证据另行记录；不根据该差异扩大 A/B 槽位或对外宣称新的总容量。

## 诊断暂存与确认

测试传输副本由 ST-Link 写到固定 `0x91000000` 窗口；`vgota fixture stage` 仅将其复制到 eMMC，生成带真实摘要的 fixture manifest 和两份 FAILED BCB。该窗口不属于 A/B，生产固件没有此复制入口。

`vgota fixture arm` 显示绑定的版本、task_id 和大小，等待 USER 键先释放、再长按至少 2 秒；超时不写 pending。按键确认后重新核对镜像摘要，再写带本地确认标志的 pending。串口和开发助手不能生成按键事件。该入口仅供开发验收，正式屏幕确认和安全联锁属于任务 07。
