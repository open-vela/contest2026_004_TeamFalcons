# 阶段 2A 决定性闭环：ttss 确定性触发器 + 池 A/B（2026-10-24 晚）

背景：P2 语音会话移交证据（docs/voice-evidence/2026-10-24/p2-reuse/，commit
f55738a）——vgvoice ttss 在 TTS 播放器（vg_tts_player：128KB 环 calloc +
AUDIOIOC_ALLOCBUFFER 堆缓冲 + 16KB 线程）启动后 TLS 握手必败；另有同源码
不同构建布局的确定性 TCB 破坏硬fault（fopen→nx_vopen→getumask NULL+0x29，
故障任务栈基址在任务全表不存在）。

## 实验与结果

镜像均为当前树（含 P2 voice streaming 与取证代码，ai_agent
velaguard/voice-streaming ⊃ velaguard/tls-forensics）。
复现计划 `plans/ttss-repro.json`：netprobe×2 → vgvoice ttss×3 → netprobe×2。
注意 17:52 起服务端对本板所有请求回 HTTP 400（P2 移交信息 #3），因此
ttss 成败以 TLS 层 ssl_handshake 行判读，rc=http status=400 表示握手成功。

| 镜像 | mbedtls 数据位置 | netprobe(前后) | ttss×3 | 取证 |
|---|---|---|---|---|
| 取证版（堆） | SDRAM 堆 0xd02axxxx | 4/4 OK | **3/3 -0x4380** | 3/3 落盘 |
| 池版 v1* | （池未装载，实为堆） | 4/4 OK | 3/3 -0x4380 | 3/3 落盘 |
| **池版 v2** | **AXI SRAM 池 0x24037c00** | 4/4 OK | **3/3 握手成功（HTTP 400）** | 无失败可捕 |

\* 池版 v1 暴露构建坑：ai_agent 旧 .o 未随 Kconfig 宏重编（平铺构建不自动
重编包对象），池代码被编出镜像外；已在 build.sh 诊断分支补
`find packages/ai_agent -name '*.o' -delete`，v2 确认装载
（TLS pool installed: 102400 bytes at 0x24037c00）。

另：取证构建已默认关闭 vg_llm_health（30s 后台探测与被测握手争用唯一
捕获槽，曾致失败样本静默丢失；frn_begin 槽忙时现在会留痕）。

## 取证判决（ttss 三份转储，dump-ttss-0/1/2）

三份签名完全一致（ret=-0x4380，rx=4373/tx=283/in_msg=300，双副本一致，
金丝雀完好）：

1. 接收流三张证书 SHA-256 与服务器当前真实链全部一致；
2. 失败记录即 SKE（0x0c 00 01 28，整条 300 字节），且 in_msg 与捕获流中
   同一记录**逐字节一致**——TLS 输入缓冲在失败时完好；
3. 用捕获字节离线重演 SKE 验签（RSA-PSS-SHA256）通过；
4. 失败连接的 in_buf/in_msg/out_buf 均在 SDRAM 堆（0xd02a11a8 等），
   ssl ctx 在 AXI（0x24037df0）。

推论：被破坏的是验签的另一侧输入——解析后的证书 RSA 公钥、握手随机数
或验签临时对象（全部 mbedtls_calloc 堆对象）；与上午 critical 告警触发器
的取证结论（dump-tls-0）完全一致。

## 结论

- **写者行为**：后台写破坏通用堆内容；受害对象由堆布局/分配时序决定
  （ttss 场景：voice_conn 重连的缓冲在播放器 128KB 环 + 音频缓冲之后分配，
  落入破坏区；早先建立的池化连接缓冲幸免；对端 TCB 破坏同理）。
- **区域免疫**：mbedtls 全部对象移入 AXI SRAM 静态池后，同一确定性触发
  3/3 握手成功、池金丝雀零破坏——写者不达 AXI 静态池。
- **触发器收敛**：vg_tts_player 路径（128KB 环 / AUDIOIOC_ALLOCBUFFER
  音频缓冲 / SAI DMA / 播放线程）为晚间的确定性触发；上午 critical 告警
  触发（无音频参与）随 Windows 重启消失，可能为第二触发或同一写者在不同
  负载下的表现。
- **写者嫌疑排序**（待定位，P2 模块范围）：音频缓冲释放后 DMA 仍在写、
  播放器环形缓冲索引越界、AUDIOIOC_ALLOCBUFFER 生命周期错配、SAI DMA
  越界。定位手段：确定性触发下对通用堆布哨（5930feb/1f807ab 巡检 +
  canary），或池版保持 TLS 免疫的同时在堆区放哨兵观察写者是否仍在写。

## 现场与产物

- 镜像：.debug/nuttx-forensics-p2.hex（取证+P2）、.debug/nuttx-pool-p2.hex
  （取证+池+P2，8KB 内容缓冲）。
- 计划：plans/ttss-repro.json、pull-dump-p2c.json。
- 日志：ttss-forensics-run1/2、ttss-pool-run1/2、pull-dump-p2b/p2c。
