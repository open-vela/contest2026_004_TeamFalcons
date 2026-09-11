# Host NSH point-table protocol (`vgpoint`)

## Scope / Trigger

Use when adding or changing `vgpoint`, host serial scripts that write the runtime point table, `points.json` fields (`cmp` / `warn` / `crit` / `fail_n`), or Agent shell allow-lists that might touch config.

Canonical text (Chinese, commands and reply lines): `docs/velaguard-host-nsh-protocol.md`. Do not fork a second command table in task PRDs.

## Contract

- Transport: ST-LINK VCP → NSH (`nsh>`), one LF-terminated command per line. Not RS485, not MQTT, not a framed binary protocol.
- Two tables: committed `/data/velaguard/config/points.json` (live poll + HMI + alarms); candidate `/data/velaguard/discover/point_table_candidate.json` (`add` / `set` / `del` / `test` only).
- Verbs: `list`, `add`, `set`, `del`, `test`, `get`, `apply --confirm`, `abort`. `apply` without `--confirm` must `ERR code=need_confirm` (exit 1), not a successful dry-run. `apply --confirm` with an existing empty candidate file must write an empty committed table (`OK n=0`); missing candidate file still `no_candidate`.
- `get` / `get <id>` reads the HMI poll snapshot (`/data/velaguard/live/values.txt`), not RS485. Must not call `vg_bus_try_lock`. Empty committed table → `OK n=0`; missing snapshot with a non-empty committed table → `ERR code=no_sample`. Stable line prefix is `vgpoint: VALUE` (not `READ`).
- Scripts parse only `vgpoint: OK`, `vgpoint: ERR`, `vgpoint: POINT`, `vgpoint: READ`, `vgpoint: VALUE`. Wait for `nsh>` before the next command.
- Host scripts must pause after `test` for a human, then send `apply --confirm` as its own line. Firmware must not auto-apply after `test`.
- Agent must not run `vgpoint`, `vgdiscover apply`, or `vgcfg commit`.
- Implementation (not this spec file): raise `CONFIG_NSH_LINELEN` to 128 and `CONFIG_NSH_MAXARGUMENTS` to 32 on `velaguard-lvgl`; command body max 120 bytes. A full `add` is ~26 tokens; NSH default 7 args is rejected before `vgpoint` runs.
- `vg_point_table_read` must heap-allocate the JSON buffer. Putting `VG_POINTS_JSON_MAX` (8K) on the NSH `vgpoint` stack plus `vg_discover_summary` overflows and panics (seen as IDLE assertion).

## Wrong vs Correct

- Wrong: treat `vgdiscover apply` dry-run (exit 0 without `--confirm`) as the `vgpoint apply` pattern.
- Wrong: live acquisition from `vg_mthings_points[]` after this protocol lands.
- Index and mutate points by `id` only. Display `name` may repeat and may be UTF-8; never look up or de-duplicate by `name`.
- Do not treat legacy JSON `tag` as `id`. A point object without a valid `id` is dropped.
- Wrong: analog alarms by matching point names (水浸 / 烟雾).
- Correct: empty committed table → empty home; missing `cmp`/`warn`/`crit` → poll still runs, no analog alarm.
- Correct: HMI live path calls `vg_alarm_eval` on the committed table (`cmp` / `fail_n`) and writes `/data/velaguard/pending_alarm.txt` once. Report page reads `/data/velaguard/reports`.

## Tests Required

- Host: parse OK/ERR lines; candidate edits must not change the committed file until `--confirm`.
- Board: COM3 script pause-then-apply; concurrent `test` vs HMI poll must not open RS485 twice. Concurrent `get` vs HMI poll must not `bus_busy`.
