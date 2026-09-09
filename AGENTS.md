<!-- TRELLIS:START -->
# Trellis Instructions

These instructions are for AI assistants working in this project.

This project is managed by Trellis. The working knowledge you need lives under `.trellis/`:

- `.trellis/workflow.md` — development phases, when to create tasks, skill routing
- `.trellis/spec/` — package- and layer-scoped coding guidelines (read before writing code in a given layer)
- `.trellis/workspace/` — per-developer journals and session traces
- `.trellis/tasks/` — active and archived tasks (PRDs, research, jsonl context)

If a Trellis command is available on your platform (e.g. `/trellis:finish-work`, `/trellis:continue`), prefer it over manual steps. Not every platform exposes every command.

If you're using Codex or another agent-capable tool, additional project-scoped helpers may live in:
- `.agents/skills/` — reusable Trellis skills
- `.codex/agents/` — optional custom subagents

Project-local skills (also mirrored under `.cursor/skills/` for Cursor):
- `mthings-automation-config-skill` — generate or modify MThings `.mthings` from Modbus/S7/DL/T645/CJ/T188/DL/T698.45 point tables; trigger when importing registers, building SCADA pages/widgets, or validating `.mthings` XML.

Managed by Trellis. Edits outside this block are preserved; edits inside may be overwritten by a future `trellis update`.

<!-- TRELLIS:END -->

# Project constraints (agents)

Official contest rules override every local document. Durable local boundary: `docs/agents/BOUNDARY.md`. Terminology: `CONTEXT.md`.

## When board circuitry is involved (BOUNDARY V11)

**Trigger**: feature work that depends on the real development-board circuit — e.g. device drivers, pinmux, SDMMC/UART/SPI/I2C/ETH bring-up, expansion-board wiring, or any claim about which MCU pin/net a peripheral uses.

**Then**: hardware materials must be taken from the local official board pack (not web-only UM scrapes or third-party pinmux notes as sole evidence):

- Windows: `F:\Project\Embeded\H750B-DK\BOARD INFO\H750B-DK`
- WSL: `/mnt/f/Project/Embeded/H750B-DK/BOARD INFO/H750B-DK`

Priority inside that pack: **schematic PDF / SchDoc > ST BSP under `bsp/` > ST UM / data brief**.

Files whose names contain `unofficial` are working notes only — useful for search; **cross-check** against schematic and/or BSP before coding. Non-hardware work (app logic, MQTT, Agent prompts, docs-only) does not require opening this pack.

## Public trees (nuttx / apps / MQTT-C)

Edit the sibling openvela git checkouts **directly**. Do **not** create, apply, or keep using `scripts/openvela-*.patch` / `scripts/apply-openvela-*-patch.sh`.

| Tree | Typical local branch | Belongs here |
|------|----------------------|--------------|
| `../nuttx` | `velaguard/*` | board pinmux, drivers, defconfig, LTDC/FT5x06 |
| `../apps` | `velaguard/netinit-esp8266` | netinit / ESP8266 compat |
| MQTT-C | `velaguard/mqtt-pal-hook` | pal send/recv hooks |

Contest repo holds product firmware (`app/`, `gui/`, `scripts/build.sh`). `scripts/configs/*.defconfig` may be copied onto the nuttx board config at build time — that installs a tree file, it is not a patch.

`build.sh` only **verifies** those trees already contain VelaGuard changes. Submit public-tree work by committing on that repo’s feature branch and opening a PR to `dev-ai-contest-2026`. Canonical text: `docs/agents/BOUNDARY.md` C2.
