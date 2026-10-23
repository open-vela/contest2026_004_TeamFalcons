# 给 Claude 的接续提示词

你接续 VelaGuard 的 BVO-07 至 BVO-14 语音任务。用户要求无人值守推进到 BVO-14 完成；本次明确要求把当前进展和剩余工作交给你。工作目录是 `/home/hello19y/openvela/contest2026_004_TeamFalcons`，WSL Debian，2026-10-23，Asia/Shanghai。交接核对时间约 21:10。先读仓库 AGENTS.md、本文和本目录 README.md，再直接推进，不重复询问已经明确的产品取舍。

## 授权与约束

- 用户已明确选择恢复原方案：语音必须经过板载 ai_agent 的模型轮次、ReAct 和注册的只读工具；异常播报保留总数和完整前三项，继续优化原 30 秒首次可播放预算。不得通过跳过模型、删减前三项或延长预算来达到成功率。
- 固定测试集目标至少 27/30；失败原样保留，不挑选重试覆盖原矩阵。SAPI 合成样本不能当作真人语音证据；首次可播放不能写成播放完毕。
- 每次任务完成本地提交，不推送。公共依赖在实际 checkout 修改，不建补丁，不改 vendor，不擅自新建其他任务。不要进入 QA-04 总线冻结根因或启动 24 小时长测。
- 不应用点表、不代答人工确认、不写寄存器、不清告警、不控制数字量输出。测试从站只响应 FC03/04，板端保持唯一主站。
- 保留用户及其他任务的改动。AI Coding JSONL 不改正文、不删除片段、不伪造；整份会话是否删除由本人决定。历史凭据及历史日志未重写。
- connect_info 可私下用于测试，不提交，不输出密钥。Kconfig 原有默认连接参数本轮已清空。MiMo 从 eMMC 加密 provision 读取；本次 MQTT 未配置，不能声称 MQTT 在线。
- 自然中文，避免 U+201C/U+201D 中文双引号。真人按键、听音、触摸、真实拔电和服务方保留政策不能凭推测记为通过。

## 当前代码与仓库

竞赛仓分支 `velaguard/maintenance-stability`，交接前基线 HEAD 是 Gemini 的 `fb7210e`；交接时会提交本轮源码、证据和本文，请以 `git log -1` 为准。以下竞赛仓改动均由本轮产生。

ai_agent 实际仓库 `/home/hello19y/openvela/packages/ai_agent`，分支 `velaguard/rsp01-response-timing`：

- `d9da371`：请求生命周期观察、受控后台请求、固定无效凭据流式探针、16 KB 有界上传合并、默认关闭实验性 TLS 会话缓存。
- `76920a3`：仅语音模型请求临时使用 RR 100，cleanup 恢复原策略，含调度保护单测。全量主机测试和固件构建通过，已烧录，真机验证尚未开始。
- 原 Gemini 基线为 `b66573a`。两个新提交均未推送。

NuttX 分支 `velaguard/ptt-user1-stm32h750b-dk`，HEAD `cdfd23cdf0b`。本地板级 defconfig 已安装预设，仍 dirty，不提交；未跟踪 `arch/arm/src/armv7-m/vg_diag.h` 是其他 QA-04 工作，保留。apps HEAD `0ea954b79740`，未跟踪 nist-sts 文件是其他工作，保留。

### 已实现并验证的改动

1. Gemini 的跨线程 capture_any 有串取后台事实风险。现在绑定 SDK 客户端、语音命名空间、请求序号、真实执行线程、配置代际，仅接受请求的首次发布；不匹配时经只读工具注册表补取。测试覆盖旧请求、后台/其他线程、旧配置、私有捕获生命周期。
2. 语音 ASR 前暂停后台告警解释或日报，保留原请求，播报结束后恢复；旧回调按序号拒收。重试 TTS 同样取得前台优先权。主机验证通过，尚缺带真实暂停/恢复标记的真机成功记录。
3. 确认表换代使运行态点位及事件历史失效，不制造 CLEAR；采样必须携带当前代际，HMI 数组也带代际。删除后同名重加不能继承旧告警或恢复时长。保守策略是任一确认表变更后全部运行态历史重新积累。真实模块主机及 ASan/UBSan 通过，未在板端自动 apply。
4. 受控 HTTPS 把短 WAV/base64 源片段合并为最大 16 KB TLS 写入；普通请求保持 4 KB。缓冲在堆上有界，释放前清零，保留共享截止/取消。单测及 ASan/UBSan 通过，实测 ASR 从约 8.55 秒降到约 4 秒。
5. 诊断整轮故障入口受 `CONFIG_VG_VOICE_TEST_FIXTURE` 保护，默认关闭。固定局域网端点/错误域名、专门无效测试凭据，不把真实密钥发给夹具。故障一次性消费，重试 TTS 回到真实服务。坏 SSE 现分类为格式错误，取消/超时优先。
6. 新增 cancel_settled 元数据，等 UI 空闲、音频空闲、原始 PCM 清零、真实语音 SDK 请求退出后才记录回收时间。
7. 新候选的 `src/infra/voice_sched.h` 临时将模型线程从原 FIFO 60 调为 RR 100，与 HMI 文件线程公平轮转，结束恢复。尚待真机验证收益及恢复行为。不要把主机通过写成真机通过。

