# 9/17 晚提交计划（按官方作品提交模板）

> 个人截止：**2026-09-17（周四）晚**上传压缩包并推送仓库。官方截止 9/20，9/18–9/20 只作意外缓冲，不排工作。
> 今天 2026-09-12（周六）。可用：9/12 下午晚上 + 9/13–9/17 五个整天。
> 模板来源：`2026 首届 openvela AI 硬件开发者大赛 - 作品提交模板.docx`（评审直接 clone 专属仓编译验证；源码与日志不进压缩包）。

## 1. 交付物清单（对照模板一、）

| # | 材料 | 必/选 | 我们的决定 | 产出路径 |
|---|---|---|---|---|
| 1 | 技术报告 .pdf/.docx，按模板二、 | 必交 | 做。Markdown 起草 → pandoc 转 docx → 导出 pdf | `docs/submission/tech-report.md` → `VelaGuard-技术报告.pdf` |
| 2 | 演示视频 ≤5 min，含功能演示 / 交互操作 / AI 能力展示 | 必交 | 做。分镜 `docs/demo-video-script.md` | `VelaGuard-演示视频.mp4` |
| 3 | 作品展示照片：前 / 后 / 侧 / 俯视 | 硬件实物必交 | 做。板测日顺手拍，含扩展板接线 | `docs/submission/photos/` |
| 4 | 海报 .pdf/.jpg/.pptx，一图讲清 | 可选（入围线下） | **做一张**：复用 README 三张 SVG + 一句话定位，半天内 | `VelaGuard-海报.pdf` |
| 5 | 答辩 PPT | 可选（入围决赛） | 不做。入围后再从技术报告抽 | — |
| — | 压缩包 `<队伍名称>-<作品名称>-<仓库名称>.zip` | 必交 | `TeamFalcons-VelaGuard-contest2026_004_TeamFalcons.zip`（队伍名以报名登记为准） | 官网上传 |
| — | 专属仓：源码 + `logs/` + README 可复现编译 | 必交 | 提交分支合入；README 开头四项：作品名称、所属赛道、运行方式、简介 | GitHub |

## 2. 评分 → 报告章节 → 我们的证据

模板三、的对照表决定了每一分从哪一节拿。这里把每节要填的东西钉死到仓内已有材料，9/15 写报告时不再找。

| 维度（分） | 报告章节 | 证据来源（仓内） | 还缺什么 |
|---|---|---|---|
| 技术难度 30 | 3.2 方案 / 3.3 原理 / 3.4 实现 / 3.5 测试 | README 分层表、`system-map.svg`、手册 §2.2 §8 §16、`docs/stm32h750b_dk_qspi_xip_deep_dive.md`、7 个 PR、`packages/ai_agent` HMI 内存补丁、扩展板 `docs/velaguard-expansion-board.md`（含 BOM） | **3.5 量化数据**（见 §4 板测日采集清单） |
| 产品创新 20 | 2 摘要 / 3.1 绪论 | README「解决什么问题」表、`agent-boundary.svg`、手册 §2.3 | 创新点三条压成一段：本地安全环 / C 层只读边界 / Cortex-M7 首跑 ai_agent |
| 完整度 20 | 3.5 测试 / 源码 / 照片 | `scripts/stage*_accept*.ps1` 结果、host_tests | 功能测试表、可靠性数据、照片 |
| AI 开发 10 | 3.3 AI 算法 / 3.6 AI-Native | ai_agent + MiMo 直连（`llm_proxy`、`vgprovision` 加密 key）、`logs/Foleaf/` 311 个会话、`.agents/skills/velaguard-*` 4 个自建 Skill、官方 skills 使用记录 | 3.6 表格数字：AI 代码占比口径、工具列表、MCP、Token 用量（MiMo 控制台） |
| 商业潜力 10 | 3.7 展望 | README 目标用户、`docs/plan-cloud-backend.md` C1/C5 多设备看板 | 一段：受众 / 模式 / 规模化 |
| 展示效果 10 | 视频 / 海报 | `docs/demo-video-script.md`、三张 SVG | 成片、海报 |

选题方向填法：**AI 硬件产品创新（主）+ 新硬件平台适配（驱动开发部分）**。依据 `contest_overview.md:53`「不作为独立赛道，鼓励多方向组合」；H750B-DK 属已支持板，所以 3.4「是否完成全新硬件平台适配或驱动开发」答「是：驱动开发」，列 QSPI XIP boot stub、ETH MII、显示加速、UI 性能、ESP8266 netinit、MQTT PAL hook，不声称"全新平台适配"。

## 3. 日程

