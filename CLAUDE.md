@AGENTS.md

# Claude Code

The root rules are imported above. This file only adds Claude-specific workflow
instructions; it does not maintain a separate contest or product rule body.

## Agent skills

### Issue tracker

Issues and specs are local Markdown under `docs/issues/`. See `docs/agents/issue-tracker.md`.

### Triage labels

The tracker uses the five default roles: `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: `CONTEXT.md` and `docs/adr/`. See `docs/agents/domain.md`.
