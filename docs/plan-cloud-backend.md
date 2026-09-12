# 云端 / 后端计划（MQTT 后端、鉴权、Bridge、OTA）

> 结论先行：**9/20 前云端零新增代码。** 本文说明为什么，盘点现在真实存在什么，以及 9/20 之后按什么顺序做。
> 板端与提交计划见 [`plan-917-submission.md`](plan-917-submission.md)。

## 1. 为什么 9/20 前不做

1. 官方加分项「端云/端端协作」（`ai_hardware_track_guide.md` §三.2）定义是 **设备端 Agent 与电脑端 Agent 配合完成任务、体现任务拆分**，不是遥测上云。MQTT 多发几个主题碰不到这项分。
2. 评分权重里云端能触到的只有「完整度」「商业潜力」的边角；而它引入的成本是：必须团队自有且演示稳定的服务器（BOUNDARY C13）、视频多一个网络依赖、明文 MQTT 上镜会被问 TLS。
3. 现有固件已经有 `vg/{DEVID}/status`（retained + LWT），足以证明网络栈、RJ45→ESP-01 切换和 MQTT-C PAL hook 那个 PR 是真的。

9/20 前云端唯一可选动作：如果 ESP-01 已通且手边有 broker，用 MQTT Explorer 拍 10 秒 retained status 的 `network` 字段从 `rj45` 变 `esp01`。不通就不拍。

## 2. 现状盘点（2026-09-12 核对代码）

| 项 | 文档怎么写 | 代码里真实状态 |
|---|---|---|
| `vg/{id}/status` | 手册 §8.3、合同 §4.1 | **已实现**：`app/velaguard/vg_mqtt_session.c`，QoS0 retained，LWT 同主题；`vg_net_mgr` 开机自动建连（`CONFIG_VG_NET_FAILOVER=y`） |
| `telemetry` / `alarm` / `diagnosis` | 手册 §8.3、README | **不存在** |
| `ota/*` 七个主题 | 手册 §8.3、§8.4、ADR-0005 | **不存在**；boot stub 只做 QSPI XIP 跳转（nuttx PR #350），无 eMMC 暂存、签名、回滚 |
| TLS / HMAC token / ACL | 手册 §16.1–16.2 | **不存在**：明文，`CONFIG_VG_MQTT_BROKER_HOST` 编译期，`DEVID` 编译期宏 |
| `ai/request` / `ai/response` / `tts/*` | `docs/velaguard-mqtt-contract.md` v1 | **已从产品移除**（手册 v3.1 §8.3）；合同 v1 过期 |
| AI Bridge（Python） | `docs/backend-api.md` 描述得很细 | **仓库里没有这份代码**（restart 时未迁移，见 `docs/MIGRATED_FROM_RESTART.md`）；文档是历史快照 |
| LLM 链路 | ADR-0002 说经 Bridge | **实际直连 MiMo**（手册 §8.2 v1、BOUNDARY V4）：板载 ai_agent HTTPS，key 加密在 eMMC。ADR-0002 已被事实推翻但没有 superseding ADR |
| `events.jsonl` 结构化事件 | 手册 §16.7、README | **不存在**；工具调用审计只有 syslog |
| `pending` 队列重发 | 手册 §16.2 | **不存在** |

结论：云端能力目前 = 一个 retained 状态主题。其余全是规划。README 与手册的措辞收口见 `.trellis/tasks/09-09-judge-submit-pack/prd.md`。

## 3. 9/20 后路线（按价值/成本排序）

每一阶段都遵守 §2.2 分界线：本地安全环不依赖任何云端能力；云端挂了，板子照常。

### C0 文档对齐（半天，先做）

- [ ] `docs/velaguard-mqtt-contract.md` 改 v2：删 `ai/*`、`tts/*`、`config/candidate`；主题树对齐手册 §8.3；标明每个主题的实现状态
- [ ] `docs/backend-api.md` 顶部加「历史快照，代码不在本仓」，或移到 `docs/history/`
- [ ] 新 ADR-0006：LLM 直连 MiMo 取代 ADR-0002；Bridge 降为可选增强
- [ ] 手册 §16.7 `events.jsonl` 要么实现要么改成 syslog

### C1 遥测与告警上云（板端 2–3 天，云端 1 天）

目标：云端能看到设备当前值和告警，为商业叙事补「多设备集中监控」。

板端：
- [ ] `vg/{id}/telemetry` QoS0：复用 `/data/velaguard/live/values.txt` 快照，周期（默认 30 s）发一条 JSON，`{id,value,ok,age_ms}` 数组
- [ ] `vg/{id}/alarm` QoS1：`vg_model_set_live` 告警触发/恢复处发 `{ts,id,kind,value,thr,state:raised|cleared}`
- [ ] Pending 队列：断网期间告警写 `/data/velaguard/pending/alarm-*.json`，上线后重发（手册 §16.2「关键事件依赖本地 pending 队列」）
- [ ] 不发 `diagnosis`：Agent 输出留在板上，云端不做二次消费
- [ ] 采集线程不因 MQTT 阻塞：发布走 `vg_net_mgr` 线程，队列满则丢 telemetry、保 alarm

