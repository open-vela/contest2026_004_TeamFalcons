# 随机画面错位排查记录

日期：2026-10-28。现象为静置时偶发短暂错位、扭曲，语音期间更频繁。

## 结论与限制

已确认板端会发生 LTDC FIFO 欠载，并找到语音浮层持续触发重复重绘的问题。修复前，两轮语音测试窗口各记录一次欠载；限制重复重绘后，同样两轮窗口未记录到欠载，显示提交次数明显减少。这支持重复重绘增加 SDRAM 访问竞争、加重显示欠载的判断。

已运行的 UI 内存检查没有发现越界或释放后访问；运行中采样的显存地址也合法。不能据此排除所有板端 DMA、并发或未覆盖路径中的内存错误。此次没有同步拍摄面板，也没有收到某次闪屏的精确时间标记，因此尚未逐次对应可见闪屏与欠载，短测通过不等于长期问题已经消失。

## 板端证据

初始运行固件为 `Oct 25 2026 13:14:27`、NuttX `5454419b235-dirty`。通过 ST-LINK Hot Plug 读取寄存器，初次 `LTDC_ISR=0x00000002`，即 FUIF；该单次读取可能包含历史错误，不单独作为复现证据。

原驱动仅接入 LTDC 全局中断，没有接入独立的 LTDCER 错误中断。NVIC ISER2 初次值为 `0x81040070`，IRQ 89 的使能位为零。原处理中，换帧完成路径还可能提前返回、延后处理同次到达的错误。新增累计计数后，才用下面的增量比较验证错误会重新发生。

| 测试窗口 | 目标采样时长 | FIFO 欠载增量 | 传输错误增量 | 显示提交增量 | 原始记录 |
|---|---:|---:|---:|---:|---|
| 修复前静置 | 30 秒 | 0 | 0 | 30 | [before-idle.log](before-idle.log) |
| 修复前语音样本 1 | 60 秒 | 1 | 0 | 438 | [before-voice1.log](before-voice1.log) |
| 修复前语音样本 2 | 60 秒 | 1 | 0 | 454 | [before-voice2.log](before-voice2.log) |
| 修复后语音样本 1 | 60 秒 | 0 | 0 | 24 | [after-voice1.log](after-voice1.log) |
| 修复后语音样本 2 | 60 秒 | 0 | 0 | 46 | [after-voice2.log](after-voice2.log) |

两轮错误时的显存地址分别为 `0xd0000000`、`0xd003fc00`，扫描位置寄存器分别为 `0x00b50012`、`0x00580062`。当前 RGB565 单帧 `480 × 272 × 2 = 261120` 字节，双帧共 `522240` 字节，占用 `[0xd0000000, 0xd007f800)`；SDRAM 堆从 `0xd0200000` 开始，未与显存重叠。行跨度寄存器为 `0x03c003c3`，即跨度 960 字节、有效行长度 963，行数 272。

样本触发命令为 `vgvoice round ask /data/velaguard/voice/point01.wav` 和 `point02.wav`。两轮均执行本地录音路径，采样窗口包含后续识别活动；其中一次识别失败。这里比较显示计数，不能当作两轮完整 ASR、Agent、TTS 和播放成功的验收。前后测试的云端延迟和结果并不完全相同。

## 修改

- `gui/main/ui/widgets/vg_voice_overlay.c`：文本变化时才更新标签；可见性变化时才修改隐藏标志。当前 LVGL 的 `lv_label_set_text()` 和重复移除隐藏标志都会触发失效重绘，原浮层每 100 ms 重复调用这些接口。问题标签使用 LONG_DOT，会修改内部显示文本，因此保留原始问题文本进行比较。
- `gui/main/ui/model/vg_model.c`：按 `VG_VOICE_ROUND` 条件编译语音查询，恢复无语音模拟器和原有离屏检查的链接。
- `gui/headless/`：新增实际语音浮层检查，覆盖最大长度文本、180 次状态绘制、20 次创建/销毁、静态快照不刷新及新回答仍能刷新。语音模型和浮层一起按语音配置编译，流水线由测试快照替代。
- NuttX 提交 `33acb0847de`：接入 LTDCER；先处理同次到达的错误位；错误中断不再错误地释放换帧等待。保存欠载、传输错误、最后错误位置与显存地址的累计计数，不逐帧打印。
- `scripts/ltdc_observe.ps1` 与 `scripts/gdb/ltdc-observe.tcl`：通过 SWD 只读采样累计计数，使用匹配 ELF 定位诊断数据，不复位、不暂停、不写运行状态。日志记录 ELF 摘要，并检查诊断标记。此标记检查不能替代确认 ELF 与已烧录固件匹配。

