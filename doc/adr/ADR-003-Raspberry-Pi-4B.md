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
- Same I2C bus for IO board (`/dev/i2c-1`)
- Sunray's Linux platform layer works without modification on RPi 4B

### Hardware differences requiring config changes

| Interface | BananaPi M1 | Raspberry Pi 4B | Config / File |
|---|---|---|---|
| STM32 MCU UART | `/dev/ttyS1` | `/dev/ttyS0` | `SERIAL_ROBOT_PATH` in `configs/config.h` |
| OpenOCD SWD GPIO (SWDIO) | GPIO 10 | GPIO 24 | `openocd/rpi4.cfg` (uses `bcm2835gpio` driver) |
| OpenOCD SWD GPIO (SWCLK) | GPIO 47 | GPIO 25 | `openocd/rpi4.cfg` |
| OpenOCD SWD GPIO (SRST) | GPIO 11 | GPIO 18 | `openocd/rpi4.cfg` |
| GPS ublox f9p (USB) | `/dev/serial/by-id/usb-u-blox_AG_-_...` | Same (USB by-id) | No change needed |
| I2C IO board | `/dev/i2c-1` | Same | No change needed |

The upstream `swd-pi.ocd` is configured for BananaPi GPIO numbers. For RPi 4B, use `openocd/rpi4.cfg` which uses the `bcm2835gpio` driver with correct RPi pin mappings.

## Consequences

- Both mowers run RPi 4B with identical hardware
- Debian Bookworm provides Podman in repos (enables container-based deployment)
- GPS ublox f9p USB connection works identically (same `/dev/serial/by-id/` path)
- IO board I2C works identically (`/dev/i2c-1`)
- `SERIAL_ROBOT_PATH` must be `/dev/ttyS0` instead of upstream default `/dev/ttyS1` — changed in `configs/config.h`
- MCU flashing via OpenOCD requires `openocd/rpi4.cfg` instead of `swd-pi.ocd` (different GPIO driver and pin numbers)
