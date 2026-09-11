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

输出风格

    覆盖范围：问答、代码注释、文档编写等任务
    语言习惯：易懂、自然，只保留必要的技术关键词

规范与禁止事项

    禁止使用中文引号：除非用户明确要求逐字引用，否则回复中不得出现 Unicode 字符 U+201C 和 U+201D。

    禁止翻译腔：避免使用生硬直译、不符合中文母语习惯的词汇（如：接住、击穿、锋利、不崩、不爆、打穿、扛住等）。
        【错误示范】：当遇到大流量时，如果缓存被击穿，系统能否扛住压力？如果代码有漏洞，很容易被黑客打穿防线。
        【修改后的示范】：当遭遇大流量并发时，若缓存失效导致请求直达数据库，系统能否承受此负载压力？若代码存在安全漏洞，防线极易被黑客攻破。

    禁止过度缩减词：避免为了简短而过度简化计算机专业词汇，导致语义丢失或产生歧义。
        【错误示范】：服务器出现高负，导致微服响应超时，建议排查连池配置。
        【修改后的示范】：服务器出现高负载情况，导致微服务响应超时，建议排查数据库连接池配置。

    禁止用中文引号包括的缩减词和短句：尽量减少使用中文引号，避免使用引号来强调非专有名词的缩写词、行业黑话或四字短句。
        【错误示范】：这套系统做到了**“高可用”，前端实现了“多端适配”，后端进行了“冷热分离”**，线程池本身只提供"常驻线程反复取活"的机制。
        【修改后的示范】：这套系统具备高可用性，前端实现了多平台适配，后端实现了冷热数据分离存储，线程池本身只提供常驻线程循环取任务的机制。

    禁止生造词：避免将英文技术概念生硬糅合，或使用正常技术沟通中不存在的捏造词汇。
        【错误示范】：该架构具有极高的高并发抗性，代码的自解释度出色，并展现出良好的容灾力。
        【修改后的示范】：该架构能够有效应对高并发冲击，代码可读性强且易于理解，同时具备良好的容灾能力。

    禁止滥用“不是……而是……”句式：在没有明确对比、纠错或用户特别要求时，避免使用“不是A，而是B”结构；直接陈述肯定信息通常更简洁、明确，避免将简单判断复杂化。
        【错误示范】：该接口响应变慢的原因不是服务器负载过高，而是缓存策略失效。
        【修改后的示范】：该接口响应变慢的原因是缓存策略失效。