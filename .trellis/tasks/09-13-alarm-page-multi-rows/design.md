# Design: 告警页多分栏显示与逐告警处理静音

## 根因

模型层单告警槽 `s_alarm`(gui/main/ui/model/vg_model.c:39)配合 `vg_model_set_live` 的 rank 晋级守卫(阈值 1733-1749、离线 1794-1810),同时多点告警只保留最高优先级一条。每点的告警严重度本就存在 `vg_sensor_t.severity`,多点告警集合可直接从点表派生。

## 总体思路

告警期状态(acked/muted/时长)下沉到点级(`vg_sensor_t` 新字段);多点告警列表由模型按点表扫描派生;`s_alarm` 保留但语义变为主告警(最高优先级)的派生视图,所有既有单告警消费方(状态栏 chip、诊断页、backend pending 写入)语义不变。

## 1. 模型层 gui/main/ui/model/

### vg_model.h

- `vg_sensor_t` 新增:`int32_t al_duration_sec; bool al_acked; bool al_muted;`
- 新 API:
  - `uint16_t vg_model_collect_alarms(vg_alarm_t * out, uint16_t max)` — 扫描 `s_sensors[]`,severity 非 OK 的点生成 `vg_alarm_t`(title = 点名 + 种类、value、threshold 按 kind 取 warn/crit、duration、acked/muted、sensor_id),按 rank 降序填充,返回条数。rank 用 `vg_alarm_kind_rank` 映射(crit=3 > offline=2 > warn=1)。
  - `uint16_t vg_model_active_alarm_count(void)`
  - `bool vg_model_alarms_all_quieted(void)` — 有活动告警且每条均 acked||muted
  - `void vg_model_ack_alarm_id(const char * id)` / `void vg_model_mute_alarm_id(const char * id)` — 逐点置位 + 日志(带点名) + notify
  - `void vg_model_mute_all_alarms(void)` — 全部活动告警置 muted(首页快捷静音)
- 保留:`vg_model_get_active_alarm()`(返回派生主告警,指针仍为 `&s_alarm` 稳定地址,契约不破)、`vg_model_ack_alarm()` / `vg_model_mute_alarm()`(作用于主告警 id)。

### vg_model.c

- 新静态 `derive_primary_alarm()`:扫描出 rank 最高的告警点,从点级状态填 `s_alarm`(active/severity/title/value/threshold/duration/acked/muted/sensor_id),不重置 acked/muted;无告警则 memset。替代 `set_alarm_from_sensor`。
- `vg_model_set_live` 阈值路径(1664-1762):
  - 删除 rank 晋级守卫(1733-1749)。
  - kind != NONE 且该点此前非告警(transition)→ 重置 al_acked/al_muted=false、al_duration_sec=0,记日志 告警触发: 点名。
  - kind == NONE 且该点此前告警 → 清 al 状态,记日志 告警恢复: 点名(替换 1707-1715 按 s_alarm 属主的恢复判断)。
  - severity 照旧写 `s->severity`(1728-1731)。
  - 路径末尾 changed 时 `derive_primary_alarm()`。
- `vg_model_set_live` 离线路径(1763-1821):同样处理 offline transition/恢复,删除 1794-1810 守卫。
- `vg_model_tick`(1330-1358):对所有 severity 非 OK 的点 `al_duration_sec++`,随后 `derive_primary_alarm()`(替换 1349-1354 的单告警镜像)。
- `apply_scenario`(1026+):移除 `memset(&s_alarm)` 与 `set_alarm_from_sensor` 调用;种子设置 severity 后调 `derive_primary_alarm()`;日志引用 s_alarm.title 不变。场景标题从定制文案变为生成文案(温度 点表预警 等)。
- `vg_model_import_runtime_points`(1560-1630):移除 1623-1626 的 s_alarm 特判(import 循环 memset(s) 已清 al 状态),末尾 derive;n<=0 分支的 memset 保留。
- 派生重建签名:无。派生每次全扫,点数上限 64,代价可忽略;全部调用发生在 LVGL 线程(set_live 由 tick 的 apply_live 调用),无线程安全问题。

