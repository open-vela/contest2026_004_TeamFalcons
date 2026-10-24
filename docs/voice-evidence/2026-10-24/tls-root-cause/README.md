# TLS 握手 -0x4380/-0x3ae2 根因排查（2026-10-24）

接手复核更新：本目录记录历史假设和实验。新取证已将捕获的 -0x4380 定位到 ServerHello 随机数复制时的 FMC 非对齐读取，修复和正式镜像复测见 [接手报告](../p1-p2-takeover/README.md)。历史 -0x3ae2 本轮未重现，不宣称已有单独的因果证明。新增 mbedTLS 观测仅在诊断开关启用时编入，正式镜像已关闭。

任务：根治板端对 token-plan-cn.xiaomimimo.com 的 TLS 1.2 握手间歇性
RSA 验签失败（mbedTLS -0x4380）与证书 ASN.1 解析失败（-0x3ae2）。
计划文件：`~/.claude/plans/2026-10-24-08-58-11-543-send-ascii-34-tidy-rivest.md`（已批准）。

## 背景速览

- 2026-10-23 约 130 次握手表约 10 次失败（9×-0x4380 + 1×-0x3ae2），全部指向
  token-plan-cn.xiaomimimo.com，突发聚集后自愈；2026-10-24 08:44/08:58 纯 CLI
  ask 路径 2 连败（tls=190-200ms 快速失败）。
- 错误机理唯一：VERIFY_OPTIONAL 下 -0x4380 只能来自 ServerKeyExchange 验签
  （ssl_tls12_client.c:2411-2438），证书链问题被豁免。
- TCP 校验和开启 → 字节损坏大概率发生在 TCP 校验之后（mbedTLS 堆对象，
  大块落 SDRAM 堆）。头号嫌疑：SDRAM 堆被渲染/显示路径覆写
  （历史实锤：2026-09-12 渲染越界写、2026-10-17 LCD G0/G1 错接 SDRAM 地址线）。

## 目录内容

- `plans/baseline-static.json`   阶段 0：静屏 netprobe 基线（20×TARGET + 10×CONTROL）
- `plans/baseline-reboot.json`   阶段 0：重启对照（reboot 后同 30 次探测）
- `plans/baseline-hmi.json`      阶段 0：critical 告警重绘期间 10×TARGET + 5×CONTROL
- `00-baseline.md`               阶段 0 结果（待运行后填写）

## 已完成的准备

1. 取证固件代码：packages/ai_agent 分支 `velaguard/tls-forensics`，
   `CONFIG_AI_AGENT_TLS_FORENSICS`（默认 n）。握手收发字节流双副本捕获 +
   失败时落盘 /data/forensics/tls-\<n\>/（cert.der、ske-in-msg.bin、
   stream-rx-a/b.bin、stream-tx.bin、bundle.b64、meta.txt）。
   构建入口：`bash scripts/build.sh --tls-forensics`（不进发布配置）。
2. 离线验签脚本 `scripts/voice_test/tls_forensics_verify.py`，
   `--self-test` 已对真实服务器通过（流解析 / 证书匹配 / RSA-PSS 离线验签 PASS）。
3. 服务器参考链指纹在脚本运行时实时获取（防证书轮换）。

## 阶段 0 执行方式（COM3 空闲后）

```bash
PATH="/mnt/c/Windows/System32/WindowsPowerShell/v1.0:$PATH" python3 \
  docs/voice-evidence/2026-10-23/bvo14-closeout/handoff-tools/run_native_plan.py \
  docs/voice-evidence/2026-10-24/tls-root-cause/plans/baseline-static.json \
  docs/voice-evidence/2026-10-24/tls-root-cause/baseline-static.log
```

reboot 与 hmi 计划同理换文件名。跑计划前确认已在 vela> 输入 quit 退回 nsh>，
且无其他进程占用 COM3。解析：`vgvoice: netprobe rc=` 行分桶失败率，
失败样本同时看相邻 `[vela_tls] ssl_handshake ret=` 行取错误码。

## 约束

- 不做掩盖性重试、不关验签、不丢失败样本。
- COM7 从站进程不动；带 mode 步的计划才涉及 .debug/maintenance-slave-mode.txt。
- 诊断固件/分支不进交付；交付分支 grep FORENSICS 必须 0 命中。
