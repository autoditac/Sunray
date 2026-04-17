# Sunray for Alfred (Fork)

Fork of [Ardumower/Sunray](https://github.com/Ardumower/Sunray) for running
Alfred mowers as **Docker/Podman containers** on a Raspberry Pi 4B.

## How this fork differs from upstream

| Area | Upstream | This fork |
|---|---|---|
| **Turning** | Single-wheel turns — one wheel spins freely, lacks traction to pivot the heavy nose, digs into soft ground | Both wheels drive in opposite directions for on-the-spot turns ([ADR-004](doc/adr/ADR-004-Two-Wheel-Turn-Fix.md)) |
| **SBC** | BananaPi M1 — unstable WiFi, outdated Debian | Raspberry Pi 4B — stable WiFi, current Debian ([ADR-003](doc/adr/ADR-003-Raspberry-Pi-4B.md)) |
| **Deployment** | `start_sunray.sh` + native build on device | Multi-stage Docker image, deployed as Podman Quadlet ([ADR-002](doc/adr/ADR-002-Docker-Containerization.md)) |
| **Main loop** | Forks shell commands (`fork()`/`exec()`) every loop iteration — pins CPU at 100%, drops control loop to 1 Hz | Shell calls removed — loop runs at 50 Hz, ~5% CPU ([ADR-006](doc/adr/ADR-006-Process-Fork-Removal.md)) |
| **Control interface** | Sunray Android/iOS app (TCP socket) | CaSSAndRA + Alfred Dashboard (HTTP). Sunray app is **not supported**. |
| **Bluetooth** | BLE enabled for Sunray app pairing and ESP32 bridge | Disabled — bluez removed from OS, BLE/ESP32 code paths unused ([ADR-009](doc/adr/ADR-009-Base-OS-Cleanup.md)) |
| **MCU firmware** | Compiled on-device via Arduino IDE | Cross-compiled on x86_64 workstation, flashed via OpenOCD/SWD |
| **Upstream sync** | — | GitHub fork, `main` rebased on upstream `master` ([ADR-001](doc/adr/ADR-001-Standalone-Fork-Strategy.md)) |

## Building the container image

```bash
docker buildx build --platform linux/arm64 -t sunray .
```

The Dockerfile uses a multi-stage build: compile in `debian:bookworm-slim`,
then copy the binary into a minimal runtime image. The resulting image runs
on any aarch64 host with Podman or Docker.

## MCU firmware

The STM32 MCU firmware must be compiled on an x86_64 host (no arm64
toolchain available for the STMicroelectronics Arduino core). See
[docs/system-setup.md](docs/system-setup.md) for toolchain setup, build
commands, and SWD flashing instructions.

## Deployment

Deployment is managed by the
[alfred-ansible](https://github.com/autoditac/alfred-ansible) role, which
handles OS tuning, Quadlet service files, OpenOCD configs, and MCU flashing.

## Architecture Decision Records

| ADR | Title |
|---|---|
| [ADR-001](doc/adr/ADR-001-Standalone-Fork-Strategy.md) | GitHub Fork of Ardumower/Sunray |
| [ADR-002](doc/adr/ADR-002-Docker-Containerization.md) | Docker Containerization for Alfred Mowers |
| [ADR-003](doc/adr/ADR-003-Raspberry-Pi-4B.md) | Raspberry Pi 4B Instead of BananaPi |
| [ADR-004](doc/adr/ADR-004-Two-Wheel-Turn-Fix.md) | Two-Wheel Turn Fix |
| [ADR-005](doc/adr/ADR-005-Copilot-Config-Architecture.md) | Copilot Config Architecture |
| [ADR-006](doc/adr/ADR-006-Process-Fork-Removal.md) | Remove Process::runShellCommand from Main Loop |
| [ADR-007](doc/adr/ADR-007-Realtime-Scheduling.md) | Realtime Scheduling |
| [ADR-008](doc/adr/ADR-008-OS-Kernel-Tuning.md) | OS Kernel Tuning |
| [ADR-009](doc/adr/ADR-009-Base-OS-Cleanup.md) | Base OS Cleanup |

## Upstream

For documentation on the original Sunray firmware (Ardumower, owlPlatform,
simulator, ROS, app), see the upstream repository:
https://github.com/Ardumower/Sunray









