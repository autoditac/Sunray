# ADR-003 — Per-Mower Config Management

| Field | Value |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-04-16 |
| **Author** | Rouven Sacha |

---

## Context

Sunray uses a single `config.h` file that is compiled into the firmware. Different mowers need different settings (WiFi credentials, feature flags, tuning parameters). Upstream provides platform templates (`config_alfred.h`, `config_owlet.h`, etc.) that users copy to `config.h`.

With two mowers (robin, batman) and Docker CI building both from the same repo, we need a way to maintain per-mower configs without:
- Separate branches per mower
- Manual config swapping before each build
- Duplicating the entire repo

### Options considered

| Option | Pros | Cons |
|---|---|---|
| **Separate branches per mower** | Simple, isolated | Merge hell, duplicated patches |
| **Single config.h with `#ifdef MOWER_ROBIN`** | One file | Complex conditionals, error-prone |
| **Per-mower config files + CMake selection** | Clean, CI-friendly | Slight duplication between config files |
| **Config overlay/patch system** | Minimal duplication | Over-engineered for 2 mowers |

## Decision

Use **per-mower config files** in a `configs/` directory, selected at build time via CMake:

```
configs/
  robin.h    # Robin-specific settings
  batman.h   # Batman-specific settings
```

CMake selects the config via:
```bash
cmake -DCONFIG_FILE=../../configs/robin.h ..
```

Docker builds use:
```bash
docker build --build-arg CONFIG_FILE=configs/robin.h -t sunray-robin .
```

GitHub Actions uses a matrix to build both:
```yaml
strategy:
  matrix:
    include:
      - mower: robin
        config: configs/robin.h
      - mower: batman
        config: configs/batman.h
```

## Consequences

- Both config files are full copies of `linux/config_alfred.h` with mower-specific overrides
- When upstream adds new config options to `config_alfred.h`, both per-mower files must be updated
- Currently robin and batman configs are nearly identical (only WiFi differs), but the structure supports divergence
- The upstream template `linux/config_alfred.h` is kept as reference but not used directly in builds
