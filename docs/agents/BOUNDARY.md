# Agent Boundary Note (self-reminder)

> **Audience**: AI coding agents in this workspace only.  
> **Priority**: Official contest rules **override** local docs (`CLAUDE.md`, Trellis, Superpowers, this note).  
> **Scope**: This is the single local boundary summary, not an official compliance certificate.  
> **Sources (canonical remote)**: team README + linked official docs below.  
> **Last audited**: 2026-09-01 (aligned §4 with `VelaGuard_项目手册.md` v3.1); prior 2026-08-29 (V11 board pack); 2026-07-12 against the remote sources in §1.

---

## 0. Where am I?

```text
<openvela-workspace>/                      # openvela workspace root (.repo/ present)
├── nuttx/ apps/ packages/ vendor/ ...    # PUBLIC — edit nuttx/apps/MQTT-C on VelaGuard branches; PR. No contest patches.
├── .claude/                              # official AI skills (open-vela ai-skills)
└── contest2026_004_TeamFalcons/          # OUR ONLY contest repo (Team 004)
    ├── .claude -> ../.claude             # symlink to official skills
    ├── app/ quickapp/ board/             # product code (linkfile → openvela tree)
    ├── logs/Foleaf/                      # AI Coding logs (must submit; do not gitignore)
    ├── README.md                         # must become product submission README
    ├── docs/agents/BOUNDARY.md           # THIS FILE — durable rule memory
    └── .trellis/                         # team process (not contest official law)
```

- Workspace gate: anything under a tree that has `.repo/` is “in contest workspace” for log collection.
- Build from **parent** openvela root: `./build.sh <board-config> ...` — not from contest dir alone.
- Manifest: `contest2026_004_TeamFalcons.xml` + `<linkfile>` maps contest subdirs into packages/vendor.

---

## 1. Official sources (bookmark)

| Doc | URL |
|-----|-----|
| Team README (process) | https://github.com/FoLeaf/contest2026_004_TeamFalcons/blob/dev-ai-contest-2026/README.md |
| 大赛总览 | https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/contest_overview.md |
| 代码提交指南 | https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/code_submission_guide.md |
| AI Coding 日志手册 | https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md |
| AI 硬件赛道导航 | https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_hardware/ai_hardware_guide_index.md |
| AI 硬件赛道指引 | https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_hardware/ai_hardware_track_guide.md |
| ai_agent 上手 | https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_hardware/ai_agent_quickstart.md |
| 官方 skills README | https://github.com/open-vela/.claude/blob/dev-ai-contest-2026/README_zh-cn.md |
| 大赛官网 | https://www.openvela.com/#/contest |
| CLA | https://openvela.com/#/community/cla |

Local agent index: root `CLAUDE.md`.

---

## 2. Hard contest boundaries (never violate)

| # | Rule |
|---|------|
| C1 | **Only develop contest deliverables inside `contest2026_004_TeamFalcons/`.** |
| C2 | **Product code only in** `contest2026_004_TeamFalcons/`. Public trees `nuttx/`, `apps/`, MQTT-C: **edit those git checkouts directly** on VelaGuard feature branches, then PR to `dev-ai-contest-2026` (committee review). **Do not** add or apply `scripts/openvela-*.patch` / `apply-openvela-*-patch.sh` — patches are not a development or submission path. Leave `packages/` and `vendor/` alone except via contest `<linkfile>`. |
| C3 | **GitHub only** for contest submit (not Gitee/GitCode for submission). Flow: **fork team repo → commit → PR → self-merge OK**. |
| C4 | **Deadline**: work submit by **2026-09-20**; then push revoked (clone/view still OK). |
| C5 | **AI logs**: auto-write under workspace end-of-session to `logs/<github_login>/...`; **never auto-push**. Human/agent may `git add logs/` + commit; **do not rewrite/tamper JSONL** (cheat). Delete whole session file before commit if private content. |
| C6 | **Supported AI tools for scored logs**: Claude Code, OpenCode, Codex, AIoT-IDE. Team collector also captures Grok Build and Cursor IDE as `tool: grok-build` / `tool: cursor` (honest source labels; official scoring still follows committee rules). Do not relabel Cursor/Grok sessions as `claude-code`. |
| C7 | **Apache 2.0**, original work; CLA for first upstream contrib; PR may run `cla/signature` (`/check-cla`). |
| C8 | Track: **AI 硬件产品创新**. Product must run on openvela device; not pure cloud app; not pure chat bot. |
| C9 | Track minima (evidence): firmware on hardware/sim, **≥1 custom Skill**, **≥1 proactive+execute scenario**, scenario docs. Prefer ai_agent capabilities where issues require. |
| C10 | MiMo Token: **contest use only**. Voice wake word if used: **你好 openvela / Hello openvela**. |
| C11 | Based-on-openvela bar: use openvela system capabilities; land at least one of **graphics / AI / multimedia**. |
| C12 | One team repo only: `contest2026_004_TeamFalcons`; code via subdirs + manifest `<linkfile>`. |
| C13 | Team size 1–5; one team per person. Server/backend (if any) team-owned and demo-stable. |

