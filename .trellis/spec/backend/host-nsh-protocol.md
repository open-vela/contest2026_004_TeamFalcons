# Host NSH point-table protocol (`vgpoint`)

## Scope / Trigger

Use when adding or changing `vgpoint`, host serial scripts that write the runtime point table, `points.json` fields (`cmp` / `warn` / `crit` / `fail_n`), or Agent shell allow-lists that might touch config.

Canonical text (Chinese, commands and reply lines): `docs/velaguard-host-nsh-protocol.md`. Do not fork a second command table in task PRDs.

## Contract

- Transport: ST-LINK VCP → NSH (`nsh>`), one LF-terminated command per line. Not RS485, not MQTT, not a framed binary protocol.
- Two tables: committed `/data/velaguard/config/points.json` (live poll + HMI + alarms); candidate `/data/velaguard/discover/point_table_candidate.json` (`add` / `set` / `del` / `test` only).
- Verbs: `list`, `add`, `set`, `del`, `test`, `get`, `apply --confirm`, `abort`. `apply` without `--confirm` must `ERR code=need_confirm` (exit 1), not a successful dry-run. `apply --confirm` with an existing empty candidate file must write an empty committed table (`OK n=0`); missing candidate file still `no_candidate`. Successful confirm also calls `vg_mqtt_notify_point_table` so the dashboard retained snapshot can update; empty tables are not published.
- `get` / `get <id>` reads the HMI poll snapshot (`/data/velaguard/live/values.txt`), not RS485. Must not call `vg_bus_try_lock`. Empty committed table → `OK n=0`; missing snapshot with a non-empty committed table → `ERR code=no_sample`. Stable line prefix is `vgpoint: VALUE` (not `READ`).
- Scripts parse only `vgpoint: OK`, `vgpoint: ERR`, `vgpoint: POINT`, `vgpoint: READ`, `vgpoint: VALUE`. Wait for `nsh>` before the next command.
- Host scripts must pause after `test` for a human, then send `apply --confirm` as its own line. Firmware must not auto-apply after `test`.
- Agent must not run `vgpoint`, `vgdiscover apply`, or `vgcfg commit`.
- The agent's read-only data tools are `vg_point_read` / `vg_run_report`, registered from `app/velaguard/vg_agent_tools.c` through the `ai_agent` provider hook. They read only the committed table + live snapshot and the runtime report; neither opens RS485 nor writes a file. A provider executor must return `OK` for every tool name it owns, **including** error answers: `tool_registry_execute` reads a provider's `ERROR` as "not my tool", skips to the next provider and finally overwrites the buffer with `unknown tool`, which tells the model the tool does not exist and makes it retry instead of reporting the real fault.
- Board diagnostics for those tools: `vgagent tools` prints the definitions actually handed to the model (a provider whose JSON does not parse is dropped silently, which from outside looks identical to the model choosing not to call anything), and `vgagent tool <name> [args]` runs one tool through the same guard and audit path the ReAct loop uses. Both are read-only, and they exist so the tool layer can be verified without a reachable model backend. `[args]` may be a bare word because NSH strips the quotes JSON needs: `{"query":"ups_load"}` arrives as `{query:ups_load}` and fails to parse.
- A command sent while the console sits at `vela>` (after `ai_agent` attaches) answers `Unknown command`. A script that attaches must `quit` before its next NSH command, including one that only asks questions; otherwise two error strings get compared and the result reads as a pass.
- `agent_tools.log` survives across boots. A check that attributes a tool call to the model must clear it, or record its offset, before the round under test — the entries otherwise belong to an earlier session.
- Implementation (not this spec file): raise `CONFIG_NSH_LINELEN` to 128 and `CONFIG_NSH_MAXARGUMENTS` to 32 on `velaguard-lvgl`; command body max 120 bytes. A full `add` is ~26 tokens; NSH default 7 args is rejected before `vgpoint` runs.
- `vg_point_table_read` must heap-allocate the JSON buffer. Putting `VG_POINTS_JSON_MAX` (8K) on the NSH `vgpoint` stack plus `vg_discover_summary` overflows and panics (seen as IDLE assertion).

### Storage errors must stay visible

The store is `/data` (a symlink to the eMMC volume). Everything below it can vanish between boots, so "the table is empty" and "the store is gone" must never produce the same wire line.

