# 验收手册：ai_agent 点位查询与运行报告

对应任务：`.trellis/tasks/09-17-agent-live-query-tools/`。
本文件给执行验收的人看，不重复协议表（协议见 `docs/velaguard-host-nsh-protocol.md` 第 8.2 节）。

## 验收什么

| 编号 | 断言 | 由哪一步覆盖 |
|---|---|---|
| A1 | `ask 告诉我UPS负载的值` 返回真实工程值，与 `vgpoint get ups_load` 一致 | 第三档 |
| A2 | `ask 给我截止目前的运行报告` 返回板上三节报告 | 第三档 |
| A3 | 模型**自己**选择正确工具（不是靠提示词写死） | 第三档 |
| A4 | 中文点名能解析成点位 id，歧义时列候选不猜 | 第一档 |
| A5 | 取不到数据时如实说明，不印数字 | 第一档 |
| A6 | 报告三节齐全且声明数字是实测 | 第一档 |
| A7 | 两个工具都只读：不写 `/data/velaguard/reports/`，不打开 RS485 | 第一、三档 |
| A8 | `vgpoint` 对 Agent 仍然不可达 | 第一、三档 |

## 前置条件

```text
1. COM3 空闲（关掉 sscom / Cursor 串口监视器）
2. 模型后端可用：nsh> vgagent status  →  llm: credentials=ready
3. 台架接了从站（可选但强烈建议）：nsh> vgpoint get ups_load  →  value=<数字> ok=1
```

第 3 条决定 A1 是**逐值比对**还是**降级断言**：

- 有从站（`ok=1`）：脚本把 Agent 报出的数字与 `vgpoint get` 的读数按容差比对，
  能抓住「未按 scale 换算」这类错误（`ups_load` 是 `scale 0.1`，未换算是 10 倍）。
- 没从站（`ok=0`，value 为 `-`）：脚本转为断言工具报
  `reason=read_failed|no_sample`，即**如实说没有数据**。这仍然有意义，但它不验证数值换算。

`llm: credentials=MISSING` 说明板子丢了凭证，先按
`scripts/provision-llm-from-secrets.sh` 重新 provision 并复位，否则第三档全跳过。

### 关于代理环境（2026-09-17 实测）

这台开发机在跑 Clash/mihomo，`198.18.0.0/15` 是它的 fake-ip 池，板子解析
`token-plan-cn.xiaomimimo.com` 会拿到 `198.18.x.x` 而不是真实地址。实测结论：

```text
板端 ping 真实地址 220.181.104.192   → 0% loss        （路由没问题）
板端 ping fake IP  198.18.1.41       → 0% loss        （Clash 的 TUN 会应答 ICMP）
板端 TCP 443 到该主机                 → [vela_tls] net_connect ret=0x42
主机 curl 同一域名                    → http=401        （主机侧链路是好的）
```

也就是说 ICMP 能通、TCP 443 不通，**问题出在 Clash 转发的 TCP 这一段，不在板子**。
表现为板端每轮 `llm=fail backend=0`、耗时只有几十毫秒（不是超时），控制台回
`Sorry, I encountered an error.`。

遇到这种情况不要改板子配置，也不要把凭证反复重灌：先用上面的 `curl` 确认主机侧
能通，再检查 Clash 的规则/节点是否把该域名走了可用的出口。第三档验收会自己识别
这个窗口并把相关断言报成 `[INFO]` 而不是 `[FAIL]`。

---

## 第一档：工具层（约 2 分钟，不需要模型）

这一档证明工具本身好使，与模型后端是否可用无关。

```bash
cd /home/hello19y/openvela/contest2026_004_TeamFalcons
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/vg_agent_tools_probe.ps1
```

期望末行 `[vg_agent_tools_probe] pass=18 fail=0`，退出码 0。

覆盖 A4 / A5 / A6 / A7 / A8。关键行示例：

```text
vgagent tool vg_point_read UPS负载   → rc=0  id=ups_load        （中文名解析）
vgagent tool vg_point_read UPS      → rc=0  ambiguous n=3      （列候选，不猜）
vgagent tool vg_point_read 不存在的点位 → rc=0  reason=not_found
vgagent tool vg_run_report {}       → rc=0  通信质量/点位在线/异常时间线
vgagent tool no_such_tool_xyz {}    → rc=-1                    （反例：必须失败）
```

## 第二档：不做任何事

只想确认工具对模型可见时：

```bash
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/serial_cmd.ps1 `
  -Commands "vgagent tools"
