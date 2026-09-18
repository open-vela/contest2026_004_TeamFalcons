# 执行记录：板端 LLM 凭证丢失与 AI 全链路降级

## 现象

告警页显示「AI 建议不可用，显示规则摘要」，报告页标题从 AI 日报变成
「本地运行报告 · runtime-report.md」，板端日志：

```text
[llm_router] No available backend
[agent] LLM call failed (iter 0)
[trace:...] iter=0 tool=(none) latency=0ms llm=fail backend=-1
[trace:...] END status=fail iters=1 tools=0 llm_ms=0 elapsed=0s backend=-1
```

同一时刻 `net_test` 到 www.baidu.com 是 TLS 握手成功、HTTP 200，所以不是断网。

## 根因

**板端 LLM 凭证从 eMMC 上丢失了。** 两个页面都是设计内的离线降级在正确工作，
缺陷在上游。

定位证据（COM3 实测）：

```text
mount                → /mnt/emmc type vfat       （/data 不是 tmpfs）
ls -l /data/velaguard/provision      → stat failed: 2      （目录不存在）
ls -l /data/agent/config             → 无 config.json
vgprovision status                   → vgprovision: not provisioned
vela> config_show                    → API Key : (not set)
vela> router_status                  → backend_count: 0, backends: []
```

`[llm_router] No available backend` 只在所有 backend 槽位都没有 host 时打印
（`llm_router.c:415`）。backend 列表完全来自 `/data/agent/config/config.json` 的
`llm_backend_0`（`llm_router.c:109-118`），没有编译期内置默认值。`llm_ms=0` 加
`backend=-1` 说明请求根本没走到网络：`llm_proxy.c:732` 在 api_key 为空时直接返回失败。

对比 09-16 同一块板：`vgprovision: OK` 加 `llm=ok backend=0`。所以这是丢失，不是没配过。

### 两个页面为何这样显示（都是正确行为）

- 告警页：没有 backend，建议轮次以 ERROR 结束，eMMC 上没有 `alarm_advice.txt`，
  覆盖判定为假，`vg_advice.c` 发布 `VG_UI_ADV_ERROR`，页面落到
  `vg_page_alarm.c` 的 ERROR 分支打印「AI 建议不可用」。
- 报告页：日报轮次失败，`daily-<日期>.md` 未生成，`agent_daily_load()` 失败，
  `board_read_latest_report()` 退回 `runtime-report.md` 并把 `from_agent` 留成 false，
  页面显示「本地运行报告 · runtime-report.md」。这是 V2 要求的离线降级。

## 恢复

```bash
bash scripts/provision-llm-from-secrets.sh COM3
```

`uid=09001a800451383430303835`，封存 226 字节 blob，`vgprovision: OK`。

### 恢复过程中发现的第二个缺陷：脚本假报成功

脚本最后发 `reboot`，但**板端没有 reboot 命令**：`CONFIG_BOARDCTL_RESET` 未开，
`app/nshlib/nsh_command.c:523` 那一项被条件编译掉，实测：

```text
nsh> reboot
nsh: reboot: command not found
```

于是脚本在 `.apply_on_boot` 从未被消费、`config.json` 从未写出的情况下打印
`[provision] OK`。复位后应用步骤才执行：

```text
ls -l /data/agent/config → config.json 已生成（414 字节）
```

### 恢复后的实测

```text
vgagent status    → llm: credentials=ready provision_file=present
config_show       → API Key : tp-c**** ; Model : mimo-v2.5
router_status     → backend_count: 1, status: ok, llm=ok backend=0
```

## 第三个缺陷：4KB 响应缓冲截断工具调用回复

凭证恢复后，`ask` 立刻成功，但建议轮次报 `[llm] Failed to parse API JSON`，
耗时 161 秒、3 轮迭代。

`llm_http_direct()`（`llm_proxy.c`）把 `AGENT_LLM_STREAM_BUF_SIZE` 直接交给
vela_tls，**不走** `resp_buf_append()` 的可增长路径；而 `vela_tls.h:60` 写明
「body 大于 resp_cap-1 时静默截断」，读循环在 `vela_tls.c:658` 停在 `resp_cap-1`。
本板 `CONFIG_VG_HMI` 把该值压到 **4KB**（`agent_config.h`），而一次工具调用轮次
的回复要把模型的 tool_arguments 整份带回来（实测请求体已达 18–21KB）。

修法：

- `agent_config.h` 新增 `AGENT_LLM_DIRECT_RESP_CAP (64 * 1024)`，只用于直接路径。
- `llm_proxy.c` 改用 `vela_https_request()` 并把 `out_body_len` 取回；长度落在
  `cap-1` 时判定截断，打印 `response truncated at N bytes` 后返回失败。
  原来调用 `vela_https_post_json()` 包装器会丢弃 body 长度，截断无法察觉。
- `vela_tls.c` 的 TLS 与明文两条读路径都在结尾检测「写满即截断」并 `LOG_ERR`。

依据：板端 `heap_info` 显示剩余堆 578MB（`fordblks=578251608`），64KB 余量充足。

## 验收

| 门 | 命令 | 结果 |
| --- | --- | --- |
| G1 | `make -C app/velaguard/host_tests test` | 15 个目标全绿，含新增 `test_provision_creds`（凭证就绪判定 7 例 + 存储持久性 6 例）与 `test_advice_policy` 的 4 条 NO_CRED 断言 |
| G1 反向 | 还原 `NO_CRED` 分支 / 还原存储守卫 | 分别如预期失败（got 2 want 3；procfs 与 tmpfs 两项失败） |
| G2 | `ctest --test-dir .debug/hmi-headless` | 5/5 通过，含新增 `12_alarm_ai_no_cred` 两条断言 |
| G3 | `bash scripts/build.sh` | exit=0，**0 warning**，`nuttx.hex` 3518611 字节 |
| G4 | `scripts/flash.ps1` 后板端 | `llm: credentials=ready provision_file=present`；凭证跨烧录与复位保持 |
| G5 | 真实 AI 请求 | `llm=ok backend=0`；日报 `AI-DAILY v1 / source=agent` 已生成 |

