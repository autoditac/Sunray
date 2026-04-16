---
description: >-
  Documentation conventions for the Sunray Alfred fork. Defines where architecture
  docs, operational notes, and config references live.
applyTo: '**/*.md,docs/**'
---

# Documentation Conventions

## Documentation Locations

| Content | Location | Audience |
|---|---|---|
| Architecture, design decisions | `docs/` in repo | Developers |
| Config reference, motor tuning | `docs/` in repo | Developers, operators |
| Operational notes, mower state | Nextcloud notes (`Alfred/`) | Owner |
| Quick start, building, deploying | `README.md` | Everyone |

## `docs/` Structure

```
docs/
  ARCHITECTURE.md          # System architecture, component overview, data flow
  motor-control.md         # Motor control, PID tuning, two-wheel-turn
  serial-protocol.md       # Alfred AT command protocol reference
  config-reference.md      # Config.h options and their effects
  docker-deployment.md     # Docker build, deploy, update workflow
```

## Conventions

- Architecture docs use Mermaid diagrams for system and data flow visualization
- Config reference documents each `#define` group with purpose, valid values, and effects
- Keep docs concise and technical — this is a firmware project, not a user-facing product
- Link between docs files rather than duplicating content
- `README.md` stays short: what it is, how to build, how to deploy
