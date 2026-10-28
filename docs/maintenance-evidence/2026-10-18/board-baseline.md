# 新固件板端基线（阶段 B）

日期：2026-10-18，Asia/Shanghai。对象为本次构建并烧录的新固件。

## 固件身份

| 项目 | 值 |
|---|---|
| `uname -a` | `NuttX 0.0.0 75cbc8eb7a5 Oct 18 2026 14:37:55 arm stm32h750b-dk` |
| nuttx 提交 | `75cbc8eb7a5` |
| ai_agent 提交 | `131642d` |
| MQTT-C 提交 | `4dbb7f3` |
| apps 提交 | `0ea954b79` |
| 参赛仓 HEAD | `31f1cdd` |
| nuttx.hex SHA256 | `afd8f1e30d42aa3ae33fb58eaa8039ee17bcadf62ddc9ea7e3fb06c6697cf493` |
| nuttx.bin SHA256 | `68b2f32f37d85d78013c62151fe531a6a87a5daba51900d785b38d23a1afe13a` |
| qspi_bootstub.hex SHA256 | `877aa3cf6072d66582b33eac555ded7cd5603bacd5dc794915fbcba425f6119e` |

与旧长测固件（`581e576b65a-dirty`，构建于 2026-10-17 13:03:04）不是同一份产物。

## 烧录

- 时间：2026-10-18 14:52:36（`FLASH_EXIT=0`）。
- 方式：STM32CubeProgrammer 2.22.0，SWD，`mode=UR`，外部 loader `MT25TL01G_STM32H750B-DISCO.stldr`。
- 目标：`nuttx.hex` 1.20 MB → QSPI `0x90000000`；`qspi_bootstub.hex` 720 B → 内部 `0x08000000`。
- 两段均 `Download verified successfully`，随后 `MCU Reset`。
- ST-LINK SN `0043001D3137511133333639`，板压 3.28V。
- 原始日志：`flash.log`。

## 主机侧前置验证

| 项目 | 结果 | 日志 |
|---|---|---|
| `make -C app/velaguard/host_tests clean test` | 30 个目标全部通过 | `host-tests.log` |
| `cmake --build .debug/hmi-headless -j 4` | 通过 | `hmi-build.log` |
| `ctest --test-dir .debug/hmi-headless` | 5/5 通过 | `hmi-ctest.log` |
| `bash scripts/build.sh` | 通过，本次增量无 warning/error | `build.log` |
| 静态 SRAM | 292,252 / 524,288 字节（55.74%） | `build.log` |

## 启动基线

启动日志：`boot.log`（经 SWD 复位后在 COM3 捕获，捕获时刻 2026-10-18 14:54）。

| 检查项 | 观测 | 结论 |
|---|---|---|
| eMMC 挂载 | `eMMC mounted at /mnt/emmc`，`Symlink /data -> /mnt/emmc/data` | 正常 |
| 时间同步 | `vgtime: restore 2026-10-18 14:53:41`；后续 `vgtime: synced` | 正常 |
| 配置 | `vgcfg: OK seq=6 name=vgpoint` | 见下"设备名" |
| LVGL | 480x272，fb `0xd0000000`，fblen 522240，触摸 `/dev/input0` 打开成功 | 正常 |
| HMI | `vghmi: autostart ok`，`home fleet n=14`，`acq start ok points=14` | 正常 |
| Agent | `vgagent: ai_agent autostart ok (stack=32768)`，各阶段 `rc=0` | 正常 |
| 网络 | `eth0 192.168.137.172`，`vgnet: mqtt online network=rj45` | 正常 |
| 系统栈 | `test_boot_stack` 主机检查 8192 | 正常 |

启动过程中 `vgnet: join 19y failed` 是 WiFi 关联失败，当前走 RJ45，不影响主线。

## 点位与堆

