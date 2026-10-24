# 阶段 0 基线：netprobe 失败率分桶（2026-10-24）

固定固件：当天 08:35 构建镜像（基线，无取证代码）。端点：
TARGET=token-plan-cn.xiaomimimo.com，CONTROL=api.xiaomimimo.com，
命令 `vgvoice netprobe <host> 443 /v1/chat/completions 8000`（握手成功 = rc=0 status=401）。

## 结果汇总

| 场景 | 计划/日志 | 探测次数 | 失败 | -0x4380 |
|---|---|---|---|---|
| 同次开机静屏（开机于 08:42，此前 08:44/08:58 曾 2 连败） | baseline-static | 30 | 0 | 0 |
| SWD 复位后静屏（09:47 复位，等待 45s 启动稳定） | baseline-after-reset | 30 | 0 | 0 |
| **fixture critical 告警状态** | baseline-hmi | 15 | **12** | **13** |

- 静屏两次 30/30 全过，说明同域同镜像下握手链路本身正常；08:44/08:58 的 2 连败
  在两次测量之间已自行恢复，符合间歇性、突发聚集的历史特征。
- critical 状态下失败率 12/15（80%），**可按需复现**。
- 失败样本失败耗时 240-580ms（静息成功握手整轮 660-720ms），与历史失败样本的
  tls=190-200ms 快速失败特征一致。

## critical 失败时序要点（baseline-hmi.log 事件流）

1. mode=critical → 从站进入 critical → 板端告警评估 + vgadvice 后台解释轮启动。
2. vgadvice 的 ReAct 轮（多次 LLM 请求 + 读 alarm_interpretation.md skill）运行
   期间，探测开始交替失败；vgadvice 自身请求先是成功的（trace status=200，
   reused=0 pool=na，走受控流式路径不占连接池）。
3. 工具调用与 advice 落盘之后出现 10 连败（每次 -0x4380），期间无一次成功握手，
   直到 mode 恢复 normal。
4. 失败期间 netprobe 走连接池阻塞路径（pool 槽），vgadvice 走受控流式路径，
   两条 TLS 路径并存；屏幕告警页处于持续刷新状态。

## 待阶段 1 取证裁决的问题

critical 状态引入了三类并发变化，当前证据无法区分主因：
- HMI 告警页重绘（LTDC 扫描 + 渲染写 SDRAM，历史越界写嫌疑区）；
- vgadvice 后台轮与 netprobe 并发 TLS（两条 BIO 路径、堆分配高峰）；
- 告警事件/eMMC 写入高峰。

取证镜像（双副本字节流捕获 + 失败落盘 /data/forensics/）在相同 critical
复现场景下采集后，由 scripts/voice_test/tls_forensics_verify.py 离线裁决：
捕获流损坏 → 网络方向（IOB/驱动）；捕获流完好而板端验签失败 → 堆破坏方向。

## 现场恢复

- fixture mode 已恢复 normal（baseline-hmi 最后一步）。
- 复位方式说明：NSH 无 reboot 命令，SWD 复位用 STM32_Programmer_CLI
  `-c port=SWD mode=HotPlug -rst`（flash.ps1 同款工具链）。
