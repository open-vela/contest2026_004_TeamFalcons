# 运行时点表与告警：动手顺序

2026-09-11：本任务是手册 §14.1 / 推进方案 §10 规定的下一步。`vgpoint` 编码已收口。板测写入优先用仓外 `F:\Project\uppercomputer`（试读后人确认落盘）；仓内 `scripts/vgpoint_host_apply.ps1` 作无人值守抽检。

- [ ] 板测写入：上位机或 `vgpoint_host_apply.ps1` 走完 `apply --confirm`，采集已读已确认表
- [x] 屏幕按已确认表的 `cmp`/`warn`/`crit`/`fail_n` 告警；删除按点名称识别水浸和烟雾的代码
- [x] 待处理告警写到 `/data/velaguard/pending_alarm.txt`
- [x] 屏幕读日报目录改为 `/data/velaguard/reports`
- [x] `bash scripts/build.sh` 编译带屏固件通过（板测写入与注水浸/拔线仍待做）

## 2026-09-11 核对代码后追加（拍片前必做）

- [x] 告警恢复：`vg_model_set_live` 在线分支里，若活动告警属于本点且 `vg_alarm_eval` 返回 `NONE`，清 `s_alarm.active` 并记「告警恢复」日志（现在从不清，插回 485 后告警页永远显示离线、秒数一直涨）
- [x] 恢复时 `unlink /data/velaguard/pending_alarm.txt`（`write_pending` 文件存在即跳过，固件没有任何删除点；彩排一次后正式拍摄的新告警写不进去）
- [x] 告警页「AI 推测」标签诚实化：`vg_page_alarm.c:135-151` 是 HMI 本地拼的模板，不是 Agent 输出。无 `last_alarm.md` 时标「规则摘要」；只有读到 Agent 写的 `/data/velaguard/reports/last_alarm.md` 才标「AI 推测」（读文件这半步等 samefw 试验有结论再决定做不做）
- [ ] 板测时点一遍 DIAGNOSIS / OTA / TREND 页：确认板端没有入口触发 PC 模拟器的 mock 场景（`vg_model.c:1009` OTA 报价 9.2.0、「开始 AI 诊断」定时器）；有则屏蔽，拍片不进这些页
- [ ] 拔线→告警页离线→插回→告警恢复，完整走一遍并录屏留证

## 2026-09-12 追加：离线判定改滑窗失败率

- [x] 离线判定由连续 `fail_n` 轮失败改为滑窗失败率：最近 8 轮（约 1.6 s）内失败达 `max(fail_n, 5)` 轮才判 Offline（`vg_model.c` 滑窗 ring；零星抖动不再误判，拔线约 1 s 触发，仍满足分镜 <2 s 节拍）
- [x] `vgmodbus` 读取串行化：读前拿 `vg_bus_lock`（有界等待 1 s），消除与 HMI 采集并发时的半双工碰撞（此前碰撞会两边同时失败，是误判离线的真实来源之一）
- [x] 报告纯文本化：operations_report / alarm_interpretation Skill 改为禁止 Markdown 语法（LVGL label 渲染不了 #、*、|）；`write_if_missing` 升级为 `write_seed_file`（内容变更即覆盖，新 Skill 文案能落到已刷机的板）；报告页 `board_read_latest_report` 读取时兜底剥离残留 Markdown 装饰

```bash
bash scripts/build.sh
powershell.exe -ExecutionPolicy Bypass -File scripts/flash.ps1
```
