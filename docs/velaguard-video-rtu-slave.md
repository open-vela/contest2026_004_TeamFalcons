# 视频录制用 RS485 从站模拟器

对应 [80 秒真机演示分镜](submission/final-defense-20261105-claude-final/80秒视频脚本.md) 的台架从站部分。程序只做一件事：在 PC 的 USB-RS485 适配器上扮演 7 个 Modbus RTU 从站，按分镜的节奏改变回风温度，让板端首页、告警页和日报页出现脚本要求的读数。

板端仍是总线上唯一的主站。程序只回应 FC03 / FC04 读请求，不主动发帧，不写寄存器，不访问板端文件系统。

## 文件

| 文件 | 用途 |
|---|---|
| [scripts/video_rtu_slave.ps1](../scripts/video_rtu_slave.ps1) | 模拟器本体，Windows PowerShell 5.1，无第三方依赖 |
| [scripts/video_rtu_slave.cmd](../scripts/video_rtu_slave.cmd) | 双击入口，参数原样转给 ps1 |
| [config/modbus-slave/video_scene_80s.json](../config/modbus-slave/video_scene_80s.json) | 14 点场景、4 个阶段和自动时间线 |
| `.debug/video-rtu-slave.log` | 运行日志，含 PHASE / MARK / STATUS / STOP 行 |

## 台架数据

除了回风温度，其余点位按答辩快照的数值固定或小幅漂移。带漂移的点每秒刷新一次，画面能看到读数在变。

| id | 名称 | 从站 | 寄存器 | 类型 | 倍率 | 基线值 | 阈值和结果 |
|---|---|---|---:|---|---:|---:|---|
| temp | 温度 | 1 | 0 | int16 | 0.1 | 24.0 ± 0.2 | warn 28 / crit 35，正常 |
| humid | 湿度 | 1 | 1 | uint16 | 0.1 | 88.0 | crit 85，基线告警 1 |
| flood | 水浸 | 2 | 2 | uint16 | 1 | 1.0 | eq 1，基线告警 2 |
| smoke | 烟雾 | 3 | 0 | uint16 | 0.1 | 10.0 | crit 10，基线告警 3 |
| acu_ret | 回风温度 | 4 | 0 | int16 | 0.1 | 26.0 ± 0.2 | crit 80，阶段 2 推到 83.2 |
| acu_set | 设定温度 | 4 | 1 | int16 | 0.1 | 24.0 | 不做告警 |
| acu_run | 空调运行 | 4 | 2 | uint16 | 1 | 1.0 | 不做告警 |
| ups_load | UPS负载 | 5 | 0 | uint16 | 0.1 | 102.3 ± 0.3 | crit 90，基线告警 4 |
| ups_soc | UPS电池 | 5 | 1 | uint16 | 1 | 88.0 | warn 40 / crit 20，正常 |
| ups_bypass | UPS旁路 | 5 | 2 | uint16 | 1 | 0.0 | eq 1，正常 |
| volt_a | A相电压 | 6 | 0 | uint16 | 0.1 | 279.5 ± 0.5 | warn 200 / crit 180，正常 |
| pwr_kw | 有功功率 | 6 | 1 | uint16 | 0.01 | 3.50 ± 0.02 | warn 8 / crit 10，正常 |
| energy_kwh | 正向电能 | 6 | 2 | uint16 | 0.1 | 1234.5，每秒 +0.02 | 不做告警 |
| door | 门磁 | 7 | 0 | uint16 | 1 | 0.0 | eq 1，正常 |

基线是 14 点在线、4 条活动告警、0 离线、10 点正常。回风温度升到 83.2 后变成 5 条告警、9 点正常。这是 2026-11-06 确认的口径：字幕里的 4 条告警保持不变，越限后多出第 5 条。

如果改成基线 3 条、越限后 4 条，把 JSON 里 `humid` 的 `start` 调到 85 以下即可，例如 62.0。

## 与 80 秒分镜的对应

| 分镜 | 阶段 | 操作 |
|---|---|---|
| 00–16 上电与在线点位 | `baseline` | 启动后默认就在这一阶段 |
| 16–28 回风温度越限 | `rise` | 回风温度用 6 秒从 26.0 线性升到 83.2，跨过 80.0 |
| 28–70 AI 建议、日报、问答 | `hold` | 保持 83.2，其余点位继续漂移 |
| 拍完或重拍 | `recover` | 用 5 秒把回风温度降回 26.0 |

自动时间线按 16 秒基线、8 秒升温、56 秒保持排好，总计 80 秒，用于彩排和对照。正式拍摄建议用手动阶段，剪辑点更稳。

## 运行

在拍摄 PC 上打开 PowerShell，进入仓库根目录：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\video_rtu_slave.ps1 -Port COM11
```

仓库放在 WSL 里时，从 WSL 用绝对路径调用：

```bash
/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe -NoProfile -ExecutionPolicy Bypass \
  -File '\\wsl.localhost\Debian\home\hello19y\openvela\contest2026_004_TeamFalcons\scripts\video_rtu_slave.ps1' \
  -Port COM11
