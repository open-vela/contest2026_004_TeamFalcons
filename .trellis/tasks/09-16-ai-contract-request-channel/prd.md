# AI 文本契约与请求通道

## Goal

为告警页逐点建议与 Agent 日报提供共同基础：一个可在 host 上做边界测试的纯 C 契约模块，一条板端主动发起 Agent 轮次的通道，以及把 Agent 只读边界落到 C 实现的加固。

## Requirements

- **R1** 新增 `app/velaguard/vg_ai_contract.{h,c}`，不依赖 LVGL、cJSON 与 NuttX 头，可被 `app/velaguard/host_tests` 直接编译。
- **R2** 提供 `VGADV1` 行式文本的严格解析：任何格式错误、越界字段、未知键、重复条目、计数不符、身份不匹配、超长、非法字符都必须整份拒绝，且不部分写入输出结构。
- **R3** 提供告警建议请求文本的纯构造函数，无 IO、无副作用。
- **R4** 提供 Agent 日报的校验函数（标记行、日期、长度、mtime 新鲜度、UTF-8 合法性）。
- **R5** 提供板端主动发起 Agent 轮次的通道，具备单在途串行化、pending 槽、超时上限，且不阻塞 UI 线程。
- **R6** Agent 看不到的状态变更入口在 C 层封堵：`vgstats` 只允许 `dump`，`vgnet` 只允许 `status`，`vgruntime` 只允许 `dump`。
- **R7** 固件的 `runtime-report.md` 在 C 层设为 Agent 不可写。
- **R8** 工具调用审计落盘，脱敏后记录时间、工具名、参数前缀与结果状态。
- **R9** Gate 0：板上完成一轮真实 `ask` 并留下证据。

## 非目标

- 不改告警页与报告页的界面（属于子任务 B、C）。
- 不打开 `heartbeat_send()` 在 `CONFIG_VG_HMI` 下的提前返回；主动轮次统一由板端 file worker 发起，避免两个不同步的触发源互相打架。
- 不预防性重构 09-16 任务列出的那批同类偏移累加文件；只有验收中真的崩进去才按证据修那一个点。

## Acceptance Criteria

- [ ] `make -C app/velaguard/host_tests test` 全绿，含新增 `test_ai_contract`。
- [ ] 边界用例全部通过：3 条与 8 条正常、9 条拒绝、`sum` 恰好 80 B 完整保留且 79 B 加一个 3 字节汉字时该汉字整体丢弃（不劈开 UTF-8）、整份 2560 B 边界、缺 `END`、尾部垃圾、未知键、非法 `id`（`..`、空格、斜杠、24 字符）、同一 `id` 重复、`boot`/`req` 不匹配、控制符与非法 UTF-8、空缓冲与 `len == 0` 不崩、文档 epoch 2 查 epoch 3 不命中、全 `sev=offline` 的构造与解析、构造器遇 `NaN`/`Inf` 输出 `-`。
- [ ] Gate 0 证据落盘：中文回答、至少一行 `Executing tool: `、`END status=ok iters=N tools=M`；未通过时明确记录并停下功能 2。
- [ ] 板端请求通道在途串行化与超时经过板测验证，超时后状态回到可重试。
- [ ] 只读加固后，板上 Agent 执行 `vgstats inject`、`vgnet inject`、`vgruntime report <path>` 均被拒绝，`dump` / `status` 正常。
- [ ] 公共树改动在 `velaguard/*` 分支上，未使用任何 patch，并准备了 fork + PR 的提交材料。

## Notes

- 本子任务是 B、C 的共同前置，独立可验收。
- 详细设计见 `design.md`，执行顺序见 `implement.md`。
