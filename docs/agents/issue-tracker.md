# Issue tracker: Local Markdown

Issues and specs for this repo live as Markdown files under `docs/issues/`. The repository rules reserve `.scratch/.../issues/` for historical reference, so active work does not use that directory.

## Conventions

- One feature per directory: `docs/issues/<feature-slug>/`
- The spec is `docs/issues/<feature-slug>/spec.md`
- Implementation tickets are one file per ticket at `docs/issues/<feature-slug>/issues/<NN>-<slug>.md`, numbered from `01` in dependency order
- Triage state is recorded as a `Status:` line near the top of each file, using the strings in `docs/agents/triage-labels.md`
- Comments and conversation history append to the bottom of the file under a `## Comments` heading
- A ticket is unblocked when every ticket named in its `Blocked by` line is done

## When a skill says "publish to the issue tracker"

Create the spec or ticket file under `docs/issues/<feature-slug>/`, creating the directory if needed.

## When a skill says "fetch the relevant ticket"

Read the file at the referenced path. The user will normally pass the path or the ticket number directly.

## Wayfinding operations

Used by `/wayfinder`. The **map** is a file with one **child** file per ticket.

- **Map**: `docs/issues/<effort>/map.md` with the Notes, Decisions-so-far and Fog body
- **Child ticket**: `docs/issues/<effort>/issues/NN-<slug>.md`, numbered from `01`, with the question in the body. A `Type:` line records the ticket type (`research`/`prototype`/`grilling`/`task`); a `Status:` line records `claimed`/`resolved`
- **Blocking**: a `Blocked by: NN, NN` line near the top. A ticket is unblocked when every ticket in that line is resolved
- **Frontier**: scan `docs/issues/<effort>/issues/` for files that are open, unblocked and unclaimed; first by number wins
- **Claim**: set `Status: claimed` and save before any work
- **Resolve**: append the answer under an `## Answer` heading, set `Status: resolved`, then append a context pointer to the map's Decisions-so-far in `map.md`
