# 修复：告警页有建议却显示「AI 建议不可用」

## 现象

告警页选中一个活动告警点，详情区头部是「AI 建议不可用，显示规则摘要」，正文是本地规则摘要。行内没有 `AI · ` 那一行。板端此前确实写出过合规的 `alarm_advice.txt`（`VGADV1`、`boot`+`req`+8 条齐全）。

## 根因

页面只在 `vg_ui_alarm_advice_state() == VG_UI_ADV_ERROR` 时打印这句话，所以现象等价于 `g_state` 落在 ERROR 且 `vg_ui_alarm_advice_get()` 未命中。而旧实现的命中条件把轮次状态也算了进去：

```c
if (g_loaded && g_state == VG_UI_ADV_READY)   /* vg_advice.c 旧 :485 */
```

于是任何一个后续失败都会屏蔽仍然有效的建议。板端实测确认了最后一环：磁盘上的 `alarm_advice.txt` 写着 `boot=30bd52a6 req=2`，8 条点的 `id`/`epoch` 与当时告警完全一致，而当前轮次是 `req=3`，`vg_ai_advice_parse` 因为 `req` 差一返回 `VG_AI_ERR_STALE` 整份拒绝（`[vgadvice] advice rejected rc=-6`），页面落到规则摘要。**建议就在盘上、身份也对，只被轮次序号挡在门外。**

五条判定错误：

1. **命中挂了轮次状态**。`advice_load()` 成功时保留 `g_doc`，但任何一轮失败都会把状态置成 ERROR（旧 `:202 :211 :230 :334 :404 :428`），已通过校验、`boot`+`epoch` 对得上的建议立刻读不出来。这直接违背归档设计里「保留上一份文档，避免页面闪回」的意图。
2. **把 `req` 当身份门槛**。文档的身份是 `boot` 加逐条 `(id, epoch)`；`req` 只说明它是哪一轮写出来的，与是否仍然适用无关。一轮超时后迟到的产物在下一轮被读到，`req` 必然差一，却因为身份完全对得上而被整份拒绝。改为只校验 `boot`，`req` 降为诊断信息。
3. **把 `-EBUSY` 当错误**。`advice_set_state(VG_UI_ADV_ERROR)` 在 `-EBUSY` 分支上也是无条件执行的，注释却写着它只是通道被占一个 tick。日报轮在途时页面就会闪成不可用。
4. **重问门槛用错变量**。退避只在 `!g_loaded` 时装载，提前返回用 `g_loaded` 而不是「文档是否覆盖当前告警集合」。新告警轮次那一轮失败后，`changed` 变假、`g_loaded` 为真，于是既不退避也不重问，页面白挂 `VG_ADV_REFRESH_MS`（5 分钟）。
5. **刷新判定无符号下溢**。`now` 在 tick 开头取得，而 `advice_load()` 在其之后把 `g_last_ok_ms` 设为更晚的读数，`now - g_last_ok_ms` 在无符号下变成约 2^32，每次 tick 都判为「已过期」，于是一轮接一轮重问（板端实测 `req` 每约 30 秒自增一次）。这一条是修复第 2 条之后新暴露出来的，README 记在下面。

触发源（板端日志实证）是模型侧偶发行为：一轮只回文字没调 `write_file`，或一轮在写文件后被判超时。触发源本身不是这次的修复对象。

## 改动

| 内容 | 位置 |
|---|---|
| 命中、覆盖、重问、刷新判定、队列分类五条规则 | 新增 `app/velaguard/vg_advice_policy.{h,c}`（纯 libc） |
| 只读身份头解析 `vg_ai_advice_head()` | `app/velaguard/vg_ai_contract.{h,c}` |
| 状态语义与调用点 | `app/velaguard/vg_advice.c` |
| 只读探针 `vgagent advice` | `app/velaguard/vgagent.c`、`vg_advice.h` |
| 规则回归用例（含刷新下溢）与身份头用例 | `app/velaguard/host_tests/test_advice_policy.c`、`test_ai_contract.c` |
| 页面断言（含此前零覆盖的两个降级分支） | `gui/headless/alarm_check_main.c` |
| mock 与板端同规则、可注入 ERROR/PENDING | `gui/main/ui/model/vg_ui_backend.{h,c}` |
| 板端断言与串口探针 | `scripts/stage1_lvgl_hmi_accept.ps1`、`scripts/vg_probe_com3.ps1`、`scripts/vg_watch_advice.ps1` |
| 规则正文 | `.trellis/spec/backend/ai-text-contract.md` |