## 真机、进程和当前固件

- Windows COM3，115200，ST-LINK NSH。一次只运行一个串口计划；烧录前确认计划已退出。交接时没有本轮串口计划或构建进程在运行，COM3 可交给你。
- COM7 被已有项目从站占用，Windows PID 34188，包装进程 35936，交接时仍存在。复用它，不把 COM7 再开成主站，不随意杀此既有进程。
- 从站控制文件 `.debug/maintenance-slave-mode.txt`，交接时 `normal`。日志 `.debug/maintenance-slave.log`，本轮起始字节偏移 255177（也见 fixture-reuse.json）。每组场景结束恢复 normal。
- 模式：normal、warn、critical、offline、offline1、offline2、offline12、critical_offline2。14 点在线，正常 UPS 负载 40%。offline12 的慢轮询会使其他点超过 3 秒新鲜度限制，出现 3 个离线和额外过期点是有效事实，不要伪造成恰好 3 个异常。
- 实际网络接口是 eth0，当前交付候选上次地址 192.168.137.139，网关 192.168.137.1；不是 ESP8266 承载此次业务。重新启动可能换 DHCP 地址。
- 当前板上是 `firmware-foreground.json` 对应镜像，`flash-foreground.log` 已成功复位启动。HEX SHA256：`8e757b66a6402c5aaf1385553cf4db3bd30aab164fbeb9895702d06dd90ccfee`。
- 当前配置：DIRECT_TOOL=n、VOICE_TEST_FIXTURE=n、VOICE_TLS_CACHE=n、IOB_NBUFFERS=128、MBEDTLS_SSL_OUT_CONTENT_LEN=16384、NET_SOLINGER=y。
- `.debug/nuttx.hex` 是上述新调度镜像。`firmware-final-delivery.json` / `closeout-final-delivery.hex` 是前一轮历史候选，不能拿来冒充当前镜像。
- `/data/velaguard/voice/` 仍有固定 30 个合成输入 WAV，以及原有 q1.wav、pref.txt。不要删除原有 q1.wav 或偏好。诊断服务都已停止：两个故障服务分别正常退出 28 和 33 个请求，没有本轮 HTTP/TLS 服务占端口。

## 已取得的实测结果

### 完整固定矩阵：26/30，尚未达标

`matrix-iob128-baseline/` 是上一候选完整且不可覆盖的 30 条结果：

| 类别 | 完整播报且首次可播放≤30秒 | 文字正确 | 成功首播中位/最大 |
|---|---|---|---|
| 异常 | 10/10 | 10/10 | 16.425 / 28.020 秒 |
| 点位 | 9/10 | 10/10 | 25.020 / 26.650 秒 |
| 恢复 | 7/10 | 9/10 | 25.160 / 29.260 秒 |
| 合计 | 26/30 | 29/30 | 9 条播放结束超过 30 秒 |

失败：point06 的 TTS 证书解析 `-0x3ae2 PK/ASN1`；recovery01、02 的模型阶段较长后 TTS 超时；recovery07 被 ASR 识别成环保后按规则拒答，没有静默选错点。当前新调度镜像尚未开始第二份完整矩阵。

Gemini 的旧 `review/matrix-session/` 可复算 23/30、文字 28/30，16 条播放结束超 30 秒；报告中的全体文字 100% 正确、消除 DNS 丢包、ESP8266 吞吐归因均证据不足，review/README 已更正。更早 29/30 用了跳模型和缩摘要的旧合同，不可作为现方案结果。

### 整轮故障：12 类各三次有完整通过证据

