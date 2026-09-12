# 9/12 实施 plan：告警恢复 + pending unlink + 「规则摘要」标签

对应 `implement.md` 「2026-09-11 核对代码后追加」前三项。只写 plan，未动代码。
出口：`bash scripts/build.sh` 通过，固件可烧；9/13 板测走「拔线→离线→插回→恢复」。

## 0. 调研结论（改动为什么长这样）

| 事实 | 位置 | 对 plan 的影响 |
|---|---|---|
| 在线分支算出 `kind == NONE` 时只改 `s->severity`，`s_alarm` 永不清 | `gui/main/ui/model/vg_model.c:1595-1629` | A1 在这里加恢复 |
| `write_pending` 文件存在即返回 0，固件无任何 `unlink` | `app/velaguard/vg_agent_alarm.c:78-108` | B1 新增 `vg_pending_alarm_clear()` |
| 板端每 tick 在 `apply_live` 末尾尝试写 pending | `app/velaguard/vg_ui_backend_board.c:703-726` | B2 在同一处检测 active→inactive 并 unlink |
| 重灌点表 (`import_runtime_points`) 不清 `s_alarm`，旧 id 消失则告警永久卡住 | `vg_model.c:1489-1553` | A2 兜底清除 |
| `vg_model.c` 依赖 LVGL（`lv_snprintf`/`lv_timer`），不能 host 编译 | — | 状态迁移靠板测；host 只扩 `test_alarm_eval.c` 序列用例 |
| PC 模拟器 `apply_live` 返回 false，从不进 `set_live` | `gui/main/ui/model/vg_ui_backend.c:107` | A 项只影响板端，PC mock 场景不受影响 |
| 告警页 6 处硬编码「AI 推测」，全是 HMI 本地模板 | `gui/main/ui/pages/vg_page_alarm.c:67,136,143,149,181,278,286` | C1 全部改「规则摘要」 |
| `refresh_alarm` 每次 model 变化都跑（告警活动时 ≥5 Hz） | `vg_page_alarm.c:43` | 读 `last_alarm.md` 必须带节流/缓存，今天不做（C2 只留设计） |
| 字体子集已含 规/则/摘/要/恢/复 | `gui/main/ui/fonts/cjk_symbols.txt` | 不需要重生成字体 |
| `velaguard-lvgl.defconfig` 打开 `CONFIG_VG_AGENT_OPS=y` | `scripts/configs/velaguard-lvgl.defconfig:179` | B 项在作品固件里生效 |

风格：`gui/` 下 LVGL 风格（4 空格、`if(`）；`app/velaguard/` 下 NuttX 风格（2 空格、大括号换行）。各文件按各自风格。

---

## A. 告警恢复（`gui/main/ui/model/vg_model.c`）

### A1. 在线分支加恢复（核心）

位置：`vg_model_set_live` → `if(online)` → `#ifdef VG_HMI_BOARD` 块内，紧跟 `kind = vg_alarm_eval(...)` 之后、`if(s->severity != sev)` 之前（约 1595 行后）。

```c
            /* Recovery: this point owns the active alarm and reads normal */
            if(kind == VG_ALARM_KIND_NONE && s_alarm.active &&
               strcmp(s_alarm.sensor_id, s->id) == 0) {
                char logbuf[96];

                lv_snprintf(logbuf, sizeof(logbuf), "告警恢复: %s", s->name);
                memset(&s_alarm, 0, sizeof(s_alarm));
                vg_model_append_log(VG_LOG_ALARM, VG_SEV_OK, logbuf);
                changed = true;
            }
```

行为核对：

- 清零后 `sensor_id` 为空、`active=false`；紧接着的抬升条件 `title != NULL && (...)` 因 `title == NULL` 不触发。
- 其他点若仍处于 WARN/CRIT，下一 tick 自身的抬升条件 `!s_alarm.active` 成立，自动重新当选（duration 从 0 起）。单活动告警模型不变。
- 离线分支不动：`fail_streak < fail_n` 时 `kind == NONE` 但不清告警，短暂读失败不会误恢复（符合 CONTEXT.md「Offline 保留既有告警」）。
- 序列「阈值告警 → 同点离线（升级为离线告警，沿用 duration）→ 插回且值正常 → NONE → 清除」成立。
- 序列「插回但值仍越限」：`kind == WARN`，同 sensor_id → `set_alarm_from_sensor` 覆盖为阈值告警，不算恢复。正确。

不做回差/恢复窗口（手册 16.6 `restore_duration_ms`）：演示用例 eq 水浸和离线都是二值，`ge` 温度贴阈值抖动的风险记入已知问题，9/17 后再议。