| 日期 | 上午 | 下午 / 晚 | 当日出口 |
|---|---|---|---|
| 9/12 六 | — | 告警恢复 + unlink + 标签三处代码；host 单测；build。技术报告骨架建好 | 固件可烧；`tech-report.md` 有 7 节标题和每节数据来源 |
| 9/13 日 | **告警板测**（`09-09-demo-threshold-alarm` 出口）+ 采集 3.5 数据 | **拍实物照片**（前/后/侧/俯 + 接线特写）；板子通电**开始 24 h 连续运行**（3.5 稳定性数据） | 拔线→离线→插回→恢复录屏；照片 4+ 张；数据表第一批 |
| 9/14 一 | **Agent 同机试验**（限时 8 h，`09-09-samefw-agent-spike`） | 同一天��：`ask` 时延 / 迭代数 / 工具调用数记录；**第 6 拍拒绝写请求必录**；读 24 h 运行结果 | 书面结论 A/B；3.5 Agent 性能数据；§14.2 精简版（注入 5 次） |
| 9/15 二 | **技术报告全文**（按 A/B 定口径） | README 重写 + claim 清理（`judge-submit-pack/prd.md` 追加节）；分镜定稿 | 报告初稿可读；README 评委 5 分钟读完 |
| 9/16 三 | **拍摄**（彩排 2 遍 + 正式） | 剪辑 ≤5:00；海报一页；报告二稿（补视频截图） | 成片 v1；海报 pdf；报告 pdf 可导出 |
| 9/17 四 | 报告终稿导出 pdf；`validate-log.py`；`git add logs/` + 代码 → commit → push → PR → 自合；7 个 PR 链接复核 | 打 zip；官网上传；**晚上前完成** | 上传回执；远端仓库与本地一致 |

溢出砍项顺序：海报 → §14.2 精简版 → `last_alarm.md` 上屏（A 分支胶水）→ 分支 A 改 B。**不砍**：告警板测、照片、技术报告、视频、日志校验、合入。

## 4. 9/13 板测日：3.5 量化数据采集清单

模板 3.5 要求"务必提供量化数据"，这一天顺手把能测的全测了，每项记数字 + 测法。

功能测试（表格：项 / 方法 / 结果）：
- [ ] 空表冷启动首页为空
- [ ] 上位机 add→test→confirm→首页出现（记从 `apply --confirm` 到首页刷新的秒数）
- [ ] 水浸 = 1 → 告警页弹出（记注入到弹出的时延，秒表或串口时间戳；采集周期 200 ms，理论 < 0.5 s）
- [ ] 拔 485 → 离线（滑窗 5/8 轮确认，理论 ≈ 1.0 s；瞬时抖动不再误判离线）
- [ ] 插回 → 恢复（记时延）
- [ ] `pending_alarm.txt` 写入 / 恢复后删除
- [ ] 拔网线后以上全部重复一遍成立

性能 / 资源（表格）：
- [ ] 开机到首页时间（秒表）
- [ ] 采集周期与每轮总线读耗时（`vgstats dump` 延迟分布）
- [ ] 片内 SRAM 静态占用（map 文件；用 `codesize` skill 出 .text/.data/.bss 表）
- [ ] SDRAM 堆余量、帧缓冲占用（`free` 或启动日志；带屏固件**不要**跑调 `mallinfo()` 的 heap 命令）
- [ ] 固件镜像大小（`.debug/nuttx.hex` → bin 大小）、boot stub 大小
- [ ] LVGL 帧率（HMI 有 fps 显示则记；无则跳过，不编）

可靠性（9/13 晚开始 24 h，9/14 读）：
- [ ] 连续运行时长、期间告警计数、是否复位（串口日志 uptime）
- [ ] 误报 / 漏报：注入 N 次记命中次数（和 §14.2 合并）
- [ ] 异常恢复：拔插 485 三次、拔插网线三次，每次恢复时间

9/14 Agent 数据（分支 A 现场；B 用 08-30 记录）：
- [ ] `ask` 端到端时延、迭代数、工具调用数（08-30 记录：115 s / 6 iters / 6 tools）
- [ ] 每请求 token 用量（MiMo 控制台）
- [ ] 写操作次数 = 0（syslog 无 blocked 之外的写命令）
- [ ] 拒绝写请求：blocked 日志行截图

## 5. 技术报告写作分工（9/15）

`docs/submission/tech-report.md`，每节先粘来源再改写。摘要 ≤300 字最后写。