- `vgpoint list`：`OK cmd=list table=committed n=14`，14 个点与原始场景表逐字段一致（temp/humid/flood/smoke/acu_ret/acu_set/acu_run/ups_load/ups_soc/ups_bypass/volt_a/pwr_kw/energy_kwh/door）。
- `vgpoint get`：14 行 `VALUE`，全部 `value=- ok=0`，`OK cmd=get table=committed n=14`。RS485 从站缺失时如实报告无数据，未伪造数值。
- `cat /proc/meminfo`：total 7,013,984；used 317,128；maxused 428,024；nused 996。
- `cat /data/velaguard/hmi_fleet.txt`：`n=14`。

## 设备名

`vgcfg dump` 与双槽文件都显示设备名为 **`vgpoint`**：

```text
{"schema_version":1,"seq":6,"committed":1,"crc32":4044018832,"device_name":"vgpoint"}
```

代码里唯一能写这个名字的入口是 NSH `vgcfg commit <device_name>`（`vgcfg.c:96` 调 `vg_config_commit`），工厂默认值是 `factory`（`vg_config_store.c:106`）。点表提交路径不调用 `vg_config_commit`，符合 PT-02 的修复预期。因此 `vgpoint` 是此前某次人工 `vgcfg commit` 留下的状态，不是本次固件的写入行为。

这是观感问题，不是功能故障；后续若要让首页或上报显示正式设备名，需要人工决定新名字并单独执行一次 `vgcfg commit`。

## 存储诊断

`vgstoretest 50` → `OK rounds=50 bytes=25600 elapsed_ms=96990`（约 97 秒）。

对照旧固件同命令的 83,390 ms，本次略慢，属同量级。

## Agent 与工具

- `vgagent tools`：vg 提供方注册 2 个只读工具 `vg_point_read`、`vg_run_report`。
- `vgagent tool vg_point_read temp` → `rc=0`，返回 `vgquery: point id=temp ... reason=read_failed`（从站缺失，如实报读失败）。
- `vgagent tool vgpoint` → `rc=-1`，`Error: unknown tool 'vgpoint'`。
- `vgagent tool vg_cfg_commit` → `rc=-1`，`Error: unknown tool 'vg_cfg_commit'`。
- C 层只对自有工具名放行（`vg_agent_tools.c:440-467`），其余一律 `ERROR`，由注册表报 unknown tool。
- shell 侧：构建 `.config` 为 `CONFIG_EXAMPLES_AI_AGENT_VELA_SHELL_ALLOWLIST=y`，`s_vg_readonly` 把 `vgpoint`、`vgdiscover` 收成"任何子命令都不允许"。

## 定时日报与告警产物

`/data/velaguard/reports/` 中存在：

| 文件 | 大小 | 说明 |
|---|---|---|
| `daily-2026-10-18.md` | 555 B | 首行 `AI-DAILY v1`，`date=2026-10-18`，`source=agent` |
| `alarm_advice.txt` | 1827 B | `VGADV1` 格式，8 条离线告警解释 |
| `daily-2026-10-17.md` | 543 B | 前一日的日报 |

日报由 `daily_maybe_request`（`vg_ui_backend_board.c`）自动发起，只有当日文件存在**且通过板端校验**时才停止重试。校验要求（`vg_ai_contract.c:1060` 起）为：首行 `AI-DAILY v1`、第二行 `date=<当日>`、第三行 `source=agent`、合法 UTF-8、无 CR/NUL、长度不超限。该文件全部满足，因此本日的定时日报确实落盘并通过校验。

`pending_alarm.txt` 内容格式正确：

```text
type=offline
tag=temp
slave=1
reg=0
value=0
threshold=0
hint=use alarm_interpretation skill
```

## 说明：串口日志编码

板端输出 UTF-8，PowerShell 按控制台代码页（CP936）回写，导致重定向日志变 GBK。已在 `scripts/serial_cmd.ps1` 固定 `[Console]::OutputEncoding` 为 UTF-8。本目录中 14:40 之前抓取的日志（如 `nsh-reports.log`）仍是 GBK，可用 `iconv -f GBK -t UTF-8` 还原，正文未损坏。
