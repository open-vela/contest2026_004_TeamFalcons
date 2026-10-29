# OTA 控制协议与本地配置

实现基线：`74fc4ad`。本文件说明任务 09 的设备契约；真机安装与网络验收仍见[待办](issues/ota/acceptance-todo.md)。不能据此宣称 OTA 已交付。

## 连接及主题

OTA 使用独立 MQTT-C 会话，协议为 MQTT 3.1.1 over TLS，使用每设备用户名和密码。CA 与签名公钥由构建提供，TLS 校验服务器证书链、主机名和时间；缺少可信时间、CA、凭据或已验收熵源时拒绝连接。

只订阅 `vg/{device_id}/ota/offer`，QoS 1、非 retained。只向相同设备下的 `ota/status`、`ota/result`、`ota/confirm` 发布 QoS 1、非 retained 消息。服务端 ACL 应只允许该设备使用自己的主题。`ota/confirm` 是自检结果上报，不是入站命令，没有远程确认安装接口。

网络出口必须为 RJ45，物理链路通过 eth0 的 PHY 地址查询及 BMSR 双读判断，下载和控制 socket 均绑定 eth0。Wi-Fi 出口拒绝，错误为 `wifi_not_supported`。断网期间结果保存在本地，无法保证当时通过 MQTT 报出错误；RJ45 恢复后补报。现有普通 MQTT 遥测会话及其主题不迁移。

## Offer

JSON 对象仅允许以下九个字段，全部必填，拒绝重复字段、未知字段、内嵌 NUL、超过四层嵌套和超过 1536 字节的消息。

| 字段 | 要求 |
|---|---|
| schema | 数字 1 |
| device_id | 必须等于本机标识 |
| task_id | 1–39 字节，字母、数字、下划线、连字符、点或冒号 |
| version | MAJOR.MINOR.PATCH，最多 31 字节 |
| manifest_url / image_url | HTTPS URL，各不超过 383 字节；必须命中本机下载主机和端口白名单 |
| image_size | 整数，8 至 8 MiB 减 8192 字节 |
| sha256 | 64 位十六进制文本 |
| expires_at | Unix 秒整数，必须与签名 manifest 一致且未过期 |

Offer 不能授予安装权限。设备校验签名 manifest 与 Offer 的 task、版本、大小、摘要、有效期、设备和型号后流式下载，完整回读校验成功才进入 READY。仅用户在本机长按确认，且安全联锁全部通过，才写 pending BCB 并重启。

status 包含 schema、device_id、task_id、version、slot、error_code、state、downloaded_bytes、progress；result 包含基础字段及 result；confirm 另含 self_check。结果名为 `installed`、`declined`、`expired`、`failed`、`rolled_back`、`self_check_failed`。错误使用固定标识，不包含 URL、响应正文或凭据。

## 重连与持久化

结果保存在 `/data/velaguard/ota/event0.bin` 至 `event7.bin`，每条 256 字节，带 CRC，改名替换并保留上一份备份。最多八条；队列满或损坏时拒绝新任务。只在收到对应 PUBACK 后更新确认位；需要 self_check 时，result 与 confirm 都收到 PUBACK 才删除记录。发送后断线、未收到 PUBACK 或写盘失败可能导致重复上报，服务端须按设备、task_id、结果去重。15 秒未获 PUBACK 会断开并重连，记录继续保留。

`/mnt/emmc/VGOTA/SUPPRESS.BIN` 保存已终止任务，跨重启拒绝重复 Offer，不自动淘汰。上限 4096 条、180224 字节；达到上限或发现破损时拒绝新任务，须维护处置，不能直接删除来重新授权旧任务。重启前未确认的 READY/下载任务终止，不恢复为自动安装。

A 和已确认的 B 均可接收下一版本；B 替换只在重启后的片内 stub 执行，回滚始终指向出厂 A。主机的 B 身份测试不代表真实 B→B 安装已验收。

## 凭据导入

本机配置 JSON 只允许 broker、port、username、password、allowed_hosts 五个字段。broker 为小写 DNS/IPv4 主机名，port 为十进制端口字符串；allowed_hosts 为一至两个 HTTPS 主机及可选端口。用户名最多 63 字节、密码最多 127 字节，不允许控制字符。

先在仓库外生成配置文件，再用 `scripts/ota/pack_credentials.py --config <文件> --uid <24位十六进制UID> --output <文件>` 生成加密包；工具依赖 Python cryptography。然后用 `scripts/provision-ota.ps1 -BlobPath <文件> -ComPort COM3` 导入。脚本仅发送加密分块并核对固定应答，不输出字段。板端鉴别完整性和 UID 后原子替换 `/data/velaguard/provision/ota_secrets.v2`。配置与原 LLM provisioning 分开；正在运行的会话需重启后使用新配置。

加密包为 AES-256-GCM，含随机 12 字节 nonce、UID 绑定、认证头和 16 字节 tag。密钥从公开 UID 和域字符串派生，**这不提供抵抗物理读取的凭据保密能力，也不是硬件信任根**。此实现遵循 v1 排除物理改写威胁的范围；部署前仍须认可该边界，不得把设备绑定称为防提取加密。

生产 CA 与 P-256 公钥经 VG_OTA_CA_FILE、VG_OTA_PUBLIC_KEY_FILE、VG_OTA_SIGNING_KEY_ID 构建环境提供，三者须一起设置。真实密码、私钥及配置文件不得提交。默认构建不含部署信任与凭据，OTA 控制服务保持关闭。

## 发布检查

`bash scripts/build.sh` 在主线生产配置中校验实际 PHY/RNG 选项，并调用 `scripts/ota/check_release.py` 检查 A/B 镜像的 fixture 入口、测试标识及私钥标记。它不能证明任意未知凭据不存在，发布负责人还须检查真实部署材料。

`check_release.py` 的 `--require-deploy-ready` 另要求已配置部署信任和已验收熵源。默认空信任构建可以通过材料排除检查，但 `deploy_ready=0`，不能用于宣称生产 OTA 可用。
