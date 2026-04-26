---
name: sunray-firmware
description: >-
  Build, configure, and modify the Sunray firmware for Alfred mowers. Use when
  compiling, editing motor control, navigation, driver code, or config files.
  Covers CMake build, config management, and cross-compilation.
---

# Sunray Firmware Development

Build and modify the Sunray firmware for Alfred mowers running on RPi 4B (aarch64).

## Build system

### IMPORTANT: Never build locally

**Do NOT build firmware on the workstation or on the mower.** All builds go through GitHub Actions CI which cross-compiles for arm64 via QEMU and pushes to `ghcr.io`. To verify a change compiles, push to a PR branch and check the CI build status.

### Release streams (5-stream model)

| Stream | Tag(s) | Trigger | Mowers |
|---|---|---|---|
| **alpha** | `:alpha`, `:feature-<name>` | Push to `feature/*` | Dev mowers |
| **beta** | `:beta` | Push to `main` (merged PR) | batman |
| **release** | `:latest`, `:<version>` | `v*` tag push | Production (robin) |
| **upstream-alpha** | `:upstream-alpha` | `upstream-alpha-*` tag | Manual |
| **upstream-release** | `:upstream-release`, `:upstream-<version>` | `upstream-v*` tag | Manual |

- **batman** runs `:beta` — receives every merged changeset automatically
- **robin** runs `:latest` — only updates on tagged releases
- Feature branches build `:alpha` + `:feature-<branch>` for targeted testing
- Each feature/fix should be a **separate PR** so each merge produces an individual beta build
- Beta firmware version includes the short SHA: `Sunray,1.0.331-autoditac.1-beta.abc1234`
- Release firmware version is clean: `Sunray,1.0.331-autoditac.1`

### Cross-compile via Docker (CI only)

```bash
# This happens in GitHub Actions, not locally:
docker buildx build --platform linux/arm64 \
  --build-arg FIRMWARE_SHA=abc1234 \
  -t sunray .
```

### Config selection

The CMake build copies the config file to `sunray/config.h` before compiling:

```bash
# Used in Dockerfile:
cmake -DCONFIG_FILE=../../configs/config.h -DFIRMWARE_SHA=abc1234 ..

# FIRMWARE_SHA is empty for release builds
```

## Key source files

| File | Purpose |
|---|---|
| `sunray/motor.cpp` | Motor control loop, PID, unicycle model, two-wheel-turn |
| `sunray/motor.h` | Motor class definition, tuning parameters |
| `sunray/map.cpp` | Map management, docking logic, path planning |
| `sunray/LineTracker.cpp` | Stanley path tracking controller |
| `sunray/StateEstimator.cpp` | GPS + IMU + odometry fusion |
| `sunray/src/driver/SerialRobotDriver.cpp` | Alfred hardware driver (STM32 serial) |
| `sunray/src/op/*.cpp` | Operation state machine (Mow, Dock, Charge, etc.) |
| `sunray/httpserver.cpp` | HTTP API for CaSSAndRA / app communication |
| `sunray/robot.h` | Driver selection, global includes |
| `sunray/robot.cpp` | Driver instantiation |
| `linux/src/` | Linux platform layer (Arduino API emulation) |

## Motor control specifics

### Unicycle model
```
VR = V + omega * L/2
VL = V - omega * L/2
RPM = V / (2*PI*r) * 60
```

Where `L` = wheelBaseCm/100, `r` = wheelDiameter/2000

### PID tuning (config.h)
```c
#define MOTOR_PID_KP  0.5   // proportional gain
#define MOTOR_PID_KI  0.01  // integral gain
#define MOTOR_PID_KD  0.01  // derivative gain
```

### Two-wheel-turn (custom patch)
```c
#define MIN_WHEEL_SPEED  0.05  // minimum wheel speed (m/s) to maintain traction on Alfred's heavy nose
```

During forward/reverse tracking: clamps inner wheel to +MIN_WHEEL_SPEED (same direction of travel) to skip the dead zone. During rotation in place (linear ≈ 0): counter-rotates both wheels at ±MIN_WHEEL_SPEED. Excluded during docking/undocking. Guarded by `#ifdef`.

## Adding a config option

1. Add `#define` to `configs/config.h`
2. Also add to `linux/config_alfred.h` as upstream reference
3. Use `#ifdef` or `#if defined()` in source code to guard the feature
4. Document the option's purpose and valid range in a comment next to the define

## Common compilation issues

- **Missing config.h**: CMake copies it — if building fails, check `CONFIG_FILE` path
- **BLE link errors**: Need `libbluetooth-dev` even if BLE is disabled (code still compiles)
- **I2C headers**: Need `linux/i2c-dev.h` — install via kernel headers package
- **Cross-compile**: Use Docker buildx with QEMU, not a native cross-toolchain
