# Implement: 告警页多分栏显示与逐告警处理静音

## 执行清单(按序)

1. [x] 模型层 vg_model.h:vg_sensor_t 增 al_duration_sec/al_acked/al_muted;声明 collect/count/quieted/ack_id/mute_id/mute_all API。
2. [x] 模型层 vg_model.c:
   - derive_primary_alarm() 替代 set_alarm_from_sensor;
   - set_live 阈值/离线路径去 rank 守卫,加 transition 重置/恢复清理 + 触发/恢复日志;
   - tick 逐点时长递增 + 派生;
   - apply_scenario / import_runtime_points 改派生;
   - 实现 collect_alarms / active_alarm_count / alarms_all_quieted / ack_alarm_id / mute_alarm_id / mute_all_alarms;no-arg ack/mute 改为按主告警 id 操作。
3. [x] 告警页 vg_page_alarm.c 重写:分栏列表(signature 重建 + 选中高亮 + 逐栏按钮)+ 详情区绑定选中 + head 计数;删底部全局 actions。
4. [x] backend vg_ui_backend_board.c:pending 写清改 tag 变化重写 + 全部恢复才清。
5. [x] vg_shell.c chip 变暗条件改 all_quieted;vg_page_home.c on_mute 改 mute_all。
6. [x] 验证:
   - `make -C app/velaguard/host_tests test`
   - PC 模拟器:cmake 构建 gui,切 WARN/CRIT/OFFLINE 场景冒烟(多点分栏、点选详情、按钮态、布局溢出)。
   - 板端:`bash scripts/build.sh` 编译 velaguard-lvgl;板在位则走 flash + COM3 验收(AC1-AC6)。
7. [x] 文档:更新 .agents/skills/velaguard-alarm-to-screen/SKILL.md 多告警语义。

## 验证命令

```bash
make -C app/velaguard/host_tests test
cmake -S gui -B gui/build-sim && cmake --build gui/build-sim   # 模拟器(具体 target 以 CMakeLists 为准)
bash scripts/build.sh        # 板端固件 velaguard-lvgl
```

## 回滚点

- 提交 1:模型层(可独立 revert,回退到单告警行为)
- 提交 2:告警页 + shell/home 适配(UI 层)
- 提交 3:backend pending + 文档
