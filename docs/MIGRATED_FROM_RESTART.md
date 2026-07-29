# Docs migrated from feat/velaguard-restart (reference only)

These files were copied from `fork/feat/velaguard-restart` (and the first-led
babysitter from `fork/learn/openvela-dev`) onto the clean official scaffold
branch `dev-ai-contest-2026`.

## Intent

- Keep **contest rules**, **agent boundaries**, and **VelaGuard product vision**
  without bringing back the large vibecoded `app/velaguard_app` tree.
- Use this tree for **learn-first R1**: official hello app + clean nuttx/apps.
- Product code, if rewritten later, should be new and human-owned.

## What was migrated

| Path | Role |
|------|------|
| `CLAUDE.md` | Contest rules summary for agents |
| `CONTEXT.md` | VelaGuard domain glossary |
| `AGENTS.md` | Agent-facing notes |
| `VelaGuard_项目手册.md` | Product handbook (vision) |
| `VelaGuard_推进方案.md` | Old delivery plan (historical) |
| `docs/agents/BOUNDARY.md` | Hard contest boundaries |
| `docs/agents/domain.md` | Domain notes |
| `docs/adr/*` | Architecture decision records |
| `docs/velaguard-expansion-board.md` | Hardware contract |
| `docs/velaguard-mqtt-contract.md` | MQTT contract |
| `docs/windows_build_debug_setup.md` | Windows/WSL build-debug notes |
| `docs/windows_vscode_coding_setup.md` | Remote-WSL IntelliSense / coding env |
| `docs/learn/*babysitter*` | First-led learning guide |
| `scripts/apply-openvela-*.sh` + patches | Offline reference; **do not apply** until needed |
| `scripts/windows_*.ps1` | Optional host tooling |
| `scripts/prepare_wsl_intellisense.sh` | Generate NuttX headers for C/C++ IntelliSense |
| `.vscode/*` (tracked subset) | Shared VS Code coding + scaffold build/flash tasks |

## What was intentionally NOT migrated

- `app/velaguard_app/**` (product firmware)
- `.trellis/**` task dumps / full process runtime
- `.scratch/**` issue backlog copies
- `harness/velaguard_*.py` product checks
- Full `.claude` / `.agents` trellis mirrors (use openvela root `.claude` skills)

## Source pins

- Docs/scripts: `fork/feat/velaguard-restart` @ archive era (see tag
  `archive/velaguard-v0-before-clean-repo` and later journal commits)
- Babysitter: `fork/learn/openvela-dev`

