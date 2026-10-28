# OTA暂存使用现有FAT并增加本地安装确认

状态：已确认设计，待实现和验收。

日期：2026-10-28。补充 [ADR 0006](0006-mqtts-control-https-download-ota.md)，不改变 MQTTS 控制与 HTTPS 下载的传输决策。

## 背景与决策

ADR 0006 确定 MQTTS 控制、HTTPS 下载和片内 boot stub 烧写 QSPI，未确定暂存介质、槽位和安装确认。初版方案在 eMMC 尾部划原始区并要求重新格式化 FAT；该方案会清除 AI 凭据、点表和运行配置，且依赖固定容量假设，用户确认不采用。

OTA 使用现有 eMMC FAT，不重新格式化。应用在 `/mnt/emmc/VGOTA/` 下创建 8.3 专用目录，文件名固定为 `IMAGE.BIN`、`IMAGE.TMP`、`MANIFEST.BIN`、`BCB0.BIN`、`BCB1.BIN`、`STATE.BIN`。boot stub 增加只读 FAT16/32 解析，按目录项和簇链读取镜像；不支持的 FAT、损坏的簇链和超出槽位大小的文件一律回 A 槽。

eMMC 容量以板上 CID/CSD/EXT_CSD 和 `df` 实测为准，不作为 OTA 布局的固定参数。B-01/Revision B 的公开型号资料为 4GB，手册原 8GB 记录待实测更正。

## 槽位与启动控制

- QSPI A 槽 `0x90000000`，8MB，出厂和恢复镜像，只由 ST-Link 写入；B 槽 `0x90800000`，8MB，只由 OTA 写入。槽位偏移在 P0 按 QSPI JEDEC ID 和实际容量复核。
- `BCB0.BIN` 和 `BCB1.BIN` 各 4KB，包含 magic、seq、状态、槽位、版本、task_id、镜像大小、SHA-256 和 CRC。应用通过临时文件加替换更新 BCB；boot stub 读取两份并选择最高有效 seq，trial 和 failed 记录在原文件内就地写入。
- 不把 BCB 放在片内 Flash。H750 的 128KB 片内 Flash 是单个 128KB 扇区，无法单独擦除；部分字编程还可能触发 ECC 问题。
- 应用校验通过后才写 pending；boot stub 再校验 `MANIFEST.BIN` 的 CRC、`IMAGE.BIN` 的大小和 SHA-256，以及 BCB 的绑定关系。烧写后回读 SHA-256。新固件自检通过后写 confirmed；失败或 trial 次数超限回 A。

## 安装确认

下载、签名校验和 SHA-256 可以在后台自动完成。安装前必须由用户在本机明确确认，每个 task_id/version 单独确认：

- 校验完成后进入 READY_AWAITING_CONFIRM，屏幕显示版本、大小、摘要前缀和重启提示；用户长按安装或点击取消。
- 确认事件只来自板载触摸或按键。MQTT、手机、语音识别结果和设备端 Agent 都不能确认安装，也不提供远程安装授权命令。
- 确认后再次检查无高等级活动告警、存储可写、摘要仍匹配、设备空闲，然后写 BCB pending 并重启。
- 取消或超时丢弃暂存并上报 declined/expired；同一 task_id 不再重复弹窗，服务器需要新 task_id 才能再次提议。
- 新固件启动后的自检确认由固件自动完成，不构成第二次人工确认。设备通过 `ota/confirm` 上报自检结果，该主题只表示固件自检状态，不能作为远程安装授权。

## 安全边界

- 应用使用 ECDSA P-256 验证云端 manifest，manifest 绑定 device_id、型号、槽位、版本、大小、镜像摘要、task_id 和时间。HTTPS 必须验证服务器证书，MQTTS 必须验证服务器证书并使用每设备凭据。
- v1 的 boot stub 不实现签名库，只验证 BCB 绑定的 SHA-256。信任边界是应用完成签名校验后才写 pending；物理改写 eMMC 的威胁不在 v1 范围。
- OTA 控制仅使用 RJ45 出口，ESP-01S Wi-Fi 出口拒绝 OTA。现有明文遥测 MQTT 不改；生产构建要求 MQTTS。
- 完整 URL、MQTT token、API key 和签名私钥不得写入日志、串口或 MQTT payload。

## 实施门槛

1. P0：读 eMMC CID/CSD/EXT_CSD 和 `df`，确认 FAT 类型；在主机和板上验证 stub 的只读 FAT 解析；在 QSPI scratch 区验证擦写；复核 QSPI 实际容量和擦除粒度。
2. P1：boot stub 的 eMMC 1-bit 驱动、FAT 解析、BCB、槽位选择、trial/rollback 和 B 镜像构建。
3. P2：`/mnt/emmc/VGOTA/` 文件存储，镜像、清单和 BCB 的写入及断电测试；不格式化 eMMC，不迁移 AI/MQTT 凭据。
4. P3：HTTPS 下载、manifest 验签、SHA-256、READY 状态和本地确认。
5. P4：MQTTS 控制、协议状态、凭据 provisioning 和结果补报。
6. P5：自检确认、回滚、故障注入、长测和证据。

## 被否决的方案

- eMMC 尾部原始区加 FAT 缩小：需要重新格式化，清除 AI 凭据、点表和运行配置，并依赖固定容量假设。
- 片内 Flash BCB：H750 的 128KB 为单个扇区，无法独立擦除。
- 应用运行期切 QSPI 并烧写：破坏 XIP 取指，风险高于 boot stub 方案。
- 自动安装：用户要求每个新版本安装前明确确认。
