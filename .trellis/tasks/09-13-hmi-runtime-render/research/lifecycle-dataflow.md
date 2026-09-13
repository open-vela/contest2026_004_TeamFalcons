# 生命周期与数据流核查

## 页面根问题

```text
vg_nav_goto
  -> create_page
     -> lv_obj_clean(s_content)
     -> vg_page_*_create(s_content)
        -> 在 s_content 上注册 LV_EVENT_DELETE
```

`lv_obj_clean()` 只删除子对象，原宿主持续存在。report/discover 的退出回调因此不会在常规切页时自然执行。回调中保存的 label/timer/context 生命周期与屏幕看到的页面不一致。

最小修复是在 Shell 为每页创建自己的根对象。无需缓存所有页面、重写路由库或逐页依赖 `lv_obj_is_valid()` 猜测所有权。

## 同步慢操作

```text
report 点击/定时器 -> read_latest_report -> opendir/readdir/open/read
模型 tick -> apply_live -> pending_alarm_clear/write -> 文件系统
```

函数名包含 backend 不代表已经后台执行。验收必须观察调用线程并用延迟注入确认返回和滚动仍正常。

## 采集数据身份

```text
采集线程：读取点表 -> Modbus -> 写数组/数量 -> 增加 cycle
UI 线程：读取数量 -> 检查表 gen -> 检查 cycle -> 遍历数组
```

数组、数量和 cycle 不是一个原子事务；表 copy 和 gen 也不是一个一致性快照。重排点位时数组下标尤其危险。版本化复制需要同时保护写方，只有读方加锁不起作用。

既有 `g_live_cycle` 去重成果必须保留，不能因为新的 UI timer 周期更快就重复处理同一失败样本。

## 告警文件语义

- 主告警文件只代表一条当前待处理记录，不能因多告警 UI 改成未约定的多记录格式。
- 当前 `vg_agent_alarm.c` 写 helper 遇到文件存在会直接返回成功，所以应理解原调用方 clear/write 的组合含义。
- 异步 worker 需要处理旧清理与新告警的版本关系，同时允许 Agent 正常消费并删除文件。
- 业务确认/静音仅改变已知晓状态，不等于告警恢复。UI 缓存不得清除规则告警。

## 防止过度优化

先按版本跳过未变化数据，再考虑批量图表更新；不先降低业务 tick。保持四条趋势序列和现有阈值显示逻辑，不因本任务顺手修理其他规则或增加图表功能。

源码证据只证明这些调用关系和风险，没有测量实际耗时或故障发生次数。阶段性结果必须由实施者真实运行后补充。
