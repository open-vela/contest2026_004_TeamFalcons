# CLAUDE.md — Agent entry

> Official contest rules override every local document and workflow.

Before doing contest work in this repository, read:

- `docs/agents/BOUNDARY.md` — the single local contest and product boundary reference
- `CONTEXT.md` — VelaGuard terminology
- the current issue under `.scratch/velaguard-independent-edge-ai-gateway/issues/`

Do not duplicate contest rules in this file. Build from the parent openvela workspace. Keep contest product code inside `contest2026_004_TeamFalcons/`. Edit `nuttx/` / `apps/` / MQTT-C in those checkouts (no contest patches); see `BOUNDARY.md` C2. Use the official log validator documented in `BOUNDARY.md`.

## Development workflow

This repository uses Trellis as its only planning, implementation, checking,
and finish lifecycle. Claude Code loads the current `.trellis/` context
automatically through project hooks.

- Use `/trellis:resume <task>` to attach this Claude session to an existing
  task without changing its status.
- Use `/trellis:continue` to continue the task already selected in this
  session.
- Use `/trellis:finish-work` only after the workflow quality and commit gates
  have passed.
- Read `.trellis/workflow.md` when detailed phase routing is needed.

There is intentionally no `/trellis:start` command on Claude Code: the native
`SessionStart` hook performs session initialization automatically.
