# 板端时间同步：eMMC 持久化 + 开机恢复 + 联网同步北京时间

## Goal

板子没有电池 RTC，断电后系统时钟回到 1970。顶部状态栏时钟、事件日志、日报日期都需要真实时间。本任务给板端加时间同步：每 3 分钟持久化当前时间到 eMMC，开机恢复，联网时 SNTP 同步北京时间。

## Requirements

1. 新增 `app/velaguard/vg_time_sync.c`（Kconfig `VG_TIME_SYNC`，挂在 `VG_BRINGUP_TOOLS` 下），由 `velaguard_app_main` 启动一个后台线程。
2. 开机时从 `/data/velaguard/clock.txt` 恢复时间：读到合法时间（2020-2100 年）就 `clock_settime` 设为系统时钟；文件缺失或损坏则不设。
3. 每 3 分钟把当前时间写回 `clock.txt`（临时文件 + rename 原子替换，防掉电写坏）。
4. 联网时做 SNTP 校时：RJ45 出口有 IP 时向国内 NTP 服务器（阿里 203.107.6.1、腾讯 106.55.184.199）发 UDP 查询，成功后系统时钟设为北京时间（UTC+8 墙钟），并立即持久化一次。开机后首次联网必须校时；在线期间每 30 分钟重复校时。
5. 系统时钟统一跑北京时间墙钟（不依赖 TZ 数据库），HMI 时钟、日志、日报自然显示北京时间。
6. SNTP 组包/解析做成纯函数，进 `host_tests/test_time_sync.c`。

## Constraints / Non-goals

- SNTP 第一版只走 RJ45 出口。lesp 虽支持 UDP，但需要第二套传输路径；wifi 热备期间不校时（记录为已知限制）。
- 不做时区配置界面、NTP 服务器可配置。
- 不做 RTC 硬件驱动（H750B-DK 无电池 RTC，按用户要求走 eMMC 方案）。

## Acceptance Criteria

- [x] `bash scripts/build.sh` 编译通过
- [x] `make -C app/velaguard/host_tests` 新增用例通过
- [x] 板上断电重启后，状态栏时钟从 clock.txt 恢复（SWD 复位实测 vgtime: restore 2026-09-12 14:59:19）
- [x] 插网线后串口日志出现校时成功记录，时钟显示北京时间（clock.txt=1789224798 即 2026-09-12 14:53 北京时间，NTP 已同步）