`round-fault-summary.json`：42 次保留尝试，36 次通过，coverage_complete=true。额外六次是夹具重启打断一例、串口元数据不完整两例、修复前坏 SSE 分类错误三例，原记录都保留。

ASR：401、429、空文本、坏 JSON、HTTP 截断、错误域名、无响应。TTS：401、429、HTTP 截断、坏 SSE、无响应。ASR 失败明确提示；TTS 失败保留事实文字；结束后 retained_pcm=0 且请求退出。无响应提示在约 30.5–30.7 秒，文件/状态收尾仍略超网络预算，不可声称所有错误严格≤30秒。

- tts-retry.log：TTS 429 后仅重试播报成功，文字相同；ASR 7→7，TTS 7→8。
- record-limit-local.log：合成按住超过 15 秒自动提交，实录约 14.93 秒，取消回收 130 ms，原始 PCM 为零。该录音仅送本地丢弃请求体的夹具，未上传环境录音到云端。
- fault-recovery-real-service.log：关闭故障模式后同次开机访问真实 MiMo，ASR→模型工具→TTS→播放成功，首播 21.92 秒，结束 26.13 秒。
- 故障记录跨几个明确来源的诊断镜像，映射见 fault-trial-provenance.json；不是当前普通固件的整套重新测试。

### 其他证据

- scene-retry.log：阈值与离线重叠保持原阈值实例；仅成功正常采样才能恢复；事件环挤出后 history_complete=false，之后 recovered_partial，不伪造持续时长。旧镜像的非空异常语音试跑失败，不算成功播报。
- 多阶段完整 host_tests 通过。事实、SDK 本地客户端、上传、运行态及真实云适配器等有 ASan/UBSan 记录。
- 官方日志校验：307 文件、67006 事件，带一条 orphan 警告通过。历史会话和 manifest 未改动。

## 尚未解决的 TLS 问题

IOB 512 和关闭会话缓存的镜像曾出现 `-0x4380 RSA PKCS#1 verification failed`；IOB 128 完整矩阵仍出现一次证书 ASN.1 解析失败。因此缓存和 IOB 都没有被证明是根因。八次无音频的完整握手探测通过也不能证明整轮稳定。不要用关闭签名校验或丢弃失败样本来掩盖。

目前只做了只读硬件核对，没有新改驱动或 pinmux。已按 velaguard-board-hw 阅读官方资料包的 mb1381-h750xb-b01-schematic.pdf 第 5 页，确认 LAN8740A/MII，并对照 board.h 和现有缓存处理代码。源码中以太网缓冲已对齐并调用 invalidate，尚无确定驱动缺陷。NuttX 既有 SDRAM Normal NC MPU 修复及未提交 QA-04 文件不可随意重做或覆盖。若继续诊断，先找可复现证据，不以猜测开硬件改动。

## 接下来按此顺序推进

1. 核对当前固件来源和 git 状态。新调度的构建/烧录已完成，直接运行 `matrix-foreground/` 的完整固定 30 条；不得覆盖 matrix-iob128-baseline。查看模型 TLS/工具阶段是否下降，是否达到≥27/30，同时跟踪证书错误。需要改动则保留本轮完整结果后再做新的明确来源回归，不拼接最好样本。
2. 完成当前固件的 10 个取消/坏输入场景：record_early、record_limit、asr_early、asr_late、model_early、model_late、tts、playing、missing、not_wav。cancel/ 已有计划，阶段标记未到达须记未覆盖，不能只看 end 命令成功。检查真实 cancel_settled≤2000 ms、PCM 为零、UI/audio 空闲、受控请求结束、线程/堆/socket 稳定及后续真实查询恢复。15 秒上限已有诊断镜像证据，普通固件若再测可先逻辑 ifdown eth0，避免上传环境录音；必须用独立恢复计划确保 ifup 后真实服务恢复，不冒充物理拔网线。
3. 在 model_early 计划的 agent_queued 标记后增加只读 ps，结束清理后再 ps，验证模型线程实际 RR 100→原 FIFO 60。新调度目前只有主机和构建证据。
4. 执行 preemption-final-plan.json：它等真实后台提交后立即提问，避免先等 18 秒导致后台已结束。必须同时取得实际暂停、语音正确完成、原后台请求恢复的标记。早期 preemption-* 的几次失败不能算成功覆盖。
5. 在当前镜像补非空异常总数和前三项的端到端验证，与只读快照、屏幕和实际播报对照；保留完整内容和预算，不删项。旧 scene-retry 只证明数据场景，旧非空语音失败仍开放。
6. 独立完成 5 秒 WAV 自然播放时长回归，排除沿用录音时长提前停止：输入在 Windows Temp vgflash/review-playback/playback05.wav，现没有 HTTP 服务；用有时间上限的 serve_samples.py 启动后执行 review/playback-plan.json。wget 使用小写 -o。played 增长、last_ms 约 5000、自然 idle 可作软件证据，不能代替人耳听音。
7. 汇总 30+10 轮线程、堆、TCP/文件、残留音频、审计 submitted/written/lost。/proc/net/tcp 空输出在当前源码表示无 TCP 连接；也可读 /proc/3/group/fd、/proc/14/group/fd，ps 确认实际组号。若需要补采 COM7 记录，仅从偏移 255177 归档本轮片段，原日志不改。
8. 修复发现的真实软件问题，做对应主机/板端回归，更新 README、pending-manual、maintenance-todo 的真实状态。普通交付必须 fixture/cache/directtool 均关闭，记录最新源码与镜像，正常采集/偏好恢复，关闭自己的测试服务。
9. 只提交本轮明确改动和脱敏证据，不提交本地凭据、其他 QA-04 文件或历史 AI 日志。继续不推送。

