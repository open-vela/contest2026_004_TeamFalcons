# BVO-12 跨页面语音浮层

日期：2026-10-22。状态：实现并接入板端 HMI；首次显示路径已在真机跑通，发现并修复一处必崩缺陷；屏幕布局仍待人工确认。

## 1. 交付

| 文件 | 内容 |
|---|---|
| `gui/main/ui/widgets/vg_voice_overlay.h/.c` | 浮层创建、销毁、100 ms 轮询、按钮与滚动事件 |
| `gui/main/ui/app/vg_app.c` | 初始化时创建浮层，仅在 `VG_VOICE_ROUND` 下编译 |
| `app/velaguard/Makefile` | 未启用语音轮次时从 UI 源列表剔除该文件 |
| `gui/CMakeLists.txt` | 模拟器构建排除该文件 |
| `app/velaguard/vg_voice_round.c` | 浮层命令改为投递标志，由工作线程执行 |

## 2. 行为

- 浮层挂在 `lv_layer_top()`，位于所有页面之上，切换页面不重建、不失效。
- 显示状态行（录音剩余秒数、识别/查询/合成剩余秒数、回答收起倒计时）、识别文本、错误、完整回答和播报开关。
- 回答区可滚动；滚动开始暂停 15 秒收起计时，滚动结束恢复。
- 关闭按钮结束本轮；播报失败时出现重试按钮，只重做 TTS，不重新查询 Agent。
- 浮层只读 `vg_voice_round_ui()` 的快照，并在 100 ms 定时器中复制，不持有会话指针。
- 关闭与重试只设置待处理标志并唤醒工作线程；LVGL 线程不执行网络或播放等待。

## 3. 板端实测

浮层第一次显示就触发断言，`vghmi` 任务 panic。原因是 `poll_cb` 对播报按钮调用了 `lv_label_set_text(s_mute, ...)`：`s_mute` 是 `lv_button_create()` 创建的按钮，不是标签。标签函数把标签字段写进按钮对象，下一次失效重绘时在样式查询里断言。

```
dump_assert_info: Assertion failed panic: at file: :0 task: vghmi process: vghmi
dump_register: PC: 90001ab3 (_assert)
stack: poll_cb (vg_voice_overlay.c) -> lv_label_set_text -> lv_obj_invalidate
       -> get_selector_style_prop (lv_obj_style.c)
```

修复：保存按钮内标签的指针 `s_mute_label`，只改这个标签；`destroy` 时一并清空。崩溃原始日志见 [board-overlay-crash.log](board-overlay-crash.log)。

修复后的板端结果（[board-overlay-fixed-no-wav.log](board-overlay-fixed-no-wav.log)，该次运行还暴露了录音文件未写盘的缺陷，已单独修复）：

| 时点 | 浮层快照 |
|---|---|
| 未开始 | `visible=0 state=0 gen=0` |
| 按下 USER1（合成事件） | `visible=1 state=1 gen=1`，状态行“录音中” |
| 松开后 | `visible=1 state=9 gen=2`，状态行“出错了”，错误行“识别失败，请重试” |

面板能显示、能随状态更新、错误行能出现，说明显示路径可用；崩溃不再出现。

## 4. 几何与命中（板端）

[board-overlay-geometry.log](board-overlay-geometry.log)，固件 `374d3788` / `bc4b718c`：

| 项 | 值 |
|---|---|
| 面板 | 468x76 at (6,190)，屏幕 480x272，右边界 474、下边界 266 |
| 录音中 | 468x58 at (6,208) |
| 播报开关 | 62x24 at (371,197)，中心点 hit=1 |
| 关闭 | 30x24 at (437,197)，中心点 hit=1 |
| 回答区（空） | h16、scroll_bottom=0 |
| 回答区（1143 字节，14 条异常） | 面板 468x152 at (6,114)，回答框 h92、scroll_bottom=164、标签高 256，可滚动 |
| 播报失败时 | retry 62x24 at (371,121) visible=1 hit=1，文字保留 |

测量在 LVGL 线程内完成并发布快照，NSH 只读快照。长回答的滚动量见 [board-overlay-scroll.log](board-overlay-scroll.log)：回答标签 256 px 高、容器 92 px、可滚动 164 px。隐藏状态下的按钮坐标是上次布局的残值，命中测试不作为证据。

## 5. 未验证与限制

- 未在 480×272 屏幕上确认面板高度、中文换行、按钮可点区域与遮挡关系。
- 未验证页面切换过程中的对象生命周期与内存；实现上浮层不随页面删除。
- 触摸按钮（关闭、重试、播报开关）只做了代码路径检查，没有人工点击。