顺带修掉一个既有告警：`vg_provision.c` 的 `cred_to_json` 用 `snprintf` 拼凭据 JSON，
GCC 的 `-Wformat-truncation` 报可能截断（字段上限合计约 860 字节，而
`vg_provision_crypto_seal()` 把明文限制在 512 字节）。改为有界追加并在超限时返回
`-ENOSPC`，同时把 512 这个上限提到头文件作为 `VG_PROVISION_PLAIN_MAX`。
这条告警原先被增量编译掩盖，本次因为改到该文件才暴露。

## 仍未解决

### 1. eMMC 运行中继续丢文件

恢复当天的直接观测：`daily-2026-09-17.md`（964 字节）在 19:26 存在，
约 10 分钟后只剩 `runtime-report.md`，日报文件消失；`ls /mnt/emmc` 仍出现
重复的 `data/` 条目。固件里没有任何删除卷内容的代码（无 mkfatfs、挂载不带
format），所以这是 FAT 目录项被破坏的迹象，与 09-15 那次 eMMC 运行期失效同源。

`CONFIG_STM32H7_SDMMC_IDMA` 仍未开，SDMMC 走 PIO，驱动自带警告
`Large Non-DMA transfer may result in RX overrun failures`。

**这意味着本次恢复的凭证也可能再次丢失。** 新增的防护把定位成本从「按告警页
线索排查」降到一条 `vgagent status`：`credentials=MISSING`。

### 2. 建议产物产出率

`alarm_advice.txt` 能写出完整且合规的 VGADV1（上一轮启动留下 1982 字节、8 点、
真实分析），但当前这轮启动还没产出。观测到的循环是：

```text
[vgadvice] asked for 8 alarms req=3
[vgagent] round submitted: 1311 bytes
[vgagent] round finished: reply
[vgadvice] advice from another boot: file=30ba8d31 board=30baf1c8
```

最后一行是板子在正确拒收上一轮启动留下的文档（`boot` 不匹配）。轮次确实跑起来了、
模型也在调工具（`read_file alarm_interpretation.md`、`2 tool calls`），但没有走到
`write_file` 就结束了。

两个叠加因素：单次 MiMo 调用 50–86 秒，而一轮 ReAct 要把增长中的上下文重发多次
（`.trellis/spec/backend/ai-text-contract.md` 已记录「轮次开销按迭代次数算」）；
`VG_AGENT_ROUND_TIMEOUT_MS` 是 300 秒，慢链路上留给写文件的时间很紧。

这属于 Skill 与请求侧的产出率问题，不是本次修复的范围：本次只保证已经产出的建议
不再被藏起来，以及凭证问题不再伪装成建议功能故障。要提升首轮产出率需要单独调整
skill 步骤数或轮次超时，建议单独立项。

### 3. 86400 环境项

`net_test` 到 baidu 成功不代表板端能连上 MiMo：主机把
`token-plan-cn.xiaomimimo.com` 解析到 `198.18.0.197`（代理假 IP 段）。本次实测
板端直连 MiMo 成功，所以当前环境可用；换网络后需重新确认。

**2026-09-17 晚补充（修正一条此前的误判）**：当天先观测到 `[vela_tls] net_connect
... ret=0x42`，当时把它归因为「假 IP 无路由」。这个归因是错的：同一天稍后同样解析到
假 IP（`198.18.0.193` / `198.18.0.63`）的板子能正常完成 TLS 握手并跑通轮次。假 IP
段 `198.18.0.0/15` 是 mihomo/Clash 的 fake-ip 池，Windows 上的 Clash 会把发往该地址的
流量转发到真实目标，所以板子经它上网是正常的。

真实原因是**又一次凭证丢失**（与本节主体同一个故障）：`/data/agent/config/config.json`
变成 **0 字节**，`mount` 显示 `/mnt/emmc type vfat`、`vgprovision status` 为 OK、
226 字节 blob 也在，只有写出的 config.json 是空的。空文件让 router 得到空的
`llm_backend_0`，于是每一轮都是 `llm=ok`→`llm=fail` 之前的那种「无 backend」失败。
重新 `commit` 并复位后恢复：

```text
config.json 0 字节 → 414 字节
vgagent status → llm: credentials=ready provision_file=present
llm=ok backend=0，TLS Handshake OK
```

两点值得记下来：

- 该故障会表现为**间歇**：凭证在时轮次正常，丢失后同一域名、同一网络立刻失败，
  所以「域名解析异常」和「凭证丢失」必须用 `vgagent status` 的
  `credentials=` 区分，不要从 `net_connect` 的返回码反推。
- `config.json` 被截成 0 字节而不是消失，说明写入本身被打断（`vg_provision.c:546`
  用 `O_TRUNC` 打开再 `write`，不是「写临时文件 + rename」），这与本节「eMMC 运行中
  继续丢文件」是同源现象，本次未修。


### 4. COM 口占用

COM4 实测可打开，未占用；真正的阻塞是 `mbslave.exe`（Modbus Slave）10 分钟试用
到期后主动断开并在弹窗等待。COM3 当时被 `scomm5.15.exe`（sscom）独占，已关闭以
执行烧写。
