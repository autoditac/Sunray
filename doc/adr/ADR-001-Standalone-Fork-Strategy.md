# ADR-001 — GitHub Fork of Ardumower/Sunray

| Field | Value |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-04-16 |
| **Author** | Rouven Sacha |

---

## Context

The Sunray firmware is developed at [Ardumower/Sunray](https://github.com/Ardumower/Sunray) and targets multiple hardware platforms (Ardumower, Alfred, Owlet). We run two Alfred mowers (robin, batman) with custom patches (two-wheel-turn, disabled features) and need to maintain these customizations while staying close to upstream.

### Options considered

| Option | Pros | Cons |
|---|---|---|
| **GitHub Fork** | Native PR to upstream, fork network, easy sync | Must keep `main` rebased on upstream |
| **Standalone repo with upstream remote** | Full control, clean history | No fork network, manual upstream sync |
| **Patch files on top of upstream** | Minimal divergence | Fragile, hard to manage multiple patches, no CI |

## Decision

Use a **GitHub fork** at [`autoditac/Sunray`](https://github.com/autoditac/Sunray), forked from `Ardumower/Sunray`.

```
origin   → https://github.com/autoditac/Sunray.git         (our fork)
upstream → https://github.com/Ardumower/Sunray.git         (original)
```

## Consequences

- Upstream sync via `git fetch upstream && git rebase upstream/master`
- GitHub fork network enables contributing back to upstream via PRs directly
- Common conflict areas: `sunray/motor.cpp` (two-wheel-turn patch), `linux/config_alfred.h` (custom defines)
- `main` branch carries our customizations on top of upstream `master`
