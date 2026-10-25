# 语音运维助手改造验收

日期：2026-10-25。依据用户在 Codex 会话（01a170a2）确认的共同理解：
原始问题直接交给 LLM，不再靠三个固定意图和关键词后缀拦截；LLM 自由组合只读工具；
多候选点位全部查询并列说明；屏幕完整回答、语音简明摘要；查询阶段最多 60 秒；
追问上下文闲置 5 分钟自动过期，手动结束或重启立即清空；系统概况默认业务与连接，
资源信息按需追问；数据缺失如实说明，执行权限保持只读。

## 本次改动

- `packages/ai_agent`：语音作用域提示词改为运维助手（中文，含 播报： 摘要行约定）；
  工具白名单加入 `vg_run_report`、`vg_status_read`（C 层强制）；`max_tokens` 256→1024；
  移除语音请求的快照捷径（模型必须自己组织第二轮回答）。
- 参赛仓 `app/velaguard`：
  - 新增 `vg_status_read` 只读工具：点位在线、告警统计、网络/MQTT、AI 连通性、运行时长，
    数据来自运行时内存层（`vg_runtime_point_stats`），不增加 eMMC 读。
  - `vg_point_read` 歧义候选从只列名改为每候选带实时值，支持并列说明（候选上限 3→6）。
  - 语音轮次：原始问题 + 上一轮问答（5 分钟内）直接入队；回复经 `vg_va_split_reply`
    拆分为屏幕全文和 播报： 摘要并做长度/UTF-8 校验；屏幕文本带「AI 回答」前缀。
  - 数据依据防线：本轮没有任何 vg 工具执行而回复含数字时拒绝显示（防止编造读数）。
  - 预算：识别成功后查询阶段独立 60 秒（`VG_VS_AGENT_BUDGET_MS`）；上下文闲置
    `VG_VS_IDLE_MS` 2 分钟→5 分钟；手动结束清上下文。
  - 删除三意图分类器 `vg_voice_intent.c` 与 `VG_VOICE_DIRECT_TOOL` 直查路径。
  - agent 回复缓冲 192→2048 字节。

## 主机验证

`make -C app/velaguard/host_tests test` 全部通过（含新增：拆分器 7 组、
60 秒查询预算、执行计数器、数值检测、候选带值格式化）。见 `host-tests.log`。

## 板端验收

固件：`firmware-final.sha256`（08e165d2…）。问句 WAV 由板上 MiMo TTS 生成后回喂 ASR，
每轮 `vgvoice round ask <wav>` 全链路执行。日志：`final*.log`、`case*-answer.txt`。

| 场景 | 问题 | 结果 |
| --- | --- | --- |
| 自然问法 | 查一下A相电压 | 识别+vg_point_read；回答含 volt_a 单位/阈值(≤200/≤180)/读数失败原因与建议（`repro1-text.log`） |
| 带状态后缀 | 查一下A相电压的状态 | 旧版此处报「没有找到点位」；现组合查询 volt_a 并关联 curr_a，推理通信链路问题（`accept2b`） |
| 系统概况 | 告诉我当前系统状态 | 旧版此处拒答；现组合 vg_status_read + vg_run_report，给出通信质量/点位在线/异常概况（`case3-answer.txt`） |
| 连续追问 | 它现在正常吗 | 衔接上一轮上下文，回答「情况和刚才一样，没有恢复迹象」（`case4-answer.txt`） |
| 多候选并列 | 查一下UPS | 三个 UPS 点位（负载/电池/旁路）各自阈值与状态并列（`case5-answer.txt`） |
| 防编造 | 查一下UPS（修复前样本） | 模型未调工具编造 10 个点位读数；加固后同问法 tools_ran=1 正常作答；防线日志 `data basis tools_ran/numeric` |
| 手动结束 | round end 后再问「它现在正常吗」 | 请求仅 40 字节（无上下文行），上下文已清空（`case6-post-end.txt`） |

播报：场景 1/2/5 流式播报完整（speech 112–140 字节，`split=1`，`play_done`）；
场景 4 首次合成 30 秒超时，按设计保留文字并提示重试播报（`final2_out.txt`）。

## 已知问题（本次未修，另行立项）

- `vgvoice tts` 诊断命令与语音轮次并发使用语音连接槽时偶发 HARDFAULT
  （12:45 一次，复位后不复现；`accept1.log`）。规避：诊断 TTS 与 round ask 不同时下发。
- eMMC/FAT 在后台 advice/daily 报告写入与语音文件读取并发时偶发全目录 open 超时
  （12:41、13:01 两次，硬复位恢复）。本次已把 vg_status_read 改为纯内存数据源以降低
  并发读；根因待查（`fsprobe*.log`、`fsdiag.log`）。
- 模型偶发把 vg_status_read 输出中的字段误读为「接口被配置禁用」；不影响数据正确性。