- `vg_point_table_read` returns `-ENOENT` for a missing file **and for `ENOTDIR`**, `-EINVAL` when the file opens but carries no `"points"` array, `-EIO` on a mid-read failure (`ferror`), `-EFBIG` past `VG_POINTS_JSON_MAX`, otherwise `-errno`.
- `ENOTDIR` must be collapsed into "no table yet". NuttX returns `ENOTDIR`, not `ENOENT`, when a path component is missing (`cat /data/velaguard/nodir/x` → errno 20), while the desktop libc used by the host tests returns `ENOENT` for the same situation. Treating it as an I/O error made one lost directory fatal: `list -c` answered `eio`, and `ensure_candidate` returned before its rebuild branch, so every `add` failed and nothing ever recreated the tree (2026-09-17). The path-component-is-a-plain-file form is the only way to produce `ENOTDIR` on the host, so that is what the regression test uses.
- `mkdir_p` must `stat` after `EEXIST` and return `-ENOTDIR` when the name is taken by a plain file. Without that check the later `fopen` reports a bare `write_fail` that names nothing.
- Missing table counts as empty only while the store root is reachable. `vg_point_store_root_ok(root)` returns `-ENOENT` when the path does not resolve (dangling `/data`, eMMC not mounted) and `-ENODEV` when `statfs().f_type == PROC_SUPER_MAGIC` — NuttX accepts `mkdir()` and file creation on the pseudo filesystem, so a store that fell back to RAM answers every write until reboot.
- `vg_point_table_ensure_candidate` may seed from the committed table or start empty **only** on `-ENOENT`. Any other read error returns immediately: resetting to an empty table there lets the next `apply --confirm` overwrite the committed points.
- `cmd_list` / `cmd_get` reply `ERR code=io msg=<token>` for anything but "missing file with a healthy store root"; `cmd_add` / `cmd_set` / `cmd_del` use the same token in `msg` in place of the old literal `candidate_io`. `code` values do not change.
- Never report success for a write that did not land: check `ferror` / `fclose`, and `unlink` the half-written file. A truncated table reads back as a smaller valid table and can be confirmed over the real one.

## Wrong vs Correct

- Wrong: treat `vgdiscover apply` dry-run (exit 0 without `--confirm`) as the `vgpoint apply` pattern.
- Wrong: live acquisition from `vg_mthings_points[]` after this protocol lands.
- Index and mutate points by `id` only. Display `name` may repeat and may be UTF-8; never look up or de-duplicate by `name`.
- Do not treat legacy JSON `tag` as `id`. A point object without a valid `id` is dropped.
- Wrong: analog alarms by matching point names (水浸 / 烟雾).
- Correct: empty committed table → empty home; missing `cmp`/`warn`/`crit` → poll still runs, no analog alarm.
- Correct: HMI live path calls `vg_alarm_eval` on the committed table (`cmp` / `fail_n`) and writes `/data/velaguard/pending_alarm.txt` once. Report page reads `/data/velaguard/reports`.
- Wrong: `if (read(...) != 0) memset(&sum, 0, ...)` and then report `OK n=0`. That is how a dead eMMC was mistaken for an empty table (2026-09-15: every `vgpoint add` answered `ERR code=io msg=candidate_io` while `list` cheerfully said `n=0`).
- Wrong: `mkdir_p("/data/velaguard/discover")` hard-coded inside the writer, ignoring the return value, while the caller passes a path under `/data/velaguard/config`. Derive the parent from the path actually handed in (`mkdir_parent(out)`); treat an existing leaf as success, not as `-EEXIST`.
- Wrong: propagate every `fopen` errno straight out of `vg_point_table_read`. "The file is not there" and "the directory that holds it is gone" both mean "build the table", and only the caller can act on that; an `ENOTDIR` passed through as-is reached the wire as an unrecognised `eio` and stopped `ensure_candidate` from ever recreating the directory.
- Correct: `/data` is a symlink the board re-creates at every boot. A missing eMMC leaves it dangling rather than absent, so "the file is not there" is not by itself evidence that the table is empty.

## Tests Required

- Host: parse OK/ERR lines; candidate edits must not change the committed file until `--confirm`.
- Host (`host_tests/test_vgpoint.c`, asserts on the exact return value): a file without `"points"` reads as `-EINVAL`, not as an empty table; `ensure_candidate` returns `-EINVAL` for a corrupt candidate and leaves the committed path untouched; `vg_point_store_root_ok` is `< 0` for a missing root, `0` for a plain directory, `-ENODEV` for a RAM-backed one (`/proc` on the desktop); a write that fails mid-stream returns `-EIO` and leaves no file behind; a path component that is a plain file (the host's `ENOTDIR`) reads as `-ENOENT` and has its own token; a missing directory tree is rebuilt by the next write. Verify each by reverting the corresponding branch — the assertions must fail.
- Board: COM3 script pause-then-apply; concurrent `test` vs HMI poll must not open RS485 twice. Concurrent `get` vs HMI poll must not `bus_busy`.
