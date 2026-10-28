# 04: boot stub 安装暂存镜像到 B 并 trial 启动

**What to build:** 当 BCB 为 pending 时，boot stub 验证 BCB 与 manifest 的绑定、镜像大小和 SHA-256，擦除并编程 B 槽，回读摘要，写入 trial BCB，然后跳转 B 槽。镜像越界、摘要不匹配、QSPI 擦写失败或断电时回到 A 槽。此票只允许写 B 槽，A 槽保持不变。

**Blocked by:** 03。

**Status:** in-progress

- [ ] pending BCB 绑定有效 manifest 和镜像时，boot stub 完成 B 槽擦写、回读 SHA-256 并启动 B。
- [x] BCB 与 manifest 不一致、镜像超出槽位、摘要不匹配时拒绝安装并回到 A。
- [ ] QSPI 擦除或编程失败、回读失败、写入过程断电时回到 A，不跳转无效地址。
- [ ] 安装过程只写 B 槽，A 槽内容和 A 槽启动能力保持不变。
- [x] trial BCB 在跳转 B 前写入，后续启动可以识别 trial 状态。
- [x] 主机测试用模拟 eMMC 和模拟 QSPI 覆盖成功、擦写失败、回读不匹配和断电点。
- [ ] 板级验收展示 fixture 安装后 B 槽版本启动的串口证据。

2026-10-28：代码、完整主机测试和 sanitizer 通过；已构建诊断 A/B 和 stub，真机暂存及未确认 pending 拒绝通过。实际擦写、B 启动和 A 摘要复核等待用户在本机 USER 键确认，不能由开发助手代答。当前复选项的完成依据为主机验证，真机范围见 [任务 04 证据](../../../ota-evidence/2026-10-28/t04-install.md)。
