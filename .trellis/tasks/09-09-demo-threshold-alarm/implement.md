# 运行时点表与告警：动手顺序

2026-09-11：本任务是手册 §14.1 / 推进方案 §10 规定的下一步。`vgpoint` 编码已收口。板测写入优先用仓外 `F:\Project\uppercomputer`（试读后人确认落盘）；仓内 `scripts/vgpoint_host_apply.ps1` 作无人值守抽检。

- [ ] 板测写入：上位机或 `vgpoint_host_apply.ps1` 走完 `apply --confirm`，采集已读已确认表
- [x] 屏幕按已确认表的 `cmp`/`warn`/`crit`/`fail_n` 告警；删除按点名称识别水浸和烟雾的代码
- [x] 待处理告警写到 `/data/velaguard/pending_alarm.txt`
- [x] 屏幕读日报目录改为 `/data/velaguard/reports`
- [x] `bash scripts/build.sh` 编译带屏固件通过（板测写入与注水浸/拔线仍待做）

```bash
bash scripts/build.sh
powershell.exe -ExecutionPolicy Bypass -File scripts/flash.ps1
```
