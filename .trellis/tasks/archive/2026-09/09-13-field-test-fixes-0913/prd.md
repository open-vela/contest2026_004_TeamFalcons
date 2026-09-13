# Fix four field-test issues: online flapping, alarm list refresh, report refresh+regen, status bar date

## Goal

Stabilize point online/offline detection (retry + cycle dedupe), make home alarm list refresh in place on recovery, add report page refresh button that pokes the agent heartbeat to regenerate a missing daily report via MiMo, and append the date after the status bar title.

## Requirements

### 1. Online/offline counts flap while all points are online (MThings simulator)

Root causes (verified):
- `vg_discover_poll_points` does one read per point, `VG_DISC_BYTE_TO_MS=20` is tighter than the alarm reader path (50 ms), no retry -> transient frame failures.
- Acq cycle is ~1-3 s but `vg_model_tick` samples the same `g_live_on` snapshot at 1 Hz, so one real failed poll is counted 2-5x in the 8-slot offline window and `fail_streak`; recovery is instant. Counts oscillate.

Fix: byte timeout 20->50 ms; one immediate retry per failed point read (reuse `VG_DISC_RETRY_INTER_MS`); add `g_live_cycle` counter bumped once per completed acq cycle; `vg_ui_backend_apply_live` applies each cycle exactly once (point-table gen check stays before the dedupe).

### 2. Home alarm list stale after a point recovers

`vg_model.c` caches the home filter index (`s_filt_idx/s_filt_n`), rebuilt only on filter switch / point-table import. Chip counts are computed live via `vg_model_count_by_filter`, so the count updates but list membership (and therefore `build_list_sig`) stays stale.

Fix: call `rebuild_filter()` in `vg_model_tick` when dirty, before `notify_all()`.

### 3. Report page refresh button + MiMo regeneration on demand

- Refactor `vg_page_report.c` read+render into `report_reload()`; header row (trend-page pattern) with a 64x36 primary button labeled 刷新.
- If no valid daily report: show generating hint, disable button, poke the agent heartbeat, poll every 2 s for up to 180 s; timeout shows a hint and re-enables the button. Delete the lv_timer on page delete; ignore repeat clicks while generating.
- Backend: new `request_daily_report` in `vg_ui_backend` (PC mock: unsupported). Board impl writes `/data/agent/HEARTBEAT.poke` (under `CONFIG_VG_AGENT_OPS`).
- Public tree `packages/ai_agent` (branch `velaguard/*`, PR to dev-ai-contest-2026 per BOUNDARY C2): heartbeat thread sleeps in 5 s slices and consumes `AGENT_HEARTBEAT_POKE_FILE` to run `heartbeat_send()` immediately. Direct `heartbeat_trigger()` call from HMI is unsafe pre-daemon-init (message bus has no init guard).

### 4. Status bar date after the title (user-confirmed option A)

`vg_shell.c`: date label right after `s_title` (before the spacer), small/muted font; `clock_cb` refreshes it every second as `|26-9-13` (%d-%d-%d, 2-digit year).

## Acceptance Criteria

- [ ] With all points online on the simulator, home 正常/离线 chips stable for 5+ min; unplugging 485 trips offline in ~10 s, plugging back recovers instantly.
- [ ] On the 告警 filter, returning one point to normal shrinks the list within 2-3 s without page switching.
- [ ] Refresh with no daily report enters the generating state and shows the new report within ~1-2 min; timeout gives a clear hint and a clickable button.
- [ ] Status bar reads VelaGuard|26-9-13 on Home (per current date); subpage titles keep their name followed by the date.

## Validation

`bash scripts/build.sh` (TARGET=velaguard-lvgl) compiles both the contest repo and `packages/ai_agent` changes. Board acceptance per `velaguard-board-inner-loop`.

## Notes

- Public-tree `packages/ai_agent` change is committed on its own `velaguard/*` branch and submitted by PR (BOUNDARY C2), never via patch files.