无人值守应完成所有可自动工作；真人按键/语音/信噪比、人耳听音、触屏、真实拔电、真实拔网线和服务方音频保留政策仍有外部条件。TLS 完整证书链校验及旧 provision 兼容路径明文落盘也仍开放。BVO-14/阶段 B 不能因为单测或部分板测通过就整体宣布完成。若最后仅剩人工，交出精确操作、当前固件和每项判定；若仍有软件失败，明确列出而非统称人工阻塞。

## 立即可用的执行方式

所有命令从竞赛仓根目录执行。读 skill 后按本仓入口构建：

```bash
make -C app/velaguard/host_tests test
bash scripts/build.sh
```

本目录 handoff-tools 保存了当前机器的原生串口包装脚本；它们只是测试便捷入口，Plan JSON 才是逐条命令。不要并行运行它们。当前新调度固定矩阵尚未运行，可直接执行：

```bash
python3 docs/voice-evidence/2026-10-23/bvo14-closeout/handoff-tools/run_foreground_matrix.py
```

它先读启动状态，再依次跑 anomaly、point、recovery 并调用 check_matrix.py。总过程约 15 分钟，每个计划有 15 分钟上限；COM3 期间禁止其他进程占用。若被打断，先保存 Windows Temp 的 closeout-run.log 到另一个失败文件，不能覆盖现场。包装器只在结束时把当前原始日志复制回目标目录；运行时实时日志在 `/mnt/c/Users/19y/AppData/Local/Temp/vgflash/closeout-run.log`。

单计划示例，输出文件已存在时换新名字：

```bash
python3 docs/voice-evidence/2026-10-23/bvo14-closeout/handoff-tools/run_native_plan.py \
  docs/voice-evidence/2026-10-23/bvo14-closeout/preemption-final-plan.json \
  docs/voice-evidence/2026-10-23/bvo14-closeout/preemption-final.log
```

原生 Windows 工具：

- PowerShell：powershell.exe。
- Python：`/mnt/d/Python/Python314/python.exe`，Windows 路径 `D:\Python\Python314\python.exe`，加 `-X utf8 -u`。
- 烧录目录：`C:\Users\19y\AppData\Local\Temp\vgflash`。复制本次 .debug/nuttx.hex 后运行该目录 flash.ps1，`-OutDir` 为同目录。不要用历史镜像凑数。
- 故障服务源 `scripts/voice_test/round_fault_fixture.py`，固定 192.168.137.1:8443，有≤900秒和≤100请求上限、可用 stop-file 正常停机；当前服务已停。测试证书/私钥在 Windows Temp vgvoice-fixture，只用于本地服务，私钥不打印、不入库。
- 原理图可用 bundled Windows Python 的 pypdf 读取，路径 `/mnt/c/Users/19y/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe`，fitz 不可用。材料位于 `/mnt/f/Project/Embeded/H750B-DK/BOARD INFO/H750B-DK`。

请从核对新调度镜像并运行 matrix-foreground 开始，不要重新追溯整个历史，也不要把当前 26/30 误报成已完成 BVO-14。
