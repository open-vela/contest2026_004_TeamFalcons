# 9/20 前提交：按什么顺序做

父任务不改产品代码。不要把本父任务标成进行中。一次只推进一个子任务。

## 2026-09-11 整理：现在做哪件

手册 `VelaGuard_项目手册.md` §14.1 与推进方案 §9.2 / §10 的 9/20 缺口，是本地告警能上屏（不插网线也能弹出）。因此**下一步推荐 `09-09-demo-threshold-alarm`**。

| 子任务 | 状态 | 怎么看待 |
| --- | --- | --- |
| `09-10-host-nsh-vgpoint` | 编码清单已勾完 | 不再扩功能。完整 `apply --confirm` 要人回车，放到告警板测时顺带做。擦表后首页为空未目视，告警任务冷启动路径会覆盖。 |
| `09-09-demo-threshold-alarm` | **下一步（已 in_progress）** | 去掉按点名称认水浸/烟雾；按已确认表比较；写 `pending_alarm.txt`；报告页读 `/data/velaguard/reports`。 |
| `09-09-samefw-agent-spike` | 等告警板测通过 | 限时约 8 小时。不要提前开。 |
| `09-09-judge-submit-pack` | 等同机试验有结论 | README/分镜要和真实画面一致。不要提前写死口径。 |

阶段 1 父任务 `08-30-stage1-min-product` 的子任务已全部归档。它剩下的演示 MVP 与手册对照，由本父任务承接，不要再在阶段 1 清单里平行开工。

## 规划本身（本步）

- [x] 需求写清，工作顺序已确认：先告警上屏，再限时试验带屏 Agent
- [x] 设计、实施清单、四个子任务已建
- [x] 上位机写入编码已落地；下一步是屏幕告警

## 第一步：上位机写入点表

子任务：`09-10-host-nsh-vgpoint`

- [x] NSH `vgpoint` 与 COM3 脚本按协议写入候选，试读后带确认才落盘（无人值守抽检 9/9；完整 confirm 需人回车）
- [x] 采集和首页只读已确认表；无点表时首页为空（代码已切源；擦表后屏上看空待告警板测顺带确认）

## 第一步后半：告警上屏

子任务：`09-09-demo-threshold-alarm`

- [ ] 删除按点名称识别水浸和烟雾的代码
- [ ] 超阈值或拔从站后，告警页有显示，并写入 `/data/velaguard/pending_alarm.txt`
- [ ] 屏幕读日报的目录改为 `/data/velaguard/reports`
- [ ] 编译带屏固件；用 `F:\Project\uppercomputer`（或仓内 `vgpoint_host_apply.ps1`）写入演示点后再看告警

不要再平行做 `09-09-nsh-vgpoint-host-editor`。

## 第二步：带屏固件上启动 Agent（约 8 小时）

子任务：`09-09-samefw-agent-spike`

必须等告警上屏在开发板上通过。

- [ ] 烧录第一步的带屏固件，确认屏幕正常
- [ ] 等界面和网络起来后执行 `ai_agent --daemon`。不要在开机时自动同时启动两者
- [ ] 做成：日志正常、能提问并看到工具调用、报告页能打开新日报。分镜写成一次开机
- [ ] 做不成：当天停止，记下现象。分镜仍只烧作品主线：拍告警和磁盘上已有日报
- [ ] 不要打开心跳，不要继续改内部静态内存

## 第三步：评委要看的文档

子任务：`09-09-judge-submit-pack`

第二步有结论之后再定 README 口径，避免文档和视频对不上。

- [ ] README：产品说明、编译命令、演示步骤、公共仓库 Pull Request 列表、赛道证据
- [ ] 对照项目手册第 14.1 节写演示范围
- [ ] 按推进方案第 11 节写 5 分钟分镜；拍不到的段落删掉或改成实际能拍的
- [ ] 提醒准备作品介绍文档和成片；专属仓库合入提交分支
- [ ] 校验 `logs/` 目录，不要改会话文件正文

校验命令：

```bash
python3 ../.claude/skills/contest-log-collector/tools/validate-log.py logs/
```

## 父任务何时结束

上面三条验收都有结果（做成，或写明同一套固件上 Agent 未做成、改用已有记录）之后，归档三个子任务，再归档本父任务。

## 上板时常用命令

```bash
bash scripts/build.sh
powershell.exe -ExecutionPolicy Bypass -File scripts/flash.ps1
```

在设备 shell 里查看待处理告警：

```text
ls /data/velaguard/pending_alarm.txt
```

试验 Agent 时执行 `ai_agent --daemon &`。不要在带屏固件上跑会调用 `mallinfo()` 的 heap 命令。

## 做不成时

带屏 Agent 试验失败，不影响已经拍到的本地告警，也不改烧第二套固件。具体退法见 `design.md` 最后一节。