```

期望出现 `vg_point_read` 与 `vg_run_report` 两行定义。注册表会静默丢弃 JSON
不成立的 provider，从外面看和「模型没调用」一样，所以要看字符串本身。

---

## 第三档：完整验收（约 15–25 分钟，需要模型可用）

```bash
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/stage1_agent_ops_accept.ps1
```

期望末行 `[stage1_agent_ops_accept] pass=N fail=0`（N 随台架状态在 30–40 之间），退出码 0。

覆盖 A1 / A2 / A3 / A7 / A8，另外顺带验证日报、告警建议、审计日志等既有流程没被破坏。

### 这一档的结果怎么读

脚本会自己判断模型后端能不能用（真发一轮 `ask 你好` 探测），据此决定是断言还是报告：

| 输出 | 含义 |
|---|---|
| `[INFO] model backend reachable` + 断言通过 | 模型链路通，A1–A3 都已验证 |
| `[INFO] model backend unreachable ... skipping` | 模型链路不通，**A1–A3 未验证**，不是通过；先修凭证或网络再跑 |
| `[FAIL] the model itself called vg_point_read ...` | 模型通了但没调工具，这是真实缺陷 |
| `[INFO] no live reference value on this bench` | 没接从站，A1 降级为「如实报无数据」 |

一次典型通过的关键行：

```text
[PASS] the model itself called vg_point_read for the point question
[PASS] vg_point_read returns the same value as vgpoint get
[PASS] reports directory was not changed by the read-only path
```

### 想单独看两句话的实际回答

```bash
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/serial_ask_probe.ps1 `
  -Preset point -ReadSec 240 -Log .\ask_point.txt

powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/serial_ask_probe.ps1 `
  -Preset runreport -ReadSec 240 -Log .\ask_report.txt
```

`-Log` 的路径是 **Windows 侧**路径，写 `.\ask_point.txt` 或 `C:\...`；
传 `\tmp\...` 这种不存在的目录会报 DirectoryNotFound。

---

## 手工最小验收（脚本不可用时）

在 COM3 的串口终端里按顺序敲：

```text
nsh> vgagent status
     期望 llm: credentials=ready

nsh> vgpoint get ups_load
     记下 value，作为下面比对的参考

nsh> vgagent tool vg_point_read ups_load
     期望 rc=0，且 value 与上一条一致、unit=%

nsh> vgagent tool vg_run_report {}
     期望 rc=0，正文含 通信质量 / 点位在线 / 异常时间线

nsh> ai_agent
vela> ask 告诉我UPS负载的值
     期望用的是 vg_point_read，回答里的数字与 vgpoint get 一致
vela> ask 给我截止目前的运行报告
     期望用的是 vg_run_report，复述三节内容
vela> quit

nsh> ls /data/velaguard/reports
     期望与问之前一致：两次提问不写文件
```

**注意 `vela>`**：`ai_agent` 附着后提示符变成 `vela>`，此时敲 NSH 命令会回
`Unknown command`。问完必须 `quit` 回到 `nsh>` 再继续。

---

## 常见坑

| 现象 | 原因 | 处理 |
|---|---|---|
| `Unknown command: vgagent` | 控制台在 `vela>` | 先 `quit` |
| `COM3 ... 访问被拒绝` | 串口被占 | 关掉 sscom / Cursor 监视器；确认没有残留的 `powershell.exe` 抓着口 |
| 每轮 `llm=fail`、`Sorry, I encountered an error.` | 凭证丢失或后端不可达 | `vgagent status` 看 `credentials=`；MISSING 就重新 provision + 复位 |
| 回答里的数值是 10 倍 | 该路径没按 `scale` 换算 | 这是本功能要防的回归，应记为缺陷 |
| 报告页显示 `runtime-report.md` 而非 AI 日报 | 日报轮次没成功 | 与本次两个工具无关，属日报流程 |

## 实测记录（2026-09-17，本机）

| 命令 | 条件 | 结果 |
|---|---|---|
| `make -C app/velaguard/host_tests test` | 主机 | 17 个目标全绿 |
| `vg_agent_tools_probe.ps1` | 板上，模型无关 | **pass=18 fail=0**（反复可复现） |
| `stage1_agent_ops_accept.ps1` | 板上，模型可用 | **pass=37 fail=0** |
| `stage1_agent_ops_accept.ps1` | 板上，模型在 ask 窗口掉线 | `[INFO]` 跳过 A1–A3，其余仍断言 |
| `stage1_agent_ops_accept.ps1` | 板上，模型可用 + 接了从站 | **pass=36 fail=1**（该 FAIL 是模型在 ask 期间掉线；现已改为 `[INFO]` 区分） |

从站接上后（`mbslave` 喂 7 个从站，`vgstats` 全 `ok`），`vgpoint get ups_load`
返回 `value=134.4 ok=1`，此时 A1 是真正的逐值比对，已通过一次：
`[PASS] vg_point_read returns the same value as vgpoint get`。

模型自选工具的干净证据（审计日志在 ask 前清空）：

```text
548.030 tool=vg_point_read rc=0 args={"query":"ups_load"}
953.160 tool=vg_run_report rc=0 args={}
```

## 结果怎么报

按 `命令 / pass-fail / 关键日志一行` 报：

```text
vg_agent_tools_probe.ps1        pass=18 fail=0
stage1_agent_ops_accept.ps1     pass=37 fail=0   （model backend reachable）
关键行: 548.030 tool=vg_point_read rc=0 args={"query":"ups_load"}
```

