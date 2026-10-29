# T09 控制链路、设备凭据与结果补报证据

日期：2026-10-29。实现提交 `74fc4ad`，后续板测配置及链路诊断修正 `f3ddaa6`。NuttX RNG 提交 `6edcdecfcec`、PHY ioctl 提交 `e7ea456993e`；MQTT-C 头文件提交 `4d23fd0`。均为本地提交，未推送或创建 PR。

## 主机验证

在选手仓根目录执行：

```sh
make -C app/velaguard/host_tests test
make -C app/velaguard/host_tests test-ota-network
python3 app/velaguard/host_tests/test_ota_service.py
```

结果均为退出码 0。网络测试使用真实 mbedTLS、MQTT-C、cJSON、HTTPS 下载器、P-256 验签和真实 POSIX 暂存；测试服务器在主机环回地址运行，临时 CA、签名私钥、测试凭据只留在忽略的 `.debug`。服务测试的网卡选择由主机端口注入为 RJ45，不能代替硬件出口验收。

| 场景 | 实际结果 |
|---|---|
| MQTTS 有效凭据、精确 Offer 主题与 QoS 1 | 订阅成功，签名 manifest + HTTPS 镜像到 READY |
| 错误密码、错误主机名、拒绝 SUBACK、明文对端 | 拒绝，不视为有效控制会话 |
| retained、错误主题、未知或重复字段、Wi-Fi 出口、远程确认字段 | 协议门禁拒绝 |
| READY 等待 | 两份 pending BCB 均不存在，没有安装授权 |
| 主机取消及进程重启后重复任务 | 持久 declined 上报；重复 Offer 不触发第二次 HTTPS 请求 |
| 合成 confirmed BCB 的 B 身份进程 | installed + self_check passed 的 result/confirm 均收到 PUBACK 后清队列 |
| 对端收到结果后断线、故意不回 PUBACK | 同一服务进程重连，未确认结果保留并重发，队列最终清空 |
| Agent run_shell | 实际 C allowlist 拒绝 vgota 及 shell 包装命令 |
| 配置封装 | 真实 AES-GCM 验证、错误 UID/头/nonce/密文/tag 拒绝；Windows Python 打包与 C 解包互通 |

合成 BCB、主机取消和主机网卡端口只用于该测试进程，不向真实板子发送确认，不代表真实 B 启动、自检或 B→B 更新已验收。PUBACK 丢失导致至少两次相同结果上报，服务端须去重。

本次还验证 PHY 端口的 BMSR 第二次读取、陈旧 link、总线无效值、地址范围与三次忙重试；硬件依据见[PHY 调查](t09-rj45-link-research.md)。

## 真机可自动完成的范围

COM3 原先被其他会话占用，本次后段已释放。先读到旧 A 槽版本及 IDLE，随后执行 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/flash.ps1`，只通过 ST-Link 烧写本次 A 镜像及片内 stub，没有烧 B，也没有通过 OTA 安装。

首份生产配置未启用 VERIFIED_ENTROPY，`vgota entropy` 连续三次各读取 8192 字节，重复块检查及真实 mbedTLS seed 均通过，耗时 0–10 ms（系统 tick 分辨率内）。源码和芯片资料核对见[熵源调查](../2026-10-28/t08-trust-research.md)。这证明本机硬件通路和 seed 可工作，不是随机数质量认证。

据此在 H750 主线 defconfig 开启 VERIFIED_ENTROPY，重新构建并烧录。命令为 `vgota identity`、`bootinfo`、`entropy`、`link`、`provision status`、`control status`、`state`；[最终串口摘录](t09-board-verified.txt)显示：

```text
identity version=0.1.0 slot=A build=f3ddaa60f6d0-dirty
bootinfo reason=a-no-bcb valid0=0 valid1=0
entropy bytes=8192 mbedtls_seed=pass health=pass
link current=1 phy=1 bmsr_first=782d bmsr_second=782d err=0
provision configured=0 trust=0 entropy=1
control running=0 online=0 active=0 downloading=0 error=credentials_unconfigured
state=IDLE
```

首轮启动发现磁盘上仍有旧 T07-INSTALL-02 的 READY 状态，将其终止为 interrupted_before_confirmation，未创建 pending，见[首轮摘录](t09-board-offline.txt)。最终重启仍为 A、无 BCB；不能把 ST-Link 写入 A 计作 OTA 成功。

用户较早告知未连以太网；本轮探针实际读到 PHY link up，普通网络状态也报告 RJ45 有 IP。两者只是当时快照，尚未进行拔线对照、板端 HTTPS/MQTTS 或互联网连通验收。部署 CA、设备凭据均未配置，控制服务按设计保持停止。

## 未验收

真实板端 TLS/签名下载、生产凭据导入、真实用户触屏安装、B 自检结果补报、B→B 安装、并发采集/告警/AI 的资源占用仍留在[待办](../../issues/ota/acceptance-todo.md)。语音浮窗布局和关闭缺陷继续作为独立待修复项。
