# 写者边界与代码侧排除（2026-10-24 深夜，接 02-ttss-trigger.md）

对端二分结果（contest 7824504，logs/bisect-l1/l2.log，--tls-forensics 叠加）：

- level 1（环 calloc + 线程，零音频 ioctl）：3/3 握手 OK。
- level 2（open/RESERVE/GETCAPS/CONFIGURE/ALLOCBUFFER×4/REGISTERMQ，无
  ENQUEUE/START/DMA）：首次 ttss 即 -0x4380，取证 rxAB=match canary=ok，
  与 02 号文档互证。

## 代码侧排除（本节，逐项核对源码）

| 嫌疑（对端排序） | 核对结果 |
|---|---|
| ① wm8994_configure 武装 SAI DMA | 排除：configure 仅编解码器 I2C + 参数保存；setbitrate→I2S_RXSAMPLERATE→sai_samplerate 只写 SAI 寄存器（stm32_sai.c:1320，clockhold 保护），datawidth 同（:1361）；SAI DMA 仅在 sai_send/sai_receive（ENQUEUE 后）启动 |
| ② wm8994_reserve worker mq | 排除：reserve 仅置标志（wm8994.c:1575-1609）；worker 线程在 wm8994_start 创建（:1263），level 2 无 START 不会走到 |
| ③ ALLOCBUFFER 分配路径 | 未排除，但注意：分配本身不产生越界写，除非分配器缺陷 |
| SAI1 PDM 采集 DMA 常开 | 排除：bringup 仅 stm32_sai_initialize（stm32_bringup.c:465），采集须显式 stm32_pdm_capture_start（应用传入 ring），ttss 与晨间 critical 均无采集会话 |

## 修正后的写者模型

L1（128KB 环 + 16KB 线程栈分配）不触发、L2（会话 + 4×apb 分配）触发，
两者 UI 活动完全相同——推论：**写者是常驻后台写者，触发器只是把受害者
播种进写者的打击区**。对端否掉的是渲染作为"触发器"；渲染作为"写者"仍与
全部数据自洽（2026-09-12 渲染越界写砸 SDRAM+1MB 堆节点、根因未修，
18bec160782 只挪了堆）。晨间 critical（无音频）与 L2（音频会话）作为两个
不同的播种路径均成立；TCB 破坏（布局敏感换受害者）同模型。

候选常驻写者（按嫌疑排序，待定位）：
1. LVGL 渲染路径越界写（09-12 有实锤、未根修；vghmi live 每 ~1.5s 刷新，
   告警/浮窗期重绘密集）；
2. 某线程栈溢出写穿（vgvoice/advice 等大栈线程的栈在 SDRAM 堆内，
   0xd0279218 一类；coloration 已开但未见溢出告警，待复核高水位）;
3. 音频 apb 相邻对象的越界写（L2 播种者恰为 apb 分配，与受害者相邻性
   待堆邻域数据验证）。

## 下一步（写者定位，需板端）

1. **堆邻域快照**：取证失败时刻用 mm_foreach 走受害缓冲周边
   （如 in_buf±32KB）打印堆节点边界与大小，识别打击区内对象归属。
2. **金丝雀哨兵板**：在通用堆按地址梯度布 canary 块（复用 5930feb/
   1f807ab 堆巡检谱系），ttss 与 critical 两个播种路径下观察哪些地址
   被写、写入时刻与活跃任务的对应关系。
3. 复核各 SDRAM 栈线程的 coloration 高水位（栈溢出写者假设的快速证伪）。

## 现场状态

板端现跑对端 bisect 测量镜像（--tls-forensics，含 vgvoice bisect 0-2 旋钮，
commit 1cab570/7824504），可继续作取证哨兵；发布镜像恢复入口
`bash scripts/build.sh --clean`。