页面代码与文案没有改动：它本来就是先查命中、再按状态选措辞，命中逻辑修对后自然显示 AI 建议。

## 验收

| 门 | 命令 | 结果 |
|---|---|---|
| G1 | `make -C app/velaguard/host_tests test` | 14 个目标全绿，含新增 `test_advice_policy`（命中不看状态、身份、覆盖、轮次后状态、重问门、刷新窗口、状态不变量、队列分类）与 `test_ai_contract` 的新身份头用例 |
| G2 | `ctest --test-dir .debug/hmi-headless` | 5/5 通过，含 7 条新增建议断言 |
| G3 | `bash scripts/build.sh` | exit 0，0 error 0 warning，`.debug/nuttx.hex` 3515641 字节 |
| G4 | `scripts/flash.ps1` + `scripts/stage1_lvgl_hmi_accept.ps1` | pass=18 fail=0 |
| G5 | `scripts/stage1_agent_ops_accept.ps1` | 见下 |

### 板端实测（G4，2026-09-17）

一块 H750B-DK，8 条离线告警活跃时的建议缓存采样：

```
13:51  hits=0 miss=8  state=error loaded=0 req=1  covered=0   ← 告警齐了，建议还没产出
13:56  hits=8 miss=0  state=ready loaded=1 req=2  covered=1   ← 建议装载，8 点全部命中
13:58  hits=8         state=ready              req=2          ← 稳定
```

`req` 在三个采样点保持 2，页面读到的就是 AI 建议而不是规则摘要。修复前同一场景的表现是：`req` 每约 30 秒自增一次（无符号下溢导致每 tick 重问），且一旦某轮失败 `hits` 全部归零。

另一次现场恰好暴露了修复的价值：`acu_set`/`acu_run` 的 `al_epoch` 递增到 2 而缓存里还是 1，`covered=0`，但另外 6 个 epoch 对得上的点仍然 `advice=hit`。修复前这会让整页降级为规则摘要。

### G5 与环境阻塞（未获通过，需说明）

`stage1_agent_ops_accept.ps1` 在本轮未稳定通过，失败原因是环境而非本改动：

- 日报轮**确实会发起**（`vghmi: daily report requested`、`round submitted: 124 bytes`），随后 LLM 调用失败。
- 用完全不经过 `vg_advice` 的手工 `vgagent ask` 复现同一失败：`[llm] LLM call failed on backend 0`、`[llm_router] All backends backed off`、`END status=fail`、`llm_ms=60`。60 ms 即失败说明连接没建立。
- 板端 `nslookup token-plan-cn.xiaomimimo.com` 返回 `198.18.0.197`，本机（Windows 宿主机）用系统 DNS 与 223.5.5.5、8.8.8.8 查询**同样返回该地址**。`198.18.0.0/15` 是代理软件（本机装有 Clash Party）在 TUN 模式下常用的 fake-IP 段，板端经 ICS 共享继承了这台机器的 DNS。真实解析结果不可得，板端因此连不上 MiMo。
- 后续一次运行中日报**成功重建**并通过全部内容断言（`AI-DAILY v1`、`source=agent`、日期行），`advice document is terminated by END` 也由失败转为通过；同一次运行开头另有 5 项 `ps`/启动横幅断言失败，但同轮 agent 实际完成了日报与建议，属重置后 45 秒窗口内的启动时序抖动。

结论：本次改动相关的断言（日报重建、建议文档、END、归属、审计日志）在环境允许时通过；建议侧的行为已由 G4 板端采样与 G1/G2 覆盖。要在本机完成 G5，需要先让板端能解析 MiMo 域名（关闭代理的 fake-IP，或让路由器的 DNS 直接解析）。

## 遗留

这次只保证「已经产出的建议不会被藏起来」，不提高模型侧的产出率。一轮没写文件时，该告警集合仍要等一个 `VG_ADV_RETRY_MS`（300 s）退避周期才重问，这期间页面显示的是「不可用」加规则摘要。若板测发现首轮产出率仍偏低，再单独处理 skill 与请求侧。

另外，为验证单主站约束下用 PC 从站触发阈值告警，本次在 COM4 上起过一个 Modbus RTU 应答器（只应答、不发请求）。Windows 侧残留的进程（PID 26136）被安全软件保护、`Stop-Process`、WMI `Terminate`、`taskkill /F /T` 与设备禁用全部被拒，COM4 因此被占。该进程与仓库无关，下次要用 COM4 前需先在 Windows 上结束它。
