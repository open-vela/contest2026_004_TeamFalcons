# T10 主机故障、循环与生产材料检查

日期：2026-10-29。代码 `74fc4ad` / `f3ddaa6`。本记录区分主机模拟、实际固件构建和真机运行，不把模拟断电写成真实断电。

## 实际运行

```sh
make -C app/velaguard/host_tests test
make -C app/velaguard/host_tests test-ota-faults
make -C app/velaguard/host_tests test-ota-sanitize
make -C app/velaguard/host_tests test-ota-network
bash scripts/build.sh
```

以上退出码均为 0。末次生产构建的日志为本机 `/tmp/vg-verified-build.log`；主机回归、故障、ASan/UBSan、网络重连日志分别为 `/tmp/vg-final-host.log`、`/tmp/vg-final-faults.log`、`/tmp/vg-final-sanitize.log`、`/tmp/vg-final-service-reconnect.log`。临时路径不作为长期证据依赖，关键结果如下。

| 测试 | 范围与结果 |
|---|---|
| test_ota_install | 内存 FAT/QSPI 的 59 个操作边界 × 两种断电/撕裂模式；A 字节不变，只有完整 B 允许 trial，错误拒绝或回 A |
| test_ota_confirm / test_ota_confirm_b | A/B 两种运行身份下的源门禁、联锁、临时文件回读、pending 写入失败、持久取消与过期通过 |
| test_ota_faults | write 返回 ENOSPC、fsync/rename 返回 EIO；结果不被误认为已持久确认；损坏主记录恢复备份，双损坏拒绝新任务 |
| 队列循环 | 同一进程 1000 次结果入队、ACK 位落盘、重新装载、最终删除；每轮目录回到零文件、八个空位 |
| 任务终止日志 | 4096 条、180224 字节后拒绝新任务且不继续增长；损坏记录拒绝新任务 |
| 解析器 | 固定种子 10000 个随机输入、最大长度和深嵌套拒绝；ASan、UBSan 与泄漏检测通过 |
| 网络恢复 | HTTPS 中断/Range/ETag、MQTTS 重连、结果丢 ACK 的真实主机 TLS 流程通过；没有安装授权 |

循环测试保留真实文件 fsync，只对主机测试进程的全局 sync 作空包装，避免反复同步主机无关挂载；不把该循环当作 FAT 断电持久性证据。1000 次结果队列循环不等于 1000 次真实固件安装，也不等于长期整机稳定性测试。

## 生产构建

最终 A/B 镜像均为 1552792 字节，身份 `0.1.0 / f3ddaa60f6d0-dirty`。dirty 包含未提交的证据文档及保留的开发日志，不宣称发布 tag 已生成。

| 镜像 | SHA-256 |
|---|---|
| A | aa7f73ffe00def9127bf7bb9a2de742512b48ab8cf4453010837dffcbef04ada |
| B | 15154fe8df1429311528975515579222e8c6538d880dd3104a245828572bdde4 |

生产构建的 `check_fw_images.py` 检查槽地址、向量、版本及不同 A/B 身份；`check_release.py` 检查最终配置、fixture 入口标记、测试 key_id、测试密码、查询 token、已知测试 CA/公钥和私钥 PEM 标记，实际输出：

```text
OTA release check: PASS fixtures/test material absent; trust=unconfigured; entropy=verified; deploy_ready=0
```

另实际执行两项负例：对空部署信任加 `--require-deploy-ready` 会返回失败；输入带 `vgota fixture arm` 标记的合成镜像会返回失败。最初检查曾发现增量构建沿用旧 .config、没有 PHY ioctl；已修正构建预设校验，重配后生产构建通过。

这项扫描只能排除已知模式和给定材料，不能保证任意未知、混淆或二进制形式的凭据均不存在。生产 CA/公钥与设备密码未提供，因此本次镜像只证明默认拒绝状态，不能称为可部署 OTA。实际 A 镜像已通过 ST-Link 写入、校验并启动，见[T09 板测范围](t09-control-host.md)。

## 未执行

没有真实拔电、QSPI 擦除期间断电、trial/confirmed 写入时断电或真实 B→B 安装。没有长期整机采集/告警/AI 与 OTA 并行、真实 FAT 恢复及完整升级/回滚空间长测。以上保留在[人工及外部条件待办](../../issues/ota/acceptance-todo.md)，T10 与 OTA 整体保持未验收。