云端：
- [ ] Mosquitto（docker compose）+ 一个订阅落库脚本（SQLite 即可）+ 最简看板（Grafana 或静态页）
- [ ] 验收：拔网线 5 分钟内产生 2 条告警，恢复后云端按序收到，无丢无重

### C2 鉴权与 TLS（板端 2 天，云端 1 天）

前提：C1 已跑通。

- [ ] `device_id` 从 STM32 UID 派生（手册 §16.1），`DEVID` 宏仅 test 构建
- [ ] HMAC token：`base64url(HMAC-SHA256(PRODUCT_AUTH_SECRET, "velaguard:mqtt:v1:" + device_id))`；产品密钥不进固件，token 由 `vgprovision` 加密落 eMMC（复用现有 LLM key 通道）
- [ ] MQTTS：MQTT-C PAL hook 接 mbedTLS；CA 固化；`VG_BUILD_MODE=production` 时拒绝明文
- [ ] Broker ACL：设备只能读写 `vg/{自己}/#`；denylist 按 `device_id`
- [ ] 内存预算：TLS 会话在 HMI 固件上是否放得下，先量再做（当前 SRAM 92.5%，可能要等 C1 后评估是否把 MQTT 会话放 SDRAM）

### C3 Bridge / 端云协作（可选，云端 2 天，板端 0.5 天）

只有想拿「端云协作」加分或需要 key 不落板时才做。

- [ ] Bridge 暴露 OpenAI 兼容 `/v1/chat/completions` 透传；板端 `llm_proxy` 只改 base URL
- [ ] 真正的「任务拆分」叙事：PC 端 Agent（Claude + `.agents/skills/mthings-automation-config-skill`）从器件手册生成点表 JSON → 上位机下发 → 板端确定性试读 + 人确认 → 板端 Agent 解释。先核实该 skill 能否直接产出 VelaGuard 点表格式；不能则补一个转换脚本
- [ ] 写进 README 技术实现：端侧做什么、云侧做什么、如何通信
- [ ] 不做：手册解析、TTS/ASR、云端诊断请求响应（已从产品移除）

### C4 OTA（最大，板端 5–8 天，云端 2 天；排最后）

依据 ADR-0005、手册 §8.4、§16.9。这是独立 bootloader 工程，硬依赖 eMMC 与 C2（签名验证需要可信根）。

云端：
- [ ] OTA 服务：发布 `ota/offer`（版本、大小、sha256、签名、chunk 大小）；响应 `ota/chunk/request` → `ota/chunk/data`（4 KB 或 8 KB，限 inflight）；收 `progress`/`result`/`confirm`
- [ ] 签名：开发签名与生产签名分开；私钥只在 CI/签名机

板端：
- [ ] 应用态：收 offer → 屏上 Local Confirmation → 分片拉取写 eMMC staging（`CONTEXT.md` Staging Image）→ sha256 + 签名校验 → 置升级标志 → 重启
- [ ] 禁升条件：高等级活动告警、存储异常、供电不稳（§16.9）
- [ ] boot stub 扩展（片内 Flash，nuttx 树 PR）：读 eMMC staging → 再校验 → 擦写 QSPI → 跳转；失败回滚到 eMMC 保留的旧镜像
- [ ] 新固件自检 → `ota/confirm`；未 confirm 下次启动回滚（复用 `frameworks/system/ota/` bootctl/verify）
- [ ] 全过程结构化事件（依赖 C0 决定 `events.jsonl` 还是 syslog）
- [ ] 不挤占采集/告警/UI：分片拉取在网络线程，限速

验收：一次完整升级 + 一次人为损坏镜像的回滚，采集与告警全程不中断。

### C5 看板与多设备（可选）

C1 的看板加多设备列表、告警历史查询。纯云端，不影响板子。

## 4. 各阶段边界提醒

- 所有云端组件由团队自有、演示稳定（C13）。
- MiMo token 仅限比赛用途（C10）；Bridge 化后 key 只在 Bridge 环境变量。
- 任何云端功能不得让 Agent 或云端获得写总线、改配置、清告警的能力（V5）。
- 板端产品代码留在 `contest2026_004_TeamFalcons/`；云端代码另建目录（如 `cloud/`）或另仓，Apache 2.0。
- 公共树改动（TLS PAL、boot stub 扩展）走直改 + PR，不用 patch（C2）。
