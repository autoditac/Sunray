# ADR-002 — Docker Containerization for Alfred Mowers

| Field | Value |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-04-16 |
| **Author** | Rouven Sacha |

---

## Context

Sunray on Alfred mowers runs as a systemd service installed via `start_sunray.sh`. This script handles BLE, CAN, audio, and compilation — but also blocks execution inside Docker containers by checking for `/.dockerenv`. Updates require SSH into each mower, pulling code, and recompiling natively on the RPi 4B.

We want:
- Reproducible builds (same binary regardless of build host)
- Remote updates without SSH + recompile
- CI/CD pipeline for automated builds on push
- Per-mower configuration without maintaining separate branches

### Options considered

| Option | Pros | Cons |
|---|---|---|
| **Keep systemd + native compile** | Known-good, upstream-supported | Manual SSH updates, no CI, build depends on RPi state |
| **Docker with native build on Pi** | Containers but still slow compile | RPi ARM compile is slow (~10 min), still needs SSH |
| **Docker with cross-compile (QEMU)** | Fast CI on x86, reproducible, remote pull-to-update | QEMU overhead (~10 min first build), needs privileged container |
| **Cross-compiler toolchain** | Fastest native compile | Complex setup, not standard for this project |

## Decision

Use **multi-stage Docker builds** with QEMU cross-compilation via `docker buildx`:

- **Builder stage**: `debian:bookworm-slim` with build tools (cmake, g++, dev libraries)
- **Runtime stage**: `debian:bookworm-slim` with only runtime libraries
- **Config selection**: `--build-arg CONFIG_FILE=configs/<mower>.h`
- **CI**: GitHub Actions with QEMU, matrix build for robin + batman, push to `ghcr.io`
- **Deploy**: `docker compose pull && docker compose up -d` on each mower

A custom `docker-entrypoint.sh` replaces `start_sunray.sh`, running only the Sunray binary with signal forwarding (no BLE, CAN, audio, or Docker detection).

## Consequences

- Container must run **privileged** with **host networking** (serial `/dev/ttyS0`, I2C, GPS USB, port 80)
- First Docker build takes ~10 min due to QEMU; subsequent builds use layer cache
- Rollback: either pull previous image tag, or re-enable systemd service
- The original `start_sunray.sh` remains untouched for non-Docker use
- `sunray_manual.pdf` and other large files excluded via `.dockerignore`
