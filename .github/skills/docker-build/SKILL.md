---
name: docker-build
description: >-
  Build and deploy Sunray Docker images for Alfred mowers. Use when building
  containers, troubleshooting Docker builds, deploying to mowers, or modifying
  the Dockerfile and GitHub Actions workflow.
---

# Docker Build & Deploy

Build arm64 Docker images for Sunray and deploy them to Alfred mowers.

## Files

| File | Purpose |
|---|---|
| `Dockerfile` | Multi-stage build (builder + runtime) |
| `docker-entrypoint.sh` | Simplified startup script for containers |
| `.dockerignore` | Excludes irrelevant dirs from build context |
| `.github/workflows/build.yml` | GitHub Actions CI: QEMU cross-compile + ghcr.io push |
| `deploy/docker-compose.robin.yml` | Docker Compose for robin |
| `deploy/docker-compose.batman.yml` | Docker Compose for batman |

## Local build (for testing)

```bash
# Build for native architecture (e.g., on the Pi itself)
docker build -t sunray .

# Cross-compile for arm64 from x86_64
docker buildx create --use
docker buildx build --platform linux/arm64 -t sunray --load .
```

## GitHub Actions CI

The workflow at `.github/workflows/build.yml`:
- Triggers on push to `main` and on tag pushes
- Builds a single image: `sunray`
- Uses QEMU for arm64 emulation
- Push to `main` (merged PR): tags `:sha`, `:alpha`
  - Passes `FIRMWARE_SHA=<short-sha>` build arg → firmware version becomes `Sunray,...-alpha.<sha>`
- Release tag push: tags `:sha`, `:version`, `:latest` (no FIRMWARE_SHA → clean version)
- Uses GitHub Actions cache for layer caching

### Release channels

| Channel | Image tag | Trigger | Target mowers |
|---|---|---|---|
| alpha | `:alpha` | Every push to `main` | batman (guinea pig) |
| release | `:latest` + `:vX.Y.Z` | Git tag push | All mowers |

### Development workflow

1. Each feature/fix gets its **own PR branch**
2. PR merge to `main` triggers CI → `:alpha` tag → batman auto-updates
3. Test on batman, verify logs (`ssh batman "sudo journalctl -u sunray.service -f"`)
4. Once validated, create a git tag to promote to `:latest` for all mowers
5. **Never build locally** — always use CI. No native builds on workstation or mower.

## Deploy to mower (Podman + Quadlet)

The mowers use **Podman** (daemonless) with **Quadlet** systemd integration instead of Docker.

### First-time setup (on the Pi)

```bash
# Install Podman
sudo apt update && sudo apt install -y podman

# Login to GitHub Container Registry
podman login ghcr.io -u <GITHUB_USER>

# Copy the Quadlet file (from workstation)
# scp deploy/sunray.container <mower>:/etc/containers/systemd/sunray.container

# Reload systemd and start
sudo systemctl daemon-reload
sudo systemctl start sunray
sudo systemctl enable sunray  # auto-start on boot
```

### Update workflow

```bash
# batman (alpha channel): auto-updates via podman auto-update on :alpha tag
# Other mowers: auto-update via podman auto-update on :latest tag

# Automatic: alfred-safe-update.timer runs at 03:00 daily
# Checks dock state + CaSSAndRA schedule before running podman auto-update
sudo systemctl status alfred-safe-update.timer

# Manual (bypasses safety checks):
ssh <mower> "sudo podman auto-update"

# Or pull + restart directly:
ssh <mower> "sudo podman pull ghcr.io/<user>/sunray:<tag> && sudo systemctl restart sunray"
```

### Management

```bash
sudo systemctl status sunray    # check status
sudo journalctl -u sunray -f    # follow logs
sudo systemctl stop sunray      # stop
sudo systemctl restart sunray   # restart
```

### Rollback

```bash
# List available images
ssh robin "sudo podman image list"

# Edit the Quadlet file to pin a specific tag
# Image=ghcr.io/<user>/sunray:<previous-sha>
ssh robin "sudo systemctl daemon-reload && sudo systemctl restart sunray"
```

### Legacy: Docker Compose

Docker Compose files (`deploy/docker-compose.*.yml`) are kept for backward compatibility but Podman/Quadlet is preferred.

## Container requirements

The container needs **privileged** mode and **host networking** because:

| Resource | Path | Used for |
|---|---|---|
| STM32 UART | `/dev/ttyS0` | Motor control, AT commands |
| I2C bus | `/dev/i2c-1` | IMU, IO board, EEPROM, ADC |
| GPS USB | `/dev/serial/by-id/usb-u-blox*` | RTK GPS receiver |
| Network | port 80 | HTTP API for CaSSAndRA |

## Modifying the Dockerfile

### Adding a build dependency
Add to the `builder` stage `apt-get install` line.

### Adding a runtime dependency
Add to the `runtime` stage `apt-get install` line. Keep runtime image minimal.

The `CONFIG_FILE` is hardcoded in the Dockerfile to `configs/config.h`:
```dockerfile
RUN cmake -DCONFIG_FILE=/build/Sunray/configs/config.h ..
```

## Troubleshooting

### Build fails with "CONFIG_FILE does not exist"
The path must be relative to the repo root and exist inside the build context. Check `.dockerignore` isn't excluding it.

### QEMU cross-compile is slow
First build takes ~10 min. Subsequent builds use layer cache. The GitHub Actions workflow uses `cache-from/cache-to` with GHA cache backend.

### Container can't access serial port
Ensure `privileged: true` in docker-compose. If using `devices:` instead, map the exact device paths.
