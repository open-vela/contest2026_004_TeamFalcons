# P2 一期连接复用证据目录（2026-10-24）

任务：ASR/TTS 语音专用 keep-alive 连接槽 + 录音期预连接（vela_tls stream_exchange
重构 + vela_https_request_stream_voice + vela_https_voice_preconnect，Kconfig
AI_AGENT_VOICE_CONN_SLOT）。对照基线与 P1 证据见 `../p1-streaming/`。

## 代码状态：完成且主机全绿

- ai_agent `velaguard/voice-streaming`：f71ad43（槽位+重构+入口）+ 6693959
  （非 200 响应头诊断行、预连接释放顺序修正）。
- 本仓：vc_post 路由到 voice 入口（开关关闭时字节级等同原路径）、录音启动
  分支 spawn 预连接线程（24KB 栈、原子单飞、cancel 传播）、defconfig 开启。
- 主机测试：test_tls_voice_slot（keep 真值表、顺序复用、keep-alive/close 头、
  sink-early 排空发布、一次重试、预连接、普通入口隔离）+
  test_tls_stream_upload 回归（LLM 路径字节级不变）+ 全套件 0 失败。

## 板端验证：机制实证 + 服务端 400 阻断 + 环境恶化，未完成

### 已实证（p2-pilot.log，17:52）

- 预连接在录音期完成握手（1050ms），后续轮 ASR phases 全 NA（零握手复用）。
- 槽位保温：预连接命中已热槽位时 0ms 返回。
- keep-alive 头与槽位生命周期按设计运行（voice_conn stale→重连路径可见）。

### 阻断：服务端对本板请求全部 400（17:52 起，跨固件/跨重启/跨密钥）

- 症状：ASR/TTS/后台 LLM 全部 `X-MiFE-Upstream-Status: 400`（空体），LLM 形状
  偶见带体 400 "Unsupported model mimo-v2.6-flash"（forensic.log 响应头全文）。
- 已排除（证据都在 logs/）：
  - 请求形状：主机同形请求（固件精确字节、含 keep-alive、tools、大 system、
    真实录音文件）全部 200（本轮多次复测）。
  - 密钥：secrets=connect_info 一致；假密钥得 401 而非 400；18:31 重灌密钥
    后仍 400（p2-forensic2.log）。
  - 服务端窗口：主机与板端同时刻对照，主机始终 200。
  - 连接复用代码：回退 P1 已知良好提交（86ff02c+2129037）重烧后同样 400。
- 未定位：需字节级抓包（板端请求 vs 主机请求 diff）。今日工具链不具备
  （探针固定 8443、语音入口仅 443、ICS 侧无抓包手段），且环境不稳定（下条）。

### 环境恶化记录（均为已知未闭环问题的复发/加重）

- **-0x4380 ×5**：ttss 命令在播放器启动后 TLS 握手 RSA 校验失败
  （p2-forensic*.log）。与 bvo14 以来另一会话排查中的 SDRAM 堆破坏同症状。
- **P1 重建版确定性硬fault ×2**：`vgvoice asr` 在 fopen→nx_vopen→getumask
  处 NULL+0x29 解引用（p2-ab-p1b/p1c.log，PC 符号已解析）；故障任务 TCB
  栈信息为系统任务表中不存在的值。同一源码 17:06 构建运行全绿、18:43 重建
  即崩——构建布局敏感，指向底层内存破坏。终态镜像上未复现。
- 烧录未生效 ×2（SWD 复位+板端标记核验后恢复）；eMMC wedge 复发（tiers 会话，
  SWD 复位恢复）；主机断电一次。

## P4 对照（G3 收尾）

主机探针补报协商版本：服务端可谈 TLSv1.3 / TLS_AES_256_GCM_SHA384（本次
实测 83.5ms 握手）。板端 mbedTLS 3.4.0 的 MBEDTLS_SSL_PROTO_TLS1_3 在
apps 树 mbedtls_config.h:1607 被注释（无 Kconfig 开关），启用需公共树改动
+全库重编——固件侧 TLS 1.3 留作后续任务，入口条件已写入计划。

## 播放器启动二分（配合 SDRAM 写者排查，commit 1cab570）

写者触发边界已切出（logs/bisect-l1.log、bisect-l2.log，--tls-forensics 叠加构建，
每级 SWD 复位后干净堆起测，ttss 触发 ×2/级）：

| 级别 | 足迹 | 结果 |
|---|---|---|
| 1 | 环 calloc + 播放线程，**零音频 ioctl** | 握手全 OK（3/3，无 -0x4380） |
| 2 | +设备会话（open/RESERVE/GETCAPS/CONFIGURE/ALLOCBUFFER×4/REGISTERMQ），**无 ENQUEUE/无 START/无 DMA** | 首次 ttss 即 -0x4380（1/2），第二次握手 OK |
| 0 | ttss 在当前 400 环境下与 2 等价（请求失败，数据永不入队） | 同 2（历史 3/3） |

取证行（bisect-l2.log）：`FORENSIC seq=0 ret=-0x4380 ... rx=4373 rxAB=match
canary=ok`——接收字节与影子副本一致、TLS 原始缓冲金丝雀完好，被改的是堆上
解析后对象（公钥/bignum/签名缓冲），与取证会话 02-ttss-trigger.md 结论一致。

**结论：写者在设备会话建立路径（open→REGISTERMQ 之间），与 DMA 传输无关、
与纯堆分配无关。** 嫌疑排序：wm8994_configure（I2C 编解码器写 + SAI 设置，
若 CONFIGURE 武装了 DMA 通道即具备向堆写的能力）、wm8994_reserve（worker
mq 创建路径）、ALLOCBUFFER 的对齐分配器路径。非确定性（2 级 1/2）符合
"受害对象由布局决定"模型。晨间 critical 告警入口（无音频）可能是第二写者
或同源（待取证会话用哨兵定位）。

## 目录

- plans/：p2-pilot（复用实证）、forensic/forensic2（400 响应头取证+密钥实验）、
  ab-p1*（P1 已知良好回退对照+硬fault）、terminal-verify（终态镜像验证）。
- logs/：上述全部原始日志。
- handoff-tools/run_p2_plan.py：驱动器。

## 结论

P2 代码按设计完成、主机验证闭环；板端复用机制已实证，但完整板端验收被
服务端对本板的 400 拒绝与板端内存不稳定（-0x4380 旧案+布局敏感硬fault）
阻断。建议：① 由取证会话用 TLS_FORENSICS 镜像抓写者；② 板端 400 需字节级
抓包（Windows 侧 443 TLS 代理或网络镜像）后再判服务端/板端归属。
