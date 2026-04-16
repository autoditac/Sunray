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
docker build --build-arg CONFIG_FILE=configs/robin.h -t sunray-robin .

# Cross-compile for arm64 from x86_64
docker buildx create --use
docker buildx build --platform linux/arm64 \
  --build-arg CONFIG_FILE=configs/robin.h \
  -t sunray-robin --load .
```

## GitHub Actions CI

The workflow at `.github/workflows/build.yml`:
- Triggers on push to `main`
- Builds matrix: robin + batman configs
- Uses QEMU for arm64 emulation
- Pushes to `ghcr.io/<owner>/sunray-<mower>:latest` and `:sha`
- Uses GitHub Actions cache for layer caching

## Deploy to mower

### First-time setup (on the Pi)

```bash
# Install Docker
curl -fsSL https://get.docker.com | sh
sudo usermod -aG docker $USER
# Log out and back in

# Create working directory
mkdir -p ~/sunray
cd ~/sunray

# Copy the docker-compose file (from workstation)
# scp deploy/docker-compose.robin.yml robin:~/sunray/docker-compose.yml

# Login to GitHub Container Registry
docker login ghcr.io -u <GITHUB_USER>

# Pull and start
docker compose pull
docker compose up -d
```

### Update workflow

```bash
ssh robin "cd ~/sunray && docker compose pull && docker compose up -d"
```

### Rollback

```bash
# Roll back to previous image
ssh robin "cd ~/sunray && docker compose down"
ssh robin "docker run --privileged --network host ghcr.io/<user>/sunray-robin:<previous-sha>"

# Or re-enable systemd service
ssh robin "sudo systemctl enable sunray && sudo systemctl start sunray"
```

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

### Changing the config build arg
The `CONFIG_FILE` build arg is relative to the repo root:
```dockerfile
ARG CONFIG_FILE=linux/config_alfred.h
```

## Troubleshooting

### Build fails with "CONFIG_FILE does not exist"
The path must be relative to the repo root and exist inside the build context. Check `.dockerignore` isn't excluding it.

### QEMU cross-compile is slow
First build takes ~10 min. Subsequent builds use layer cache. The GitHub Actions workflow uses `cache-from/cache-to` with GHA cache backend.

### Container can't access serial port
Ensure `privileged: true` in docker-compose. If using `devices:` instead, map the exact device paths.
