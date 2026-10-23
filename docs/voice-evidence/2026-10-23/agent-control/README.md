# 语音 Agent 截止时间与取消的续接验证

日期：2026-10-23。BVO-07/08/13 的修复与有限板测，阶段 A/B 仍未通过。此目录保留成功和失败的原始记录，不将主机回归或合成音频作为真人验收。

## 变更

- 本地 SDK 增加可选绝对单调截止时间；语音从原有 ASR/排队预算继续计时，核心循环和模型传输使用同一个截止时间。旧后台请求保持原接口。
- 核心处理期间注册本轮栈上的取消标志，结束时注销；取消、替换、超时会通知当前传输。过期队列项和迟到回复按请求编号拒绝，旧轮结束不能注销新轮标志。
- 语音只取消自己的 owner；正在执行的告警解释/日报继续运行。只暂存一个尚未提交的后台任务，语音优先后恢复后台请求。
- 受控轮次不读写 LLM 回复缓存，不读取或追加会话 JSONL，避免把语音完整对话写入 eMMC。
- 增加 `vgagent askbudget` 诊断，供短预算与同次开机恢复验证；未注册成设备 Agent 工具。

## 主机与构建

`host-tests.log` 与 `host-tests-final.log` 全套通过；`test_agent_round` 覆盖原预算、超时释放、提交竞态、owner 隔离、语音优先和后台恢复。`client-control-asan.log` 与 `client-control-asan-final.log` 为本轮重新编译的 AddressSanitizer + UndefinedBehaviorSanitizer 检查，覆盖取消指针生命周期、旧轮与新轮隔离、关闭后注销、恢复。

`build.log`、`build-nonshareable.log`、`build-final.log` 构建成功；`firmware*.json` 记录真实镜像、ELF、配置和多仓来源摘要。`flash*.log` 对应实际烧录。首次 ASan 尝试因 WSL 重启后临时可执行文件不存在退出 127，重新编译后通过，未用历史结果替代。

## 首个镜像的 Agent 实测

`agent-deadline.log`，无在线从站，已配置 MiMo，板端直接诊断受控 Agent 通道：

| 请求预算 | 返回耗时 | 结果 |
|---|---|---|
| 30000 ms | 25420 ms | HTTP 200，vg_anomaly_read，total=14 |
| 300 ms | 640 ms | 超时；该请求未进入模型传输 |
| 500 ms | 850 ms | 超时；该请求在前处理后已过期，工具调用为 0 |
| 1000 ms | 1260 ms | 超时；该请求在前处理后已过期，工具调用为 0 |
| 30000 ms | 3790 ms | 同次开机 HTTP 200，再次调用工具，正常完成 |

短预算的额外 260–350 ms 包含 NSH 轮询与调度，不能描述成精确到期即返回。它们没有进入 TLS，不能代替模型等待期间取消证据。前后 `/data/agent/sessions` 文件名列表一致，没有新的 `velaguard:voice:N` 对应 JSONL；未读取历史会话内容。收尾 TCP 仅剩 MQTT 连接，used=882352 B。

## 新 HARDFAULT 与 SDRAM 属性修正

`voice-cancel.log` 前两条 2500/3500 ms 诊断因后台告警解释已经运行而返回 -EBUSY，没有取消后台任务，不能计成短预算测试成功。随后合成语音 ASR 开始时发生 HARDFAULT，整轮取消/恢复计划中断。