本次没有修改 SDRAM MPU 属性、时钟、引脚、帧大小或行跨度，也没有改动点表或凭据。

## 复测

内存检查：

```sh
cmake -S gui/headless -B .debug/ui-memory-audit-system \
  -DCMAKE_C_COMPILER=/usr/bin/gcc \
  -DVG_HEADLESS_PROFILE=board -DVG_HEADLESS_COLOR_DEPTH=16 \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build .debug/ui-memory-audit-system -j8
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir .debug/ui-memory-audit-system --output-on-failure
```

6 项通过，见 [asan-ubsan.log](asan-ubsan.log)。修复前静态快照重绘断言失败 20 次，见 [red-static-redraw.log](red-static-redraw.log)。内存检查未开启泄漏检测，也不覆盖板端显示驱动和 DMA。

固件入口 `bash scripts/build.sh` 编译成功，已用 `scripts/flash.ps1` 烧录并运行。此次板测镜像摘要：

```text
ELF SHA256 d9bb65fa18f7d6a8be08dc216f1e0d9fdea4ffe973b9eb0d9cb111c55acfd095
HEX SHA256 69ee4ff75bae0ef479187d1de7618069302d9b33cc1c125c453ce289f2e20a70
```

Windows 侧采样命令：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/ltdc_observe.ps1 `
  -Seconds 60 -LogPath .debug/ltdc-observe.log
```

在另一个终端触发已有样本或通过 USER1 运行实际语音。显示提交增加是正常绘制；只有 FIFO 或传输错误增量表示本次观测到了对应硬件错误。如果再次闪屏，保留发生时间并对照累计计数，再决定是否继续检查 SDRAM 仲裁、绘制访问量或其他内存路径。

提交前日志校验检查 323 个文件、76765 个事件，通过但带一项既有警告：2026-09-16 的一份 Claude 会话文件未出现在 manifest.sessions 中。未改动日志正文或清单。

## 资料

板级连线核对使用本地 `F:\Project\Embeded\H750B-DK\BOARD INFO\H750B-DK\mb1381-h750xb-b01-schematic.pdf` 的第 3、9、12 页，分别对应 MCU、SDRAM 和 LCD；板上 SDRAM 只连接 DQ0..15。未依据 unofficial 文件修改引脚。

ST 的 [AN4861 显示带宽检查](https://www.st.com/content/st_com/en/technical-documents/an4861-introduction-to-lcdtft-display-controller-ltdc-on-stm32-mcus-stmicroelectronics_asset/chunk-005-topic-an4861-creating-a-graphical-application-with-ltdc.html)说明取数带宽不足可导致显示问题与 FIFO 欠载；[中断说明](https://www.st.com/content/st_com/en/technical-documents/an4861-introduction-to-lcdtft-display-controller-ltdc-on-stm32-mcus-stmicroelectronics_asset/chunk-004-topic-an4861-two-programmable-ltdc-layers.html)区分全局与错误中断；[访问优化说明](https://www.st.com/content/st_com/en/technical-documents/an4861-introduction-to-lcdtft-display-controller-ltdc-on-stm32-mcus-stmicroelectronics_asset/chunk-007-topic-an4861-ltdc-synchronization-with-dma2d-and-cpu.html)介绍 CPU 和显示控制器竞争外部内存的影响。