### Submit flow (memory)

```text
fork 专属仓 → 开发 → git commit/push → PR 回专属仓 → 自行 review 合入
logs: 会话结束自动入 logs/<login>/ → 人工 git add logs/ + commit/push
公共仓改动: 在 nuttx/apps/MQTT-C 树上直改 → commit → PR → dev-ai-contest-2026（组委会 review）
获奖后: 再 PR 上游 openvela 对应仓 dev-ai-contest-2026（标准 CI）
```

### Log collector (machine)

- Install once: `bash ../.claude/skills/contest-log-collector/onboarding/install.sh --team-id contest2026_004_TeamFalcons --github-login Foleaf`
- Verify: `bash ../.claude/skills/contest-log-collector/onboarding/verify-setup.sh`
- Identity: `~/.claude/contest-collector.env` (`TEAM_ID`, `GITHUB_LOGIN=Foleaf`)
- Optional list: `contest-snapshot --list`
- Gate: only under openvela workspace (parent has `.repo/`)

---

## 3. AI hardware track minima (evidence over time)

From official track guide — **not optional for final track bar**:

1. Agent/app **runs on hardware or supported board config** (not PC-only sidecar).
2. **≥1 custom Skill** with demo (ai_agent skills under device path such as `/data/agent/skills/` when using that stack).
3. **≥1 proactive + execute** scenario (timer / threshold / event / context-driven — not only Q&A).
4. Written scenario: user story + feature list + which openvela/ai_agent capabilities used.

**Out of track**: pure cloud app; pure chatbot with no proactivity/tools.

Primary pattern for VelaGuard: **ai_agent + LVGL** on device (handbook §14.3). Official track also allows cloud LLM + device protocols (e.g. ai_chat style) — not our main path.

VelaGuard contest evidence (handbook §14.3): `alarm_interpretation.md` + `operations_report.md` Skills; **event-driven** alarm interpretation + **scheduled** daily report as proactive+execute scenarios; NL data query as supplementary interaction channel (CLI/LVGL).

---

## 4. VelaGuard product boundaries (local, still binding)

> Aligned with **`VelaGuard_项目手册.md` v3.1** (2026-08-30). Product detail, acceptance, and architecture decisions live in the handbook; this table is the agent-facing summary.

| # | Rule |
|---|------|
| V1 | Independent gateway on **STM32H750B-DK** — not a long-running PC sidecar demo. USB CDC / UART are debug/rescue only. |
| V2 | **Local deterministic loop** works offline: Modbus acquisition, frame stats, rule-engine alarms, UI (LED + screen), config/event logs, safe digital-output defaults. Continues without network, MQTT, MiMo, Agent, OTA, or a connected PC. **No ASR/TTS/voice** (removed). |
| V3 | **Board runs openvela `ai_agent`** (`packages/ai_agent`): ReAct + custom **read-only** C tools + Markdown Skills under `/data/agent/skills/`. Agent is an **operations assistant** — alarm interpretation, daily/weekly reports, NL data query — **not** bus fault triage, Modbus writes, or probe experiments. |
| V4 | **LLM via HTTPS** (OpenAI-compatible): board **direct MiMo** through ai_agent/`llm_proxy`. MiMo key **encrypted on eMMC** (`vgprovision`); never in firmware image. **MQTT** = telemetry / alarm / OTA only — not AI request/response. |
| V5 | **Agent cannot mutate plant state**: no register writes, no config apply, no alarm clear, no DO control. Point-table / config changes require deterministic test-read + **Local Confirmation** on device. Agent output is schema-validated and shown as「AI 推测」. |
| V6 | **Bus discovery is deterministic** (stage 1: NSH `vgdiscover` @9600). LVGL「Scan Bus」/「启用总线扫描」**default off**; user must enable before scan. Agent does not participate in scanning. |
| V7 | OTA = **MQTT-only** chunk pull to **eMMC**; QSPI burn via on-chip boot stub; no board-side HTTPS firmware downloader. |
| V8 | Implement **one issue at a time** under `.scratch/.../issues/`; respect `Blocked by`. |
| V9 | Canonical name **VelaGuard**. Product handbook: `VelaGuard_项目手册.md`; glossary: `CONTEXT.md`. |
| V10 | Do not pull deferred scope (prod OTA, stage-2 diag rules, Bridge prod, ASR/TTS, manual-parse, screen config editing) into issues unless the issue says so. |
| V11 | **Board circuitry → local pack**: when work depends on the real STM32H750B-DK board circuit (drivers, pinmux, peripheral bring-up, expansion wiring, pin/net claims), consult hardware docs from the local official pack — `F:\Project\Embeded\H750B-DK\BOARD INFO\H750B-DK` (WSL: `/mnt/f/Project/Embeded/H750B-DK/BOARD INFO/H750B-DK`). Priority: **schematic PDF / SchDoc > ST BSP under `bsp/` > ST UM / data brief**. Files named `*_unofficial*` are search aids only; **cross-check** before coding. Non-hardware work need not open this pack. Do not use web-scraped UM / third-party pinmux alone when this pack is available. |

