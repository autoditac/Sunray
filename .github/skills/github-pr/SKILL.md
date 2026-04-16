---
name: github-pr
description: >-
  Create and manage GitHub pull requests for the Sunray fork. Use when creating
  PRs, managing branches, syncing with upstream, or preparing releases.
---

# GitHub PR Management

Create and manage pull requests and branches in the Sunray Alfred fork.

## Repository setup

- **Origin**: `origin` — your personal GitHub repo (`sunray-alfred`)
- **Upstream**: `upstream` — `https://github.com/Ardumower/Sunray.git`
- **Default branch**: `main`

## Branch naming

```
feature/<description>       # new features or config changes
fix/<description>            # bug fixes
docs/<description>           # documentation only
```

Examples:
- `feature/two-wheel-turn`
- `fix/pid-tuning-stall`
- `docs/motor-control-reference`

## PR workflow

### Creating a feature branch

```bash
git checkout main
git pull origin main
git checkout -b feature/<description>
# ... make changes ...
git add -A && git commit -m "<description>"
git push -u origin feature/<description>
```

### Creating a PR

Use the GitHub CLI:
```bash
gh pr create --title "<title>" --body "<description>" --base main
```

Or use the VS Code GitHub PR extension.

### PR conventions

- Title: concise description of the change
- Body: what changed and why, list affected files/configs
- One logical change per PR
- Ensure the Docker build succeeds before merging (CI will verify)

## Syncing with upstream

### Fetch and rebase

```bash
git fetch upstream
git checkout main
git rebase upstream/master
# Resolve conflicts if any (likely in config files or motor.cpp)
git push origin main --force-with-lease
```

### Common conflict areas

| File | Reason |
|---|---|
| `sunray/motor.cpp` | Two-wheel-turn patch sits in `setLinearAngularSpeed()` |
| `linux/config_alfred.h` | Custom defines added (TWO_WHEEL_TURN, disabled features) |
| `sunray/config_example.h` | Upstream adds new config options |

### After rebase

1. Update per-mower configs: check if upstream added new config options to `config_alfred.h`
2. Copy new options to `configs/robin.h` and `configs/batman.h`
3. Push and verify CI builds pass

## Versioning

Use **SemVer** tags on `main`:

| Bump | When |
|---|---|
| MAJOR | Breaking config changes, upstream rebase with conflicts |
| MINOR | New features (e.g., new motor mode), new config options |
| PATCH | Bug fixes, config tweaks, doc updates |

```bash
git tag -a v1.0.0 -m "Initial Docker-based release for Alfred mowers"
git push origin v1.0.0
```