| 节 | 内容要点 | 主要来源 |
|---|---|---|
| 1 信息表 | 作品 VelaGuard；队伍 Team Falcons（报名名为准）；分工：1 人；方向：AI 硬件产品创新 + 驱动开发 | — |
| 2 摘要 | 背景一句、方案一句、创新三条、量化成果三个数字（PR 数、告警时延、Agent 工具调用/写操作 0） | 3.5 数据 |
| 3.1 绪论 | 陌生 485 总线接入痛点；难点：Cortex-M7 跑 ai_agent（SRAM 92.5%）、XIP 下 OTA、单活动链路；创新：本地安全环 / C 层只读边界 / Agent 只讲不做 | README、手册 §1 §2.3 |
| 3.2 方案 | `system-map.svg`；端云划分与断网降级（手册 §2.2 原文）；选型：H750B-DK vs ESP32-S3/Gemini-S1（RS485+屏+eMMC+ETH 一板齐）；直连 MiMo vs Bridge（ADR-0002 → 直连，理由 key 加密 eMMC、少一跳） | 手册 §8.2、BOUNDARY V4 |
| 3.3 原理 | 云端模型 MiMo：`llm_proxy` OpenAI 兼容、Bearer key、`vgprovision` 加密存储；关键机制：`vg_alarm_eval`（cmp/warn/crit/fail_n）、候选→试读→确认、net_mgr 指数退避；openvela 能力：图形 LVGL、AI ai_agent；对 openvela 的拓展：7 PR + `packages/ai_agent` HMI 内存路径（静态栈 16 KB、缓冲 4 KB、lazy loop、跳过 mallinfo） | 代码、`agent_config.h:411-431` |
| 3.4 实现 | 固件分层表；数据流图：采集→eval→pending→Agent→last_alarm→HMI；硬件：H750B-DK + 扩展板（RS485 UART7、ESP-01 USART2、DO、BOM 表）、驱动开发「是」+ PR 表；UI 页面截图；Skill：`alarm_interpretation.md` / `operations_report.md` / `modbus_query.md` 的定义 + 触发场景（事件 / 定时 / 提问） | README、`docs/velaguard-expansion-board.md`、`vg_agent_seed.c` |
| 3.5 测试 | §4 三张表 + §14.2 结果 + 24 h 运行 | 板测日 |
| 3.6 AI-Native | 代码占比口径：按 git 提交中由 AI 会话产生的文件行数估算，注明；工具：Claude Code / Codex / OpenCode / Cursor / Grok Build（与 `logs/` 标签一致）；MCP：如实（有则列，无则写"未使用"）；Skills：用了官方 `openvela-build`、`contest-log-collector`、`nuttx-driver-development`、`driver-code-reviewer`、`codesize`、`kconfig-tweak`、`submit-pr`；新增 `.agents/skills/velaguard-alarm-to-screen`、`velaguard-candidate-confirm`、`velaguard-board-inner-loop`、`velaguard-board-hw`、`mthings-automation-config-skill` + 板端 3 份运行时 Skill；Token：MiMo 控制台数字；补充：效率提升与踩坑（mallinfo 断言、Cursor 日志标签） | `logs/`、`.agents/skills/` |
| 3.7 展望 | 成果；受众：嵌入式 / 自动化集成 / 系统调试人员；模式：网关硬件 + 组态工具 + 云看板订阅；规模化：多设备 C1/C5；不足：心跳关闭、明文 MQTT、OTA 未做——链接 `plan-cloud-backend.md` | README、云端计划 |

诚实原则：报告里每个"已实现"都要能在仓库里指到文件；未做的写在 3.7 不足，不写在 3.4。

## 6. 9/17 提交动作（按顺序）

1. `python3 ../.claude/skills/contest-log-collector/tools/validate-log.py logs/` 通过；不改 JSONL 正文
2. README 开头四项 + 构建复现命令再跑一遍 `bash scripts/build.sh` 确认干净树能编
3. `git add logs/ docs/ app/ gui/ README.md ...` → 检查 `git status` 无 secrets（`secrets/`、token）→ commit → push fork → PR 回专属仓 → 自合
4. 7 个公共仓 PR 页面逐个打开确认
5. 压缩包：技术报告 pdf + 视频 mp4 + 照片文件夹 + 海报 pdf → `TeamFalcons-VelaGuard-contest2026_004_TeamFalcons.zip`
6. 官网上传，保存回执截图到 `docs/submission/`

## 7. 明确不做（到 9/17）

周报、OTA、规则库、Bridge、遥测/告警上云、语音、屏上编辑点表、上位机 GUI 收仓、新驱动 PR、开机同时拉 HMI + Agent、打开带屏心跳、改静态内存布局、答辩 PPT。云端见 [`plan-cloud-backend.md`](plan-cloud-backend.md)。

## 8. 常用命令

```bash
bash scripts/build.sh
powershell.exe -ExecutionPolicy Bypass -File scripts/flash.ps1
powershell.exe -ExecutionPolicy Bypass -File scripts/vgpoint_host_apply.ps1 -PointsFile scripts/vgpoint_demo_points.json
make -C app/velaguard/host_tests test
python3 ../.claude/skills/contest-log-collector/tools/validate-log.py logs/
pandoc docs/submission/tech-report.md -o VelaGuard-技术报告.docx
```

板端：`vgpoint list` / `vgpoint get` / `vgstats dump 1` / `ls /data/velaguard/pending_alarm.txt` / `ps`（确认 ai_agent 自启）/ `ai_agent` → `vela> ask ...`。带屏固件不跑调 `mallinfo()` 的 heap 命令。
