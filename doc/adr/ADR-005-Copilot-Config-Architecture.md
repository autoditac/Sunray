# ADR-005 — Copilot Agent, Skill, and Instruction Architecture

| Field | Value |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-04-16 |
| **Author** | Rouven Sacha |
| **Related ADR** | [ADR-001 (devops-awesome)](https://github.com/user/devops-awesome/blob/main/docs/architecture_decision_records/ADR001-Skill-and-Agent-Grouping-Architecture.md) |

---

## Context

The Sunray project involves multiple distinct workflows: firmware development (C++/CMake), Docker builds and deployment, GitHub PR management, and documentation. Each requires different tools, knowledge, and constraints.

GitHub Copilot supports `.github/agents/`, `.github/skills/`, and `.github/instructions/` to provide context-aware AI assistance. We adapted the three-layer architecture from the devops-awesome project (ADR-001) to fit Sunray's needs.

### Options considered

| Option | Pros | Cons |
|---|---|---|
| **No Copilot config** | Zero overhead | AI lacks project context, gives generic advice |
| **Single instruction file** | Simple | One-size-fits-all, too long, always injected |
| **Full agent/skill/instruction architecture** | Targeted context, specialist agents, auto-injection | More files to maintain |

## Decision

Adopt the **three-layer architecture** from devops-awesome, scaled to Sunray's scope:

### Layer 1 — Instructions (auto-injected by file pattern)

| File | Applies to | Purpose |
|---|---|---|
| `sunray-firmware.instructions.md` | `*.cpp, *.h, CMakeLists.txt, configs/**` | Architecture, build system, Alfred hardware, coding conventions |
| `documentation.instructions.md` | `*.md, docs/**` | Doc locations, conventions, Mermaid style |

### Layer 2 — Skills (loaded on demand)

| Skill | Purpose |
|---|---|
| `sunray-firmware` | CMake build commands, source layout, motor control, config management |
| `docker-build` | Dockerfile, QEMU cross-compile, CI, deploy/rollback |
| `github-pr` | Branch naming, PR workflow, upstream sync, SemVer |
| `documentation` | Doc templates, Mermaid conventions, config reference format |

### Layer 3 — Agents (specialist personas)

| Agent | Role | Key tools |
|---|---|---|
| `sunray` | Orchestrator — delegates to specialists | search, read, list + subagents |
| `firmware-build` | C++ source, CMake, config | search, read, edit, terminal |
| `docker` | Dockerfile, CI, deployment | search, read, edit, terminal |
| `pr-manager` | GitHub PRs, branches, upstream sync | search, read, terminal, GH PR |
| `docs` | Documentation only (read-only source) | search, read, edit, create |

## Consequences

- Instructions are automatically injected when editing relevant file types — no manual activation needed
- Skills provide deep workflow knowledge without bloating the instruction context
- The `docs` agent is explicitly read-only for source code — prevents accidental code changes during documentation
- Adding a new workflow (e.g., unit testing): create a skill, optionally an agent, add to orchestrator
- Maintenance: when project structure changes, update the relevant instruction/skill files

## References

- [`46fa21f`](https://github.com/autoditac/Sunray/commit/46fa21f) — Copilot agent, skill, and instruction configs
- [#1](https://github.com/autoditac/Sunray/pull/1) — PR: docs/copilot-configs-and-project-docs
