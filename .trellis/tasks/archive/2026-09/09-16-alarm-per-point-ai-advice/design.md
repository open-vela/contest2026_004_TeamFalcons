# 设计：告警页逐点 AI 建议

## 1. 与计划的两处修正（读代码后）

1. **不需要每行去重哈希**。`lv_label_set_text` 没有同文本提前返回（`gui/lvgl/src/widgets/label/lv_label.c:93` 一进来就 `lv_obj_invalidate`），但现有 `set_row_texts` 本来就每 tick 重设 `row_sum`，告警活动时 `refresh_alarm` 也本来就在 5 Hz 跑；新增一行 AI 文本不改变量级。按既有就地刷新模式实现即可，不引入新的去重状态。
2. **共享结构体不用复制**。`gui/CMakeLists.txt:43` 已把 `app/velaguard` 加入 PC 模拟器的 include 路径，`app/velaguard/Makefile:124` 也把 `$(CURDIR)` 加进板端路径，所以 `gui/main/ui/model/vg_ui_backend.h` 可以直接包含 `vg_ai_contract.h`，复用 `vg_ai_advice_entry_t`，不需要两份定义加静态断言。

另把契约里 `sum` 的上限从 96 B 收到 80 B：行内前缀 `AI · ` 占 6 B，96 B 的行缓冲（`vg_page_alarm.c:353`）要同时放前缀与 NUL，80 B 留出余量。

## 2. 后端缓存与查询

`vg_ui_backend.h` 直接包含 `app/velaguard/vg_ai_contract.h`（`vg_model.h:8` 已包含前者，所以告警页天然可见，不需要新增 include）：

```c
typedef enum {
    VG_UI_ADV_IDLE = 0,   /* 无活动告警或尚未发起 */
    VG_UI_ADV_PENDING,    /* 已发起，等待产物 */
    VG_UI_ADV_READY,      /* 已装载校验通过的文档 */
    VG_UI_ADV_ERROR       /* 超时、校验失败或离线 */
} vg_ui_advice_state_t;

void                 vg_ui_alarm_advice_request(void);
vg_ui_advice_state_t vg_ui_alarm_advice_state(void);
bool                 vg_ui_alarm_advice_get(const char *sensor_id,
                                            uint32_t al_epoch,
                                            vg_ai_advice_entry_t *out);
```

- 板端实现在 `vg_ui_backend_board.c`：持有 `vg_ai_advice_doc_t` 与状态，互斥保护。
- `vg_ui_alarm_advice_get` 命中条件：状态为 READY、文档 `boot` 等于 backend 当前 boot、且 `vg_ai_advice_find` 按 `(id, epoch)` 找到条目。**epoch 参与命中判断**，这就是同一测点重新起一轮告警时旧建议失效的机制。
- PC mock（`gui/main/ui/model/vg_ui_backend.c`）三个函数分别为空实现、`VG_UI_ADV_IDLE`、返回 false，模拟器行为不变。
- 请求的发起与限流在子任务 A 的 worker 调度里，本子任务只消费状态，不自己发请求、不建线程。

## 3. 页面改动（`gui/main/ui/pages/vg_page_alarm.c`）

### 3.1 上下文与行结构

- `alarm_ctx_t` 增加 `lv_obj_t * row_ai[ALARM_LIST_MAX];`。行仍是 flex column，在 `row_sum` 之后追加第三个标签。
- `make_alarm_row`（:268）创建 `row_ai`：`LV_LABEL_LONG_DOT`、宽度 `lv_pct(100)`、`vg_font_small()`、颜色 `vg_color_info()`，初值空并**默认隐藏**（`LV_OBJ_FLAG_HIDDEN`）。
- `row_sum` 保持原状：AI 建议是**追加一行**，不是替换确定性摘要。告警数值、阈值、持续时间仍然留在行内，详情区的度量行也不受影响。
- 隐藏而非删除：没有建议时行高与今天完全一致，8 条告警的最坏情况也只是多一行，`body` 本身可滚动。

### 3.2 行内文本

`set_row_texts`（:349）末尾追加：

```
s = vg_model_get_sensor(a->sensor_id);        /* 已有变量 */
epoch = (s != NULL) ? s->al_epoch : 0;
if (vg_ui_alarm_advice_get(a->sensor_id, epoch, &adv)) {
    char ai[96];
    lv_snprintf(ai, sizeof(ai), "AI · %s", adv.sum);
    lv_label_set_text(s_alarm_ui.row_ai[idx], ai);
    lv_obj_clear_flag(s_alarm_ui.row_ai[idx], LV_OBJ_FLAG_HIDDEN);
} else {
    lv_obj_add_flag(s_alarm_ui.row_ai[idx], LV_OBJ_FLAG_HIDDEN);
}
```

未命中就隐藏，**不合成任何 AI 文本**（V2/V5）。`build_sig`（:254）保持只哈希 id 与严重级：同点同级别的新一轮告警不触发结构重建，行文本每 tick 按当前 `al_epoch` 重查缓存即可。

### 3.3 详情区

把 `set_ai_text`（:171，只设正文）扩成同时设头与正文的 `set_ai_block(head, body)`，保留旧名给规则路径用或直接替换全部调用点。`render_detail`（:408）末尾按状态分派：

| 状态 | `ai_head` 文本 | `ai_lab` 正文 |
|---|---|---|
| 命中 | `OPENVELACLAW 建议（AI 推测）` | `【AI 建议】sum` / `【依据】ev` / `【建议关注】att` 三段，段间换行；`unresolved` 为真时末尾加一行 `证据不足，未能给出确定结论` |
| PENDING | `AI 建议生成中，暂显示规则摘要` | 现有规则摘要模板原样 |
| ERROR 或离线 | `AI 建议不可用，显示规则摘要` | 现有规则摘要模板原样 |
| 无活动告警 | `规则摘要（本地）` | `【规则摘要】当前无活动告警。` |

`a == NULL` 的分支保持现有行为，只把头部文本由 `规则摘要` 改为 `规则摘要（本地）` 以统一措辞。

正文缓冲：现有 `char buf[160]` 只够规则摘要，AI 正文另开 `char ai[560]`（`80 + 200 + 200` 加上三段标签与换行），`ai_lab` 是 `LV_LABEL_LONG_WRAP` 且宽度百分比满行，能换行显示。

### 3.4 刷新纪律

`refresh_alarm`（:500）只做 `vg_ui_alarm_advice_get` 的 8 条以内内存查表，不做文件 IO、不发请求、不加锁跨线程等待。板端缓存由 worker 线程更新，页面读取时由 backend 内部短临界区保护。

## 4. 降级路径的完整性

AI 文本的唯一来源是 backend 中已通过 `vg_ai_advice_parse` 的文档，页面没有任何通往 LLM 或文件的路径，因此不存在伪造 AI 文本的可能。断网、超时、校验失败都只影响 backend 状态，页面统一落到规则摘要分支。

## 5. 可测性

- headless：`gui/headless/alarm_check_main.c` 已能构造告警场景，扩展它注入一份预制文档并断言行文本前缀、详情头文本、以及 bump `al_epoch` 后的回退。需要一个仅在 headless 构建下可见的测试钩子（例如 `vg_ui_backend_test_set_advice(const char *doc, size_t len)`），板端不编译该钩子。
- 板端手测按 `implement.md` 的序列执行。

## 6. 回滚

改动集中在 `vg_page_alarm.c`、`vg_ui_backend.h/.c`（mock）与 `vg_ui_backend_board.c` 的缓存部分。全部回退后页面回到今天的纯规则摘要行为，A 的契约模块与请求通道可保留不影响其他功能。
