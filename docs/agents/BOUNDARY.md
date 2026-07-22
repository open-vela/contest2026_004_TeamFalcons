# Agent Boundary Note (self-reminder)

> **Audience**: AI coding agents in this workspace only.  
> **Priority**: Official contest rules **override** local docs (`CLAUDE.md`, Trellis, Superpowers, this note).  
> **Scope**: This is the single local boundary summary, not an official compliance certificate.  
> **Sources (canonical remote)**: team README + linked official docs below.  
> **Last audited**: 2026-07-12 against the remote sources in §1.

---

## 0. Where am I?

```text
/home/debian19y/openvela/                 # openvela workspace root (.repo/ present)
├── nuttx/ apps/ packages/ vendor/ ...    # PUBLIC — do not edit here for contest work
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
| C2 | **Zero normal edits to production trees** `nuttx/`, `apps/`, `packages/`, `vendor/`. Need public change → fork that public repo → PR to `dev-ai-contest-2026` (committee review). Local temp patches only if scripted in contest repo and not treated as the submission path. |
| C3 | **GitHub only** for contest submit (not Gitee/GitCode for submission). Flow: **fork team repo → commit → PR → self-merge OK**. |
| C4 | **Deadline**: work submit by **2026-09-20**; then push revoked (clone/view still OK). |
| C5 | **AI logs**: auto-write under workspace end-of-session to `logs/<github_login>/...`; **never auto-push**. Human/agent may `git add logs/` + commit; **do not rewrite/tamper JSONL** (cheat). Delete whole session file before commit if private content. |
| C6 | **Supported AI tools for scored logs**: Claude Code, OpenCode, Codex, AIoT-IDE. Not Cursor / ChatGPT web / raw API. |
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
公共仓改动: fork 对应仓 → PR → dev-ai-contest-2026（组委会 review）
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

Two allowed patterns (non-exclusive): cloud LLM + device protocols (e.g. ai_chat style); or ai_agent + LVGL/quickapp.

---

## 4. VelaGuard product boundaries (local, still binding)

| # | Rule |
|---|------|
| V1 | Independent gateway on **STM32H750B-DK** — not a long-running PC sidecar demo. |
| V2 | **Local Safety Loop** must work offline (no network/MQTT/MiMo/TTS/ASR/OTA/PC). |
| V3 | Board talks **MQTT Broker only**; **no direct MiMo HTTPS** on device. |
| V4 | **AI Bridge** is separate cloud service (MQTT in/out, HTTPS to MiMo/etc.). |
| V5 | AI **Candidate Config** never auto-applies: schema + risk + test read + **Local Confirmation**. |
| V6 | OTA = **MQTT-only** chunk pull; no board-side HTTPS firmware downloader. |
| V7 | Implement **one issue at a time** under `.scratch/.../issues/`; respect `Blocked by`. |
| V8 | Canonical name **VelaGuard**. Glossary: root `CONTEXT.md`. |
| V9 | Do not pull OTA / NL-config / TTS / manual-parse into early issues unless the issue says so. |
| V10 | Clean restart (2026-07-12): issues 01–16 text kept, all incomplete until re-accepted on this tree. |

---

## 5. Doc / rule stack (do NOT collapse into one file)

| Layer | Path | Owner / role |
|-------|------|--------------|
| Official contest process | open-vela docs (URLs in §1) | Committee — canonical |
| Team README (submission) | `README.md` | Judges — product description |
| Product handbook / plan | `VelaGuard_*.md` | Product requirements |
| Domain glossary | `CONTEXT.md` | Language contracts |
| Agent onboarding | `CLAUDE.md` | How agents work here (index) |
| Agent contest boundary | `docs/agents/BOUNDARY.md` | Local summary; official sources override it |
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

1. Confirm work path is under **contest repo** (or explicit public-fork PR path).
2. If product code: current issue in `.scratch/.../issues/` + ADR/CONTEXT if architecture.
3. If process/skill: official `.claude/skills/` + Trellis only as team process.
4. Validate AI logs with the official tool: `python3 ../.claude/skills/contest-log-collector/tools/validate-log.py logs/`.
5. Manually review the applicable official requirements and evidence checklist below; there is no local one-command contest compliance gate.
6. Logs: leave under `logs/`; human push; never rewrite JSONL bodies.

### Manual pre-PR / submission evidence

- Work and manifest mappings stay inside the team repository; public-tree changes use the required public-repo PR flow.
- README is a product description with reproducible build/run instructions, not the committee template.
- Required external deliverables are ready: work description, demo video no longer than five minutes, and repository address.
- AI hardware evidence exists: device build/run, at least one custom Skill with demo, at least one proactive+execute scenario, and written user story/features/technical capabilities.
- Apache 2.0/original-work obligations, conditional CLA, MiMo Token use, voice wake word, and server/demo stability have been reviewed where applicable.
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

**Contest rules win. Contest tree only. Logs honest. Public trees via PR. Prove on device. One issue at a time.**