- CFSR=0x00008200，BFAR=0xD027D3C4，PC=0x90092AC0。
- `hardfault-symbols.txt` 将 PC 定位至 `fdlist_dupfile` 的 `ldrex ip,[r1]`，调用链为文件引用分配、socket、ICMP ping；这是有效且对齐的 SDRAM 地址。
- `hardfault-registers.log`：MPU region 1 的 RASR=0x110C002D，Normal、不可缓存、共享；语音工作线程栈仅使用 24680/49128 B。
- 上次 SDRAM 映射修复漏掉了独占访问的共享属性要求。[Arm Cortex-M7 TRM 第 5.3 节](https://documentation-service.arm.com/static/5e906b038259fe2368e2a7bb) 说明非共享内存的独占访问只使用内部监视器；不可缓存共享内存会把独占事务送往外部总线。结合本次有效地址上 LDREX 精确总线错误，修正为 Normal、不可缓存、非共享、不可执行。未开启 SDRAM 缓存，不改变引脚/布线。
- `nonshareable-registers.log` 实读 RASR=0x1108002D。后续验证见同目录恢复日志。

故障现场 ELF 在本机临时目录保存，摘要入仓。包含任意堆数据的 SWD 内存转储留在本机临时目录，仓库仅保留寄存器与反汇编，不发布可能含临时敏感数据的堆内容。AI Coding 日志未改写。

## 继续开放

- DNS 的平台 `getaddrinfo` 仍阻塞，尚不能保证 DNS 故障时整个物理请求有界结束。
- 配置/文件前处理和只读工具的存储阻塞不能被传输取消标志抢占；短预算已实测有调度余量。
- 语音事实快照仍用全局发布序号，尚未完成请求绑定，不能宣称已排除所有旧数据串轮问题。
- 完整语音整轮的取消、同次开机恢复、静音不调用 TTS 和资源回收需继续取证。真人发声、听音、触摸、断电保持仍待人工。
- 30 条三意图端到端、30 正常 + 10 故障/取消、多类别重叠和事件环淘汰板测仍未完成；QA-04 未关闭。

## SDRAM 修正后的首组恢复

`nonshareable-recovery.log` 使用 COM7 的明确模拟 RTU 从站，只应答 FC03/04，未修改点表或配置。2500/3500 ms 的两个受控模型请求分别以 deadline 结束，HTTP rc=-6，核心在执行工具前停止；随后 30000 ms 请求 4420 ms 完成，HTTP 200，工具读到 seen=14、total=0。两次 ASR 合成样本均为 200，未再出现上述 LDREX 总线错误。收尾工作线程栈 24680/49128 B，used=882512 B；TCP 包含 MQTT 与一个 ref=0 的关闭过渡连接，不描述成完全无遗留。

此组同时暴露结束操作未设置即时取消标志：结束事件需要等阻塞阶段返回，期间甚至继续执行了工具补取。模型失败的通用错误文本也被 SDK 当作成功，导致进入补取流程。最终版对此增加即时 cancel、跨阶段保留 cancel，并由核心向匹配请求返回失败状态；旧请求失败不能影响新请求，主机用例已覆盖。最终固件与验证另见 `firmware-final.json`、`flash-final.log` 和后续原始串口记录。


诊断修正：前三份计划误用了当时尚不存在的 `vgvoice round end` 子命令，该参数被旧命令静默当作状态查询。因此这些日志不能证明结束按钮行为，也不能据此判定实际结束接口失效。源码中的结束接口确实缺少即时取消，已补充；同时增加明确的 `end requested` 回执，未知 round 子命令现在返回失败。`final-recovery.log` 的后半段还发生串口提示符采集超时，保留为失败记录，后续另测，不把它当作系统冻结或取消通过证据。


源码提交：NuttX `fca4f26223f` 修正 SDRAM 非共享属性；ai_agent `e8e2279`，SDK/核心/模型传输使用同一请求控制。烧录记录保存构建时 HEAD 与 dirty 文件摘要，后续提交不改变已烧录的文件内容。


## 有效结束测试与同次开机恢复

`end-recovery.log` 对应 `firmware-end-diagnostic.json`。未知子命令明确报错。08:02:28 发出实际 `end requested`，08:02:30 回读 visible=0/state=idle、无问题和回答；SDK owner=none。被取消的核心请求 tools=0，没有把迟到结果显示出来。本次取消发生于核心前处理阶段，不冒充模型响应体等待期间的取消。

同次开机下一轮 ASR 200（6460 ms），模型 200（工具调用 vg_anomaly_read，total=0），文字为当前没有异常点位。整段静音窗口未出现 TTS 阶段日志；约 32 秒回读时浮层已自动收起，正文诊断仍能读到结果。结束后目录只有明确保留的合成测试样本 q1.wav 与偏好文件 pref.txt，未新增语音会话 JSONL，TCP 只剩 MQTT，used=882520 B、maxfree=5795432 B，工作线程栈剩余 24448 B。播报开关已恢复测试前的 muted=0。

`final-recovery.log` 的前半段仍可作为另一组真实传输证据：2500/3500 ms 请求分别在 2750/3780 ms 返回，旧请求停止于 rc=-7、新请求 rc=-6，均 tools=0；之后 30000 ms 请求 18910 ms 完成并取到 seen=14/total=0。该日志后半段的结束命令及串口超时不计入成功项。

这些有限样本支持取消隔离与恢复的修复，不等于 30+10 轮资源验收或 30 秒语音成功率已通过。