## 2. 告警页 gui/main/ui/pages/vg_page_alarm.c(重写)

布局(480x272,内容高约 232):

- head 卡 36px:`活动告警 N 个` / `无活动告警` + 主告警严重度 chip(沿用 vg_status_chip)。
- body 滚动卡(flex_grow=1,竖向滚动):
  - 分栏列表:每活动告警一栏,复用首页 tile 视觉(严重度左边框 4px、斑马底、最小触高 36px)。栏内两行:
    - 行1:点名(ui font)+ 小号严重度 chip + 右侧按钮 静音 / 标记处理(小尺寸,约 56/88 宽 x 36 高;acked/muted 后 DISABLED)。
    - 行2(small font):阈值类 `76.2℃ ≥ 75.0℃ · 持续 120s`;离线 `通信离线 · 持续 300s`;ack/muted 追加状态词。
    - 栏 CLICKABLE,点击设选中;按钮点击不冒泡(子控件命中即截停,不设 EVENT_BUBBLE)。
    - 选中栏高亮(边框/底色)。
  - 详情区:现有 ai_head/ai_lab、6 行 vg_metric_row、hist_lab 原样保留,数据源改为选中告警;规则摘要保持本地规则引擎口径文案。
- 底部全局 actions 行删除(按钮进分栏)。
- 结构重建沿用首页 signature 模式:签名 = 分栏数量 + id 集 + severity 集;签名变化才重建行,文本每帧就地刷新(vg_model_on_change 回调 refresh,1s tick 兜底)。
- 选中状态:ctx 增 `char sel_id[VG_SENSOR_ID_MAX]`;选中点恢复后回落到列表第一栏。
- 数据获取:refresh 里 `vg_model_collect_alarms(list, VG_ALARM_LIST_MAX)`(栈上数组,上限取 8;超出按 rank 截断,状态栏计数用 active_alarm_count 仍显示真实总数)。
- 空状态:无活动告警时列表隐藏、详情区显示占位文案、chip 正常。

## 3. pending_alarm.txt — app/velaguard/vg_ui_backend_board.c:768-798

- 格式不变(单条记录,type/tag/slave/reg/value/threshold/hint),内容 = 主告警;Agent alarm_interpretation seed 与 README 示例不动。
- 逻辑改为:
  - `vg_model_active_alarm_count() > 0`:主告警 sensor_id+type 组成 tag;若与上次写入 tag 不同(或首写)→ `vg_pending_alarm_clear()` + `vg_pending_alarm_write(body)`。Agent 删文件后下一次 apply_live 会因文件缺失重写(保留 write_pending 的 exists-noop 去重,首写判断用 stat 缺失 || tag 变化)。
  - 计数 == 0 且上次 > 0 → `vg_pending_alarm_clear()`(修复任一点恢复即删整文件的问题)。

## 4. 状态栏 / 首页 / 诊断页

- vg_shell.c refresh_alarm_chip(288-317):chip 严重度取主告警;变暗条件改为 `vg_model_alarms_all_quieted()`。
- vg_page_home.c on_mute(313-325):改调 `vg_model_mute_all_alarms()`,toast 提示数量(如 已静音 3 个告警);无告警时保持 当前无活动告警。
- vg_page_diagnosis.c:不改。

## 5. 权衡与备选

- 备选 A(未选):模型维护 `vg_alarm_t[]` 告警队列 — 需要增删去重与同步,结构churn更大;点级状态 + 派生复用既有 severity,改动集中。
- 备选 B(未选):pending 文件改多记录 — 需同步改 Agent seed/skill 解析,超出本次 UI 范围。
- 风险:告警页重写为最大改动,用首页已验证的 signature 重建模式;场景种子标题文案变化需冒烟确认。

## 6. 回滚

- 单任务分支上按提交粒度 revert;模型层与 UI 改动分提交,可独立回退 UI 先行。