### A2. 重灌点表时清除孤儿告警（兜底，6 行）

位置：`vg_model_import_runtime_points`，两处：

1. `pts == NULL || n <= 0` 空表分支：`rebuild_filter()` 之前加 `memset(&s_alarm, 0, sizeof(s_alarm));`
2. 正常分支填完 `s_sensors` 之后、`rebuild_filter()` 之前：

```c
    if(s_alarm.active && vg_model_get_sensor(s_alarm.sensor_id) == NULL) {
        memset(&s_alarm, 0, sizeof(s_alarm));
        vg_model_append_log(VG_LOG_ALARM, VG_SEV_INFO, "告警清除: 点表已更新");
    }
```

注意 `vg_model_get_sensor("")` 返回 `&s_sensors[0]`，所以必须先判 `s_alarm.active`（active 时 sensor_id 非空）。

### A3. 可选：去重 severity→kind 映射

在线/离线两个分支各写了一遍三元链（1617-1620、1677-1680）。可抽 `static enum vg_alarm_kind active_alarm_kind(void)`。纯整理，不改行为；时间紧可跳过。

---

## B. 恢复时 unlink pending（`app/velaguard/`）

### B1. 新增 `vg_pending_alarm_clear()`

`vg_agent_alarm.h`：

```c
#ifdef CONFIG_VG_AGENT_OPS
int vg_pending_alarm_write(const char *body);
int vg_pending_alarm_clear(void);
int vg_agent_alarm_start(void);
#else
static inline int vg_pending_alarm_write(const char *body) { (void)body; return 0; }
static inline int vg_pending_alarm_clear(void) { return 0; }
static inline int vg_agent_alarm_start(void) { return 0; }
#endif
```

`vg_agent_alarm.c`（`vg_pending_alarm_write` 之后，`#ifndef CONFIG_VG_HMI` 之外，两种固件都可用）：

```c
int vg_pending_alarm_clear(void)
{
  if (unlink("/data/velaguard/pending_alarm.txt") < 0)
    {
      return (errno == ENOENT) ? 0 : -errno;
    }

  return 0;
}
```

ENOENT 视为成功：Agent HEARTBEAT 本身也会删这个文件，两边都删不算错。

### B2. 板端 `apply_live` 检测 active→inactive 迁移

`vg_ui_backend_board.c` 的 `#ifdef CONFIG_VG_AGENT_OPS` 块（703-726 行）改为：

```c
#ifdef CONFIG_VG_AGENT_OPS
  {
    static bool prev_active;
    const vg_alarm_t *a = vg_model_get_active_alarm();
    bool now_active = (a != NULL && a->active);

    if(now_active) {
      /* 现有拼 buf + vg_pending_alarm_write(buf) 原样保留 */
    }
    else if(prev_active) {
      (void)vg_pending_alarm_clear();
    }

    prev_active = now_active;
  }
#endif
```

为什么放这里不放 `vg_model.c`：模型层保持无 POSIX I/O（PC 模拟器也编它），文件动作全部留在板端 backend，和现有 `vg_pending_alarm_write` 同一层。

不在开机时删 pending：Agent 可能还没处理上一条。分镜第 1 步「手动删 pending」保留作彩排前的清场动作。

### B3. 可选：同点告警升级时重写 pending

同一点 WARN→CRIT 或 离线→阈值 时，pending 内容仍是旧的。若要做：backend 记 `(sensor_id, severity)`，变化时先 `clear` 再 `write`。演示序列（水浸→恢复→拔线→恢复）不触发此路径，建议跳过。

---

## C. 告警页标签诚实化（`gui/main/ui/pages/vg_page_alarm.c`）

### C1. 今天做：全部改「规则摘要」

| 行 | 现在 | 改为 |
|---|---|---|
| 278 | `lv_label_set_text(ai_head, "AI 推测")` | `"规则摘要"` |
| 286 | `【AI 推测】当前无活动告警。出现告警后将在此给出解释草稿。` | `【规则摘要】当前无活动告警。` |
| 181 | 同上 | 同上 |
| 67 | `【AI 推测】暂无告警上下文。联网后 Agent 可解释活动告警。` | `【规则摘要】暂无告警上下文。` |
| 136-139 | `【AI 推测】%s 当前 %.1f%s，阈值 %.1f%s，已持续 %d 秒。建议先核实现场与 RS485；非确定性结论。` | `【规则摘要】%s 当前 %.1f%s，阈值 %.1f%s，已持续 %d 秒。判定来自点表 cmp/阈值，本地规则引擎。` |
| 143-145 | `【AI 推测】设备离线已持续 %d 秒。建议检查供电、接线与总线占用；非确定性结论。` | `【规则摘要】连续读失败达 fail_n，离线已持续 %d 秒。` |
| 148-150 | `【AI 推测】有活动告警，但本地尚无传感器读数。确认点表或等待采集后再解释。` | `【规则摘要】有活动告警，但本地尚无该点读数。` |

