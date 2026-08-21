# Implement: 阶段 1 最小网络闭环

## 有序清单

1. 备份：`cp nuttx/.config nuttx/.config.bak.stage1-20260814`。
2. 生成 `velaguard-net` defconfig：
   - `cd $OPENVELA_ROOT/nuttx && tools/configure.sh -e stm32h750b-dk:velaguard-min`
   - kconfig-tweak 开启网络符号集（design.md 清单），保持其余 velaguard-min 符号。
   - `make olddefconfig`；grep 断言 keep 清单在、exclude 清单（LVGL/LTDC/INPUT/URANDOM）无。
   - `make savedefconfig` → 拷贝到 `boards/arm/stm32h7/stm32h750b-dk/configs/velaguard-net/defconfig`。
3. 固化 patch：`git add -N <defconfig> && git diff -- <path>`
   → `scripts/openvela-velaguard-net-defconfig.patch`；
   写 `scripts/apply-openvela-velaguard-net-defconfig-patch.sh`（幂等，仿 velaguard-min 脚本）。
4. 幂等验证：连续执行 apply 脚本两次，第二次输出 already applied；`git status` 干净。
5. 全量构建（PATH 含 `prebuilts/gcc/linux-x86_64/arm-none-eabi/bin`），修编译问题。
   [DONE] 2026-08-14 容器内通过：Flash 197148 B / SRAM 51012 B。
6. 用户现场 M1 验收：PC 共享 WiFi（桥接优先）→ 烧录 → NSH `ifconfig` 看 IP →
   `ping <网关>` → `ping 8.8.8.8`；失败时交叉验证分锅。
7. M2 实现 `vgmqtt`：
   - `app/velaguard/vgmqtt.c`（MQTT-C：init/connect/LWT/publish/sync，`DEVID` 覆盖 device_id）。
   - Makefile/CMakeLists.txt/Kconfig 挂 `VG_BRINGUP_TOOLS`；`PROGNAME`/`MAINSRC` 配对。
8. 编译验证 `vgmqtt`（容器内 make，确认新命令进入固件）。
   [DONE] 2026-08-14 注册 vgmqtt，链接通过；补开 `NETDB_DNSCLIENT`+`NETINIT_DNS` 修复
   getaddrinfo 未定义链接错误。
9. 用户现场 M2 验收：
   - PC 端 `mosquitto_pub/sub` 预检云 Broker（地址/端口/账号执行时确认）。
   - 板端 `vgmqtt -h <broker> ...` → 云侧订阅端看 retained `status`；
     强制断线 → 云侧看 LWT `online:false`。
10. 文档：更新 `docs/velaguard-bringup-known-issues.md`（velaguard-net 预设、切换命令、
    ps1 警告、M1/M2 验收方法）；journal 记录。
11. 收尾：trellis-check 验证 → 提交前与用户确认。

## 终验结果（2026-08-14）

- AC1~AC5 全部通过；known-issues 已补阶段 1 章节（§5，含预设/脚本/vgmqtt/验收/踩坑）。
- 剩余：trellis-check 质量验证 → 用户确认后提交（任务收尾）。

## 用户现场反馈修复（2026-08-14）

- 根因：`build_minimal.sh` 硬编码 velaguard-min，`expect_dev_config` 还显式拒绝
  `CONFIG_NET`，导致跑 Build & Flash 时 distclean 回 minimal；构建失败后 `.debug/`
  里的旧 minimal `nuttx.hex` 被 Flash 任务照常烧录。
- 修复：`build_minimal.sh [TARGET] [--clean]`，TARGET ∈ net|min|lvgl（默认 net，
  兼容旧 `--clean` 用法）；按目标校验配置形态；构建前清空 `.debug` 旧产物，
  失败时 Flash 因缺文件报错而非烧旧固件；`.vscode/tasks.json` 增加 `vg_target`
  选择器（Build/Rebuild All 弹窗选择），Flash 固定烧 `.debug/nuttx.hex`。
- 验证：`net` 全量构建通过（Flash 197276 B），二次运行不重复配置；未知目标 exit 1。

## M1 现场验收结果（2026-08-14）

- AC3 通过：DHCP 拿到 `192.168.137.28`（ICS 共享形态，网关 192.168.137.1）；
  ping 网关 10/10 0% 丢包（RTT 20ms）；ping 1.1.1.1 连通（6/10，上游 Mihomo/WiFi
  抖动导致 40% 丢包，网关 0% 丢包证明板端收发干净）。
- 拓扑历程：桥接两成员未同时入桥 → 板子无 DHCP；切 ICS/移动热点后 DHCP 正常。
  记录教训：Windows 网桥必须同时包含 WiFi + USB 以太网，否则成为无上游孤桥。