---

## 5. Doc / rule stack (do NOT collapse into one file)

| Layer | Path | Owner / role |
|-------|------|--------------|
| Official contest process | open-vela docs (URLs in §1) | Committee — canonical |
| Team README (submission) | `README.md` | Judges — product description |
| Product handbook / plan | `VelaGuard_项目手册.md` v3.1, `VelaGuard_推进方案.md` v3 | Product requirements |
| Domain glossary | `CONTEXT.md` | Language contracts |
| Agent onboarding | `CLAUDE.md` | How agents work here (index) |
| Agent contest boundary | `docs/agents/BOUNDARY.md` | Local summary; official sources override it |
| Board hardware pack (local) | `F:\Project\Embeded\H750B-DK\BOARD INFO\H750B-DK` | Use when board circuitry is involved (V11) |
| ADR | `docs/adr/` | Decisions |
| Issue workflow | `docs/agents/*`, `.scratch/...` | Local backlog |
| Trellis | `.trellis/` | Team AI workflow — not contest law |
| Official skills | `.claude` → `../.claude` | openvela AI skills — do not edit root-owned skills casually |

**Unsafe**: treating this summary as official law; duplicating the rule body across README/CLAUDE/Trellis; allowing a local copy to drift without re-auditing the sources in §1.

---

## 6. Pre-development checklist (official path)

| Step | Status on clean restart |
|------|-------------------------|
| repo init/sync `dev-ai-contest-2026` | done |
| Develop only under team repo | enforced by process |
| Official skills available (`.claude`) | symlink done |
| Log collector install + verify | done (`Foleaf`) |
| Replace example logs with real `logs/<login>/` | done (`logs/Foleaf/`) |
| `.gitignore` ignores build junk, **not** `logs/` | done |
| README becomes product description | **before final submit** |
| CLA if first upstream PR | as needed |
| Track evidence: Skill + proactive + device demo | during product issues |

---

## 7. Working loop (every non-trivial turn)

1. Confirm work path: product code under **contest repo**; nuttx/apps/MQTT-C edits in those checkouts (then public-repo PR). Never contest `.patch` files.
2. If the work depends on **real board circuitry** (drivers, pinmux, peripheral bring-up, etc.): open **BOUNDARY V11** local pack (schematic > BSP > UM); do not rely on `*_unofficial*` alone.
3. If product scope: confirm against **`VelaGuard_项目手册.md`** (handbook wins over this summary for product detail).
4. If product code: current issue in `.scratch/.../issues/` + ADR/CONTEXT if architecture.
5. If process/skill: official `.claude/skills/` + Trellis only as team process.
6. Validate AI logs with the official tool: `python3 ../.claude/skills/contest-log-collector/tools/validate-log.py logs/`.
7. Manually review the applicable official requirements and evidence checklist below; there is no local one-command contest compliance gate.
8. Logs: leave under `logs/`; human push; never rewrite JSONL bodies.

### Manual pre-PR / submission evidence

- Work and manifest mappings stay inside the team repository; public-tree changes use the required public-repo PR flow.
- README is a product description with reproducible build/run instructions, not the committee template.
- Required external deliverables are ready: work description, demo video no longer than five minutes, and repository address.
- AI hardware evidence exists: device build/run; Skills `alarm_interpretation.md` + `operations_report.md`; proactive scenarios = event-driven alarm interpretation + scheduled daily report; written user story/features/technical capabilities.
- Apache 2.0/original-work obligations, conditional CLA, MiMo Token use, and server/demo stability have been reviewed where applicable.
- Required code, artifacts and validated AI Coding logs have been committed and pushed before the deadline.

---

## 8. Scoring awareness (do not game; build evidence)

| Axis | Weight (overview) |
|------|-------------------|
| 技术难度 | 30 |
| 产品创新性 | 20 |
| 项目完整度 | 20 |
| AI 开发 | 10 |
| 商业潜力 | 10 |
| 展示效果 | 10 |

Judges use repository evidence together with the required work description and demo video. README and AI Coding log clarity matter.

---

## 9. One-line mantra

**Contest rules win. Product in contest tree. Public trees: direct edit + PR (no patches). Logs honest. Prove on device. One issue at a time.**