去掉「非确定性结论」「建议…」这类措辞：规则摘要是确定性事实，建议/推测是 Agent 的活（V5）。用词与手册 742 行的「规则判定」（阶段 2 规则库归因）区分开，两者都不是 AI 标签。

顺手项（可选）：136/143 用 `%.1f` 依赖 `LV_SPRINTF_USE_FLOAT`，文件内已有 `fmt_f1()`，可换掉。不是本次目标。

### C2. 不做，只定形状：`last_alarm.md` 上屏（A 分支胶水，9/14 后决定）

- `vg_ui_backend_t` 加 `int (*read_last_alarm)(char *body, size_t sz);` board 读 `/data/velaguard/reports/last_alarm.md`，mock 返回 `-ENOENT`。
- 页面：读到 → 头「AI 推测」+ 文件正文；读不到 → C1 的「规则摘要」。
- 必须节流：`refresh_alarm` 每 tick 跑，不能每次 open；用 `stat` mtime 缓存或每 5 s 读一次。
- 分镜 152 行「若告警页已接 last_alarm.md」对应此项；`plan-917` 溢出砍项第 3 位。

---

## D. 文档同步（代码合入同一提交）

- `README.md:174`：「当前显示本地草稿并标「AI 推测」」→「当前显示本地规则摘要并标「规则摘要」；Agent 解释在 `last_alarm.md`」。
- `docs/demo-video-script.md:37`「每次彩排后手动删除 pending」→ 改为「恢复后固件自动删除；彩排前仍手动清一次」；128 行「若恢复逻辑未修」可删。
- `.agents/skills/velaguard-alarm-to-screen/SKILL.md`「落盘与上屏」加一行：本点恢复正常时清 `s_alarm` 并 `unlink pending_alarm.txt`。
- `implement.md` 三个复选框打勾；「板测时点一遍 DIAGNOSIS/OTA/TREND」与录屏留到 9/13。
- `docs/plan-917-submission.md:57`「pending 写入 / 恢复后删除」板测项保持。

---

## E. 验证

1. host：`app/velaguard/host_tests/test_alarm_eval.c` 加三条序列用例（编译在 `make -C app/velaguard/host_tests test`）：
   - flood：`eval(1,0,1.0)==CRIT` → `eval(1,0,0.0)==NONE`
   - offline：`eval(0,3,..)==OFFLINE` → `eval(1,0,25.0)==NONE`
   - 短暂失败不恢复：`eval(0,1,..)==NONE`（这是「离线分支不清告警」的契约，注释写明）
2. 固件：`bash scripts/build.sh`（WSL 里跑，父 openvela 树）。
3. 9/13 板测（分镜第 4 拍）：
   - 水浸置 1 → 告警页「水浸 点表严重告警」→ `ls /data/velaguard/pending_alarm.txt` 存在
   - 水浸置 0 → 告警页「无活动告警」，日志页「告警恢复: 水浸」→ `ls pending_alarm.txt` 报 no such file
   - 拔 485 → 「温度 离线」→ pending 再次出现（证明彩排后新告警能写进去）
   - 插回 → 恢复 → pending 消失
   - 重灌一次点表（`vgpoint_host_apply.ps1`）后再触发一次告警，确认 A2 不误清活动告警
4. 可选 PC 模拟器编一次 `gui/`，确认 `vg_page_alarm.c` 改动不破坏 PC 构建（改动全在共用代码）。

## F. 顺序与预算

| 步 | 内容 | 预算 |
|---|---|---|
| 1 | `/trellis:resume 09-09-demo-threshold-alarm` | — |
| 2 | B1 → B2（先做 I/O 层，`vg_model.c` 未改时 B2 永不触发，安全） | 20 min |
| 3 | A1 → A2 | 20 min |
| 4 | C1 | 15 min |
| 5 | E1 host 单测 + E2 build | 20 min |
| 6 | D 文档 | 15 min |
| 7 | trellis-check → 提交（不 push，等 9/13 板测通过） | — |

不做：A3、B3、C2、恢复窗口/回差、多活动告警。
