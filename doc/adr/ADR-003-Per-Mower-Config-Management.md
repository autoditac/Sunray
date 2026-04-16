# ADR-003 — Shared Config for All Mowers

| Field | Value |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-04-16 |
| **Author** | Rouven Sacha |

---

## Context

Sunray uses a single `config.h` file that is compiled into the firmware. Upstream provides platform templates (`config_alfred.h`, `config_owlet.h`, etc.) that users copy to `config.h`.

We run two Alfred mowers (robin, batman) on identical hardware with identical settings. Both use the same firmware binary.

### Options considered

| Option | Pros | Cons |
|---|---|---|
| **Per-mower config files** | Supports divergence | Unnecessary duplication when configs are identical |
| **Single shared config** | Simple, one place to change | Must fork if mowers diverge in future |
| **Config overlay/patch system** | Minimal duplication | Over-engineered |

## Decision

Use a **single shared config** at `configs/config.h` for all mowers. One build produces one image deployed to all mowers.

```
configs/
  config.h   # Shared config for all Alfred mowers
```

Build:
```bash
cmake -DCONFIG_FILE=../../configs/config.h ..
```

CI builds a single image `ghcr.io/<owner>/sunray:latest` — no matrix, no per-mower variants.

## Consequences

- `configs/config.h` is a customized copy of `linux/config_alfred.h` with our patches
- When upstream adds new config options to `config_alfred.h`, only one file needs updating
- If mowers diverge in the future, reintroduce per-mower configs and a CI matrix
- The upstream template `linux/config_alfred.h` is kept as reference but not used directly in builds