- M2 联调参数（用户提供）：broker=107.174.123.74:1883（明文、匿名、QoS1、
  MQTT 3.1.1）；AI Bridge 合同 `backend-api.md` 已读（`vg/{device_id}/ai/request`
  请求/响应主题，阶段 3 用）。DEVID 用默认 `vg-test-01`。

## M2 首轮实测排查（2026-08-14）

- 现象：板端打印 connected（16:06:55）→ published（16:07:26，**相隔 31.5s**）→
  保持 30s 退出；云侧观察端未收到 status（观察端窗口先于测试过期，且发布疑似未达）。
- 根因（代码级）：`mqtt_pal_recvall` 在 NuttX 上是**阻塞 recv**；mqtt_connect 返回 OK
  只代表 CONNECT 排队，未确认 CONNACK；QoS1 报文在 PUBACK 前留在发送队列。
  原 vgmqtt 未等 CONNACK/PUBACK，sync 卡在 recv 里约 31.5s，且打印"published"是假确认。
- 修复：socket 设 SO_RCVTIMEO/SO_SNDTIMEO=2s；等 `event_connect`（CONNACK，≤5s）才打印
  connected；`mqtt_mq_length==0`（PUBACK 已收，≤5s）才打印 published；失败输出
  `mqtt_error_str` 并退出 ERROR。
- 其他嫌疑：Mihomo TUN 代理可能干扰 MQTT 长连接与 ICMP（对应 1.1.1.1 的 40% 丢包），
  复测前要求退出。

## M2 第二轮实测（2026-08-14，AC4 核心已演示）

- 云侧证据（用户工具 + 容器观察端双确认）：retained `status`（QoS1, `online:true`）
  收到；断线后 LWT `{"online":false}` 收到（两轮均成立）→ AC4 核心闭环通过。
- 遗留问题：PUBACK 未回到板端（不对称路径）→ vgmqtt 的确认循环空转 33s 后
  报 MQTT_ERROR_SOCKET_ERROR；嫌疑为 Mihomo/ICS 上行下行不对称。
- 修复：① QoS 显示 bug（存数字 0/1/2，发布时 `qos<<1` 编码，原打印显示编码值 2）；
  ② 确认等待改用 CLOCK_MONOTONIC 真实墙钟计时（CONNACK/PUBACK 各 ≤10s），
  杜绝阻塞式 mqtt_sync 稀释超时导致的长尾空转。
- 待办：确认 Mihomo 已退出 → 干净复测一轮（期望 connected → published → holding
  → 拔线 → LWT）。

## M2 第三轮（2026-08-14）：合同对齐 QoS0

- 证据：CONNACK 回程正常（7s），但 QoS1 的 PUBACK 10s 未回（连接未断，无 socket
  error）；同时观察端确认 status（`online:true`）实际已到达 Broker、LWT 已触发 →
  "板→云"发布路径通，卡点仅在 QoS1 确认回程（ICS/上游入站抖动嫌疑）。
- 合同核对：status 主题按规定应为 **QoS0 + retained**（`docs/velaguard-mqtt-contract.md`
  §3、推进方案 §16.3），原默认 QoS1 属设计偏离，已改正。
- 变更：默认 `qos=0`、LWT `WILL_QOS_0`（与 status 主题一致）；`-q 1/2` 保留供调试。
- 记录为阶段 3 风险：QoS1 流（alarm/ai/request）的 PUBACK 回程需在云链路稳定后复查。

## 验证命令

```bash
export PATH="$OPENVELA_ROOT/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:$PATH"
cd $OPENVELA_ROOT/nuttx
tools/configure.sh -e stm32h750b-dk:velaguard-net && make olddefconfig && make -j$(nproc)
# 现场 M1
ifconfig eth0            # 期待 DHCP IP
ping 192.168.x.1         # 网关
ping 8.8.8.8             # 公网
# 现场 M2（云侧另开 mosquitto_sub -t 'vg/+/status' 观察）
vgmqtt -h <broker_ip> -p 1883 -t vg/vg-test-01/status -m '{"online":true,...}' --retain
```

## 风险文件 / 回滚点

- `nuttx/.config`（备份步骤 1）。
- `boards/arm/stm32h7/stm32h750b-dk/configs/velaguard-net/defconfig`（patch 可 reverse）。
- `app/velaguard/Makefile` / `CMakeLists.txt` / `Kconfig`（挂载条件，勿破坏现有 vg* 工具）。
- `scripts/apply-openvela-velaguard-net-defconfig-patch.sh`（幂等性必须验证）。

## task.py start 前复查

- [ ] prd/design/implement 三件套齐备，用户已批准最终规划摘要。
- [ ] implement.jsonl / check.jsonl 已替换种子行。
- [x] 当前 `.config` 已备份（`.config.bak.stage1-20260814`）。
