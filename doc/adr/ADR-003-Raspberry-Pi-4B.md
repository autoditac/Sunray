# ADR-003 — Raspberry Pi 4B Instead of BananaPi

| Field | Value |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-04-16 |
| **Author** | Rouven Sacha |

---

## Context

Alfred mowers ship with a BananaPi M1 as the Linux SBC. The BananaPi runs an outdated Debian version that is no longer supported, and its WiFi connectivity is very unstable — causing frequent disconnections that disrupt CaSSAndRA communication and remote monitoring.

### Options considered

| Option | Pros | Cons |
|---|---|---|
| **Keep BananaPi M1** | No hardware change, upstream-supported | Outdated Debian, unstable WiFi, no security updates |
| **Raspberry Pi 4B** | Debian Bookworm (supported), stable WiFi, widely available, well-documented | Requires physical swap, different GPIO/serial pinout |
| **Other SBC (Orange Pi, etc.)** | Potentially cheaper | Smaller community, driver support uncertain |

## Decision

Replace the BananaPi M1 with a **Raspberry Pi 4B** (4GB) running **Debian Bookworm** (aarch64).

- Stable WiFi out of the box (onboard BCM43455)
- Current Debian with security updates until 2028
- Same serial interface for STM32 communication (`/dev/ttyS0`)
- Same I2C bus for IO board (`/dev/i2c-1`)
- Sunray's Linux platform layer works without modification on RPi 4B

## Consequences

- Both mowers (robin, batman) run RPi 4B with identical hardware
- Debian Bookworm provides Podman in repos (enables container-based deployment)
- GPS USB receiver and IO board I2C work identically to BananaPi
- No upstream code changes required — Sunray's Alfred Linux build runs on any aarch64 Debian