```

也可以双击 `scripts\video_rtu_slave.cmd`，不带参数时会列出 COM 口并提示输入。加 `-ListPorts` 只列口不启动。

常用参数：

| 参数 | 说明 |
|---|---|
| `-Port COM11` | RS485 适配器端口，必填；不带时交互选择 |
| `-Auto` | 按 80 秒时间线自动切换阶段 |
| `-AutoDelaySec 5` | 自动时间线开始前的倒计时，留出开录时间 |
| `-ReplyDelayMs 0` | 回复延迟，默认 0；只在做时序实验时调大 |
| `-Hours 8` | 最长运行时长，到时自动停止 |
| `-Seed 20261106` | 随机漂移种子，同一 seed 的彩排可复现 |
| `-SelfTest` | 不开串口，自检 14 点编码、异常码和升温曲线 |

按键：

| 键 | 作用 |
|---|---|
| `1` 到 `4` | 切到对应阶段 |
| `0` | 切到最后一个阶段，即 `recover` |
| `r` | 回到第一个阶段，即 `baseline` |
| `m` | 在日志里写一条 MARK，剪辑时用来对时间 |
| `q` | 退出 |

不想用键盘时，把命令写进控制文件，每次写一个新值：

```powershell
echo rise > .debug\video-rtu-slave-control.txt
```

支持 `baseline` / `rise` / `hold` / `recover`、阶段序号 `1` 到 `4`、`mark` 和 `stop`。同一个值重复写入不会重复触发，要先写别的值。程序启动时会忽略文件里上一次留下的命令。

## 日志

`.debug/video-rtu-slave.log` 追加写入，关键行：

```text
START 2026-11-06T09:17:36+08:00 port=COM11 baud=9600 scenario=velaguard-80s-video seed=20261106
PHASE 2026-11-06T09:17:52+08:00 elapsed_s=16.0 id=rise label=回风温度 26.0 升到 83.2，跨过 80.0 严重阈值
MARK  2026-11-06T09:17:58+08:00 elapsed_s=22.0 phase=rise label=alarm-visible
STATUS 2026-11-06T09:18:02+08:00 elapsed_s=26.0 phase=hold req=322 rep=322 other=26 badcrc=0
STOP  2026-11-06T09:19:00+08:00 req=1180 rep=1180 other=95 badcrc=0
```

`req` 是发给本模拟器的请求数，`rep` 是实际回复数，`other` 是发给其他从站地址的帧（保持静默），`badcrc` 是 CRC 校验失败的字节回退次数。正常运行时 `req` 和 `rep` 应该始终相等。

## 拍摄前检查

- RS485 总线上只有板端一个主站。MThings、Modbus Slave、SSCOM 等组态或监视工具先关掉，否则会出现两个主站或两个从站抢答。
- 板端已确认点表是视频用的 14 点，回风温度严重阈值 80.0。模拟器不修改点表。
- 当前板端已确认表多了一个 `s8_f4_r1`（从站 8、FC04、寄存器 1）。本场景只服务 7 个从站，这个点会显示离线。拍摄前用现有 NSH 候选流程删掉它，或把它补进场景 JSON。
- 端口号以实际枚举为准。本机 CH340 适配器是 COM11，板端调试口是 COM3，不要混用。
- 视频脚本写的“告警页自动弹出”与当前固件不一致：`gui/main/ui/` 里没有告警变化自动跳转的逻辑，首页告警行会变红，告警页需要点状态栏的告警徽标或首页的“告警”按钮。拍摄时按固件实际行为改成分镜里的手动切页，或先改固件再拍。
- 板端 UI 在 9600 下每个点位约 1 秒轮询一次，升温的 6 秒里能看到 6 到 7 个中间值，跨越阈值的瞬间会落在最后两次采样内。

## 自检

不开串口，直接验证编码和异常路径：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\video_rtu_slave.ps1 -SelfTest
```

期望最后一行是 `selftest: pass=22 fail=0`。自检覆盖：

- 已知帧 `01 03 0000 0001` 的 CRC 是 `84 0A`
- 14 个点在基线下的原始寄存器值
- 未列入场景的从站地址保持静默
- 未定义寄存器返回异常码 02，FC06 返回 01，数量 0 返回 03
- 升温曲线在 3 秒时约 546（54.6 ℃），6.5 秒时是 832（83.2 ℃）
- 复位阶段回到 260（26.0 ℃）

## 证据

录制时保留 `.debug/video-rtu-slave.log`，和原片、字幕文件一起存档。日志里的 `PHASE` 和 `MARK` 时间戳能对上分镜的秒数，答辩被问到读数来源时直接出示。模拟器的读数是测试激励，不是真实传感器采集，讲稿里按脚本既有口径说明台架从站为模拟从站。
