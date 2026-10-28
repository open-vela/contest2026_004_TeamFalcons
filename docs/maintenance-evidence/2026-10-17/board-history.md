# 2026-10-17 板测过程记录

以下保留接续任务当时的观察与推断。当前状态只看 `docs/maintenance-todo.md`；本次复核见 `../2026-10-17-review/review.md`。

复核说明：2–3ms 接收盲区没有硬件时序测量证据，延迟回复改善成功率不能证明所有物理问题已排除。两次重启的原因尚未确认。150ms 从站测试不等同于快速从站验收；warn 和模拟器静默不等同于 crit 与物理拔线；手动 ask 返回报告不等同于定时日报落盘。长测实际日志起始为 16:40:30，尚未完成。

### 板端进展（2026-10-17 13:00 起，COM3/COM4 已接）

以下为真机观察，原始输出在上述证据目录：

- 启动崩溃修复已板验：`CONFIG_INIT_STACKSIZE=8192` 重烧后正常启动，启动任务刷新设备 Skill 不再 HardFault（flash-child-status.log）。
- LCD G0/G1 引脚修复（PG0/PG1 改 PJ7/PJ8）后，内存统计自洽：total=7,014,016、free 约 6.65MB，堆尾哨兵完整；`vgstoretest` 50 轮写读备份比较两次通过。历史 SDRAM 别名问题与此吻合，但长期证据继续由 soak 累积。
- `SCHED_CHILD_STATUS` 等配置补齐后，板端 Agent shell 工具成功执行只读 `vgcfg dump`，禁用的 `vgpoint` 仍被拒绝（shell-child-status-retry.log）。
- 共地修复前的过渡窗口内，板端真实产生离线告警 raise/clear 事件（`modbus_fail_offline`、`offline_recovered`，见 soak-smoke-console.log），滑窗判定在真机上工作；真实拔线与阈值注入仍待做。
- 485 链路：人工修复共地后，RTU 从站模拟累计 9031 请求、9031 回复、0 CRC 错误（rtu-slave-status.log）。板端采集在线数恢复情况待 COM3 空闲后复查（13:14 冒烟时 online=3，为共地修复前数据）。
- MiMo：PC 侧域名已解析到真实 IP 且 HTTPS 可达，DNS 例外生效；板端 `nslookup` 与 LLM 对话待 COM3 空闲后复验。
- `scripts/maintenance_soak.ps1`：语法解析通过，冒烟 1 轮通过；24 小时长跑尚未开始。
- COM3 于 14:50 起被串口工具占用，板端复查暂停，等待释放。

### 板端进展二（2026-10-17 15:00-15:45，网络恢复与采集排查）

- 板子 14:28 与 15:20 前后各重启一次，均与现场接线/拔插时段吻合，疑似供电抖动而非固件崩溃；24 小时验收要求零非预期复位，期间须避免动板端供电与接线，重测时继续观察。
- 板端断网根因：主机侧连接板子的有线网卡被禁用（Realtek Code 22），后经用户换接 USB 网卡恢复；ICS 192.168.137.1 在新网卡上生效，板端 DHCP、出口 ping、DNS 全部恢复。MiMo 域名板端解析到真实 IP（220.181.104.191），TLS 握手与 LLM 对话端到端通过（问 1+1=2 答 2），时间同步恢复。
- COM4 反复被占的机制：scomm 串口工具会对重新插拔的串口自动重连；PnP 重启 MacroSilicon USB 串口设备可释放。已按用户先前授权结束 scomm 进程。
- 采集 ok 率仍低（12%-16%，超时为主）且与从站实现无关：PowerShell 从站与 Modbus Slave 原生从站结果一致，多数成功来自第二次尝试（延迟 300-800ms，即第一次读超时后由重试收割迟到回复）；单次新鲜扫描会话可稳定发现从站。板端驱动历史记录（docs/velaguard-bringup-known-issues.md 第4节）已确认：LA 证明板端 DI/DIR 时序正确后，A/B 乱码归因于 USB-RS485 收发器，换适配器后误码大幅下降。当前 COM4 适配器为 MacroSilicon MS2109 方案，列为头号嫌疑。
- 待办：换用可靠 USB-RS485 适配器后复测采集 ok 率与告警注入；`VGMODBUS_BYTE_TO_MS=20` 的字节间隔超时对 USB 虚拟从站偏紧，若换适配器后仍复现再评估放宽。

### 板端进展三（2026-10-17 16:05-16:45，采集根因定位与验收注入）

- **采集 ok 率根因定位**：裸收发与 30ms 换向测试 6/6、3/3 全过，适配器与板端收发器均排除。真正根因是板端发送后的接收盲区（约 2-3ms：`uart_tcdrain` 以 `POLL_DELAY_USEC=1ms` 轮询 `up_txempty`，加上 400µs DIR 保持、500µs settle 和 `tcflush(TCIFLUSH)`）：快从站回复（Modbus Slave 约 2-8ms）落进盲区整帧丢失，慢从站回复（旧 PS 从站 300-700ms）超出 500ms 读超时，第二次尝试只能收割迟到帧，两种从站都表现为约 90% 首试失败。验证与缓解：PS 从站新增 `-ReplyDelayMs`（默认 150ms）落在盲区后、读超时前，板端 `vgmodbus` 换向测试与采集随即 100% 通过（重启后 bus 290/290，warn 注入期间 1311/1311 全 ok）。
- **遗留固件项（新）**：盲区对响应快于约 3ms 的真实从站仍会丢帧。后续修复方向：去掉写后 `tcflush(TCIFLUSH)`（它在 DIR 落地后还会吞掉早到回复）、评估在 `uart_tcdrain` 等待 TC 时缩短轮询或由驱动 TC 中断直接通知；涉及公共 NuttX 仓改动按 C2 走 fork+PR。
- **告警注入全部通过**：warn（temp=30 越 warn=28 ge）raise、恢复后 threshold_cleared、slave 静默后 14 点离线 raise、恢复后 offline_recovered。advice 轮次由真实告警触发，agent 经只读工具取数并产出 `VGADV1` 解释（引用实测 30.0、阈值 28、通信成功率 100%），写入 `/data/velaguard/reports/alarm_advice.txt` 并挂接告警集。
- **日报与带工具 ask 通过**：`vgagent ask` 触发 ReAct 循环，读取设备 Skill `operations_report.md`、调用 `vg_run_report`，生成中文日报（2 轮迭代、88.7s LLM）。
- **运维教训（新增）**：flat 构建下 LVGL 初始化标志为全局状态，`kill vghmi` 后新实例会因 `lv_is_initialized()` 静默退出，NSH 无 reboot 命令；恢复手段是 `.debug/nsh_reset_only.ps1` SWD 硬复位。后续如需停采集，优先设计运行时开关，不走 kill。
- **24 小时 soak 已启动**（16:43，`-MinimumOnline 14 -IntervalSec 120`），首轮 CYCLE 1 通过（online=14，内存 total 7,014,016 / used 约 318KB）；从站以 30 小时寿命覆盖 soak 窗口。发现故障自动停止，不自动复位。期间 COM3 由 soak 独占，避免动板端供电与接线。
