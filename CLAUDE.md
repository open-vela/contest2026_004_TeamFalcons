@AGENTS.md

# Claude Code

The root rules are imported above. This file only adds Claude-specific workflow
instructions; it does not maintain a separate contest or product rule body.

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
