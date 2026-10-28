# COM4（RS485）被占用，采集与告警注入缺少条件

记录日期：2026-10-18。这是环境阻塞，不是固件缺陷。

## 现象

- COM3（STMicroelectronics STLink Virtual COM Port）可正常打开，NSH 全部命令可用。
- COM4（MacroSilicon USB Serial Ports，VID_345F&PID_3020，SN A02001FJ）**始终返回 access denied**。

```text
ports: COM1,COM3,COM4
COM3 OPEN-OK
COM4 BUSY: 使用“0”个参数调用“Open”时发生异常:“对端口“COM4”的访问被拒绝。”
```

用 Win32 `CreateFile` 直接打开 `\\.\COM4` 得到 `win32err=5`（ERROR_ACCESS_DENIED），确认是独占句柄占用，不是路径或权限问题。

## 已尝试且无效的释放手段

| 手段 | 结果 |
|---|---|
| 用户手动重新插拔 USB（多次，含 14:41、14:48 前后） | 仍 denied |
| `Disable-PnpDevice` + `Enable-PnpDevice`（管理员） | 设备重启成功，仍 denied |
| `pnputil /restart-device`（管理员） | 设备重启成功，仍 denied |
| `pnputil /remove-device` + `/scan-devices`（管理员） | 设备被删除并重新枚举，仍 denied |
| 30 秒内 60 次轮询打开 | `open-ok=0 busy=60`，无任何窗口可用 |

## 已排除的原因

- **不是设备故障**：`DEVPKEY_Device_ProblemCode = 0`，`CM_PROB_NONE`，`This device is working properly`；驱动 `MacroSilicon 1.0.16.35`，服务 `msusb2serial`，类 `Ports`。
- **不是端口号冲突**：只有一块 MacroSilicon 适配器。
- **不是 RDP 串口重定向**：`query session` 只有 `services`（断开）与 `console`（本机活动），没有远程会话。
- **不是我们自己的残留进程**：管理员权限枚举全部进程，没有 maintenance/slave/soak 相关的 powershell 或 python 残留；没有串口助手类进程。
- **不是权限不足**：以管理员身份打开同样 denied。

## 未定位的部分

占用进程未能确认。本机没有 Sysinternals `handle.exe`；自行编写的跨进程句柄枚举脚本被自动权限分类器以安全理由拦截，未继续尝试绕过。

一个尚未证实的观察：机器上存在 Codex 的 computer-use 运行时（`codex-computer-use-swift.exe` 及多个长期存活的 `node.exe` / `node_repl.exe`，位于 `C:\Users\19y\AppData\Local\OpenAI\Codex\runtimes\cua_node\`）。这些进程没有串口相关的命令行参数，无法据此认定占用，但如果要排查，退出 Codex 后重新探测是一个成本很低的实验。

## 影响范围

以下验收项因缺少 RS485 从站而无法开始，报告中记为**条件不足**：

- 阶段 C：快速从站下的采集正确性、发送后早到回复保留、BUS-03 的 0/5/30/150ms 各延迟工况。
- 阶段 D 的 `vgpoint test` 部分（试读需要真实总线）。
- 阶段 F：warn / crit 阈值注入、单从站与全部静默、恢复、`ge/le/eq` 覆盖、物理拔线。
- 阶段 H：新固件 24 小时长测（`maintenance_soak.ps1` 要求 `-MinimumOnline`，无从站时在线数恒为 0）。

不受影响、已在 COM3 上完成的项目见 `board-baseline.md`。

## 恢复条件

释放 COM4 独占句柄即可。可选路径：关闭持有串口的工具；或重启 Windows 清空句柄；或允许运行句柄枚举以先确认持有者。
