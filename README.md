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
| **Control interface** | Sunray Android/iOS app (TCP socket) | [CaSSAndRA](https://github.com/EinEinfach/CaSSAndRA) + [Alfred Dashboard](https://github.com/autoditac/alfred-dashboard) (HTTP). Sunray app is **not supported**. |
| **Bluetooth** | BLE enabled for Sunray app pairing and ESP32 bridge | Disabled — bluez removed from OS, BLE/ESP32 code paths unused ([ADR-009](doc/adr/ADR-009-Base-OS-Cleanup.md)) |
| **WiFi management** | Firmware polls `wpa_cli` every 7 s in the control loop | Removed from firmware — OS owns WiFi via NetworkManager, kernel LED trigger ([ADR-010](doc/adr/ADR-010-WiFi-Management-OS-Level.md)) |
| **Lift sensor** | Enabled — stops mower and triggers obstacle avoidance when tilted | Disabled — causes frequent false triggers on uneven terrain ([`f9ea46a`](https://github.com/autoditac/Sunray/commit/f9ea46a)) |
| **Buzzer** | Enabled | Disabled — beeps not wanted ([`f9ea46a`](https://github.com/autoditac/Sunray/commit/f9ea46a)) |
| **Container updates** | Manual | Safe auto-update via `alfred-safe-update.timer` — checks dock state and CaSSAndRA schedule before running `podman auto-update`. `:latest` tag only updates on release, not on every push ([Ansible role](https://github.com/autoditac/alfred-ansible)) |
| **MCU firmware** | Compiled on-device via Arduino IDE | Cross-compiled on x86_64 workstation, flashed via OpenOCD/SWD |
| **Upstream sync** | — | GitHub fork, `main` rebased on upstream `master` ([ADR-001](doc/adr/ADR-001-Standalone-Fork-Strategy.md)). Last merge: [`e2791e6`](https://github.com/Ardumower/Sunray/commit/e2791e6d1560b7f44d18151480079546f8dd92e3) (2026-03-22) |

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

## Control interface

The mower is controlled through two web apps, both running on the RPi as
Podman containers alongside Sunray:

- **[CaSSAndRA](https://github.com/EinEinfach/CaSSAndRA)** — full mower
  management: map editing, mowing schedules, path planning, RTK corrections,
  and firmware configuration. This is the primary interface for all complex
  operations.
- **[Alfred Dashboard](https://github.com/autoditac/alfred-dashboard)** —
  lightweight status app showing live telemetry (GPS, battery, motor state,
  WiFi signal). Designed for quick at-a-glance checks, not for control.

The upstream Sunray Android/iOS app is **not supported** — its TCP socket
protocol is not exposed outside the container.

## Chassis and hardware

Alfred is a conversion built on a commodity robot-mower chassis. The
Sunray/Alfred project replaces the OEM mainboard with a Raspberry Pi 4B +
STM32 co-processor but keeps the **chassis, drive motors, mow motor, and
wheels unchanged**.

| Item | Value |
|---|---|
| Base chassis | **Güde GRR 240.1** (part no. 95447-01058, also sold as **Fuxtec RB-224**) |
| Total mass | 16.0 kg |
| Wheelbase | ~39 cm |
| Weight distribution (measured 2026-04-19) | 7.0 kg rear / 9.0 kg front → **43.75 % rear, 56.25 % front** |
| Drive | Rear-wheel drive, 2 × RM24 gear motors |
| Rear wheels | Knobbed rubber, 205 mm |
| Front support | **Two swivel casters (Schwenkrollen)**, each on 2 × sealed 6000-series ball bearings |
| Mow motor | Front-mounted disc, 9 swing blades |
| Battery | Li-Ion 3 Ah / 28 V, front-mounted |

**Key OEM part numbers** (from the Güde GRR 240.1 spare-parts catalogue —
useful when ordering replacements):

| Part # | Item | Qty |
|---|---|---|
| 95447-01051 | Motor Radantrieb (rear drive motor) | 2 |
| 95446-01055 | Rad mit Noppen (knobbed rear wheel) | 2 |
| 95447-01058 | Rad vorne komplett (front caster, complete) | 2 |
| 99999-6000G | Kugellager 6000 geschlossen (sealed 6000 ball bearing) | 4 |
| 95447-01064 | Fahrsensor (drive / motion sensor) | 1 |
| 95447-01038 | Motor Mähwerk (mow motor) | 1 |
| 95447-01086 | Li-Ion Akkupack 3 Ah / 28 V | 1 |
| 95446-01070 | Radgewichte Satz / Rad (**optional** rear-wheel weight kit) | 2 |

**Bearing upgrade:** the OEM front-caster bearings are steel-ring 6000
types that corrode in wet grass. Replace with corrosion-proof polymer
rings + glass balls — e.g.
[igus xiros BB-6000-B180-10-GL](https://www.igus.de/product/xiros_radial_B180_GL_PA_C?artnr=BB-6000-B180-10-GL-C)
(same 6000 footprint, drop-in).

For deeper mechanical analysis — CoG effects, traction envelope,
rotate-in-place physics, and proposed control-software fixes — see
[doc/steering-analysis-2026-04.md](doc/steering-analysis-2026-04.md).

## Hardware design challenges

The Alfred (Güde GRR 240.1) was designed for **random-path, wire-guided
mowing**: drive forward, hit the perimeter wire, turn randomly, repeat.
Running Sunray's RTK-GPS systematic mowing on this platform means working
against several hardware decisions.

### Weight distribution

The mow motor sits at the front, making the mower **nose-heavy**. Most of
the weight rests on the two front caster wheels, while the rear drive wheels
have comparatively little ground pressure. This causes:

- **Poor traction** — drive wheels spin on wet or soft ground before the
  mower turns
- **High turning resistance** — the heavy front must be pivoted by the
  lightly-loaded rear wheels
- **Controller overshoot** — the Stanley path controller compensates for
  the sluggish response, then overshoots once inertia is overcome,
  producing a serpentine pattern

### Front caster wheels

Two independently-swiveling caster wheels (like shopping cart wheels) sit
at the front. During turns, each caster must swing to the new heading
independently — creating asymmetric drag and oscillation until both
settle. On soft ground, one caster may dig in while the other swivels
freely, pulling the mower off-line.

The stock caster wheels use **steel ball bearings** that are exposed to
ground moisture (dew, rain, wet grass). These rust within one season,
increasing rolling resistance and causing the casters to bind — see the
igus xiros replacement noted in **Chassis and hardware** above.

### Sensors designed for random-path

The lift sensor triggers on normal terrain undulation during systematic
mowing (disabled in this fork). The bumper is tuned for frequent wall
impacts, not the rare incidental contact expected in GPS-guided mode.

### What works despite the hardware

RTK-GPS accuracy (~2 cm) compensates for the mechanical imprecision on
**flat, firm ground**. The two-wheel turn fix
([ADR-004](doc/adr/ADR-004-Two-Wheel-Turn-Fix.md)) addresses the worst
turning problem. With generous exclusion zones around obstacles and
realistic expectations (~5 cm edge accuracy instead of the theoretical
2 cm), the Alfred mows systematically and docks reliably.

### Planned tuning

The Stanley controller gains (`P=3.0`, `K=1.0`) are upstream defaults
tuned for the lighter Ardumower chassis. Reducing these values for the
Alfred's heavier front end should produce smoother path tracking at the
cost of slightly larger lateral deviation. See
[mowing-fixes notes](docs/system-setup.md) for the tuning plan.

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
| [ADR-010](doc/adr/ADR-010-WiFi-Management-OS-Level.md) | WiFi Management at OS Level |

## Upstream

For documentation on the original Sunray firmware (Ardumower, owlPlatform,
simulator, ROS, app), see the upstream repository:
https://github.com/Ardumower/Sunray









