# CLAUDE.md — Instructions for Claude Code

This file is read by Claude Code at the start of every session in this repo.
Treat the rules below as standing orders unless the user overrides them in chat.

## Project

NeuroStream — a behavioral simulator for SSD↔NPU co-design that streams AI
model weights into a console-class system without disturbing audio, textures,
or input. See `PROJECT_PLAN.md` for the phase roadmap and
`Design_Detail.md` for locked design decisions.

## Working agreements

### 1. Design-decision logging (mandatory)

Whenever a design decision for any phase is finalized in conversation, append
it to `Design_Detail.md` **before** writing the corresponding code.

Each entry must follow this structure:

```markdown
## Phase N — <Phase Name>

### Decision: <short title>

- **Choice:** <what was chosen>
- **Reason:** <why — link to constraints, KPIs, or prior decisions>
- **Alternatives considered:**
  - <option A> — rejected because …
  - <option B> — rejected because …
- **Implications:** <what this forces or forbids downstream>
- **Date:** YYYY-MM-DD
```

Rules:
- One entry per decision, not one per conversation.
- If a later decision overrides an earlier one, do not delete the old entry —
  add a new entry that references it (`Supersedes: Phase N / <title>`) and
  mark the old entry with `> **Status:** superseded by …`.
- Group entries under the phase they belong to. Cross-cutting foundational
  decisions go under `Phase -1 — Foundation`.
- Keep each entry tight — bullets, not paragraphs. The goal is fast review,
  not prose.

### 2. Plan synchronization

When a phase's status changes (`[ ]` → `[~]` → `[x]`), update
`PROJECT_PLAN.md` in the same commit as the code change.

### 3. Scope discipline

Non-goals listed in `PROJECT_PLAN.md` are hard limits. If a task seems to
require crossing one, stop and ask before proceeding.

### 4. Language and dependencies

- C++20 for the simulator. CMake build.
- `yaml-cpp` for YAML parsing (scenarios + config).
- `doctest` for unit tests.
- No Boost, no SystemC, no Python in the build path.

### 5. Commit hygiene

- One logical change per commit.
- Commit message: imperative subject under 70 chars, body explains *why*.
- Never bypass hooks or signing without explicit user request.
