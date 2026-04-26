---
description: >-
  Sunray firmware project conventions for Alfred mowers. Defines build system,
  config management, hardware constraints, and coding patterns.
  Auto-injected when editing C++ source, config, or CMake files.
applyTo: '**/*.cpp,**/*.h,**/*.ino,**/CMakeLists.txt,configs/**'
---

# Sunray Firmware Conventions

## Project Overview

Sunray is a C++ firmware for RTK-GPS robot mowers, originally targeting Arduino (Ardumower) and extended to Linux SBCs (Raspberry Pi, BananaPi). This fork targets **Alfred** mowers with RPi 4B (aarch64, Debian bookworm).

## Architecture

- **Platform abstraction**: `linux/src/` provides Arduino-compatible API (Wire, Serial, BLE, WiFi, etc.) on Linux
- **Firmware core**: `sunray/` contains platform-independent motor control, navigation, mapping, state machine
- **Drivers**: `sunray/src/driver/` — abstract `RobotDriver` base with `SerialRobotDriver` (Alfred), `CanRobotDriver` (Owl), `AmRobotDriver` (Ardumower), `SimRobotDriver`
- **Operations FSM**: `sunray/src/op/` — IdleOp, MowOp, DockOp, ChargeOp, EscapeReverseOp, etc.
- **Config**: Compile-time `#define` configuration — config.h is copied from a config file at cmake time

## Build System

- **CMake** in `linux/` directory
- Config selection: `cmake -DCONFIG_FILE=../../configs/config.h ..`
- Default: copies `linux/config.h` → `sunray/config.h`
- Source globs: all `sunray/**/*.cpp` + `linux/src/**/*.cpp` + `sunray/sunray.ino`
- Excludes: `agcm4|due|esp` patterns (other platform targets)

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

- **batman** runs `:beta` — auto-updates on every merge to main
- **robin** runs `:latest` — only updates on tagged releases
- Feature branches build `:alpha` + `:feature-<branch>` for targeted testing
- Each feature/fix should be a **separate PR** so each merge produces an individual beta build

## Config Management

### Shared config: `configs/config.h`

Single config file for all Alfred mowers. Customized copy of the upstream template `linux/config_alfred.h`.

### Upstream template: `linux/config_alfred.h`

Reference file from upstream. When upstream adds new options, merge them into `configs/config.h`.

### Config conventions

- All config values are `#define` preprocessor macros
- Feature flags: `#define ENABLE_FEATURE 1` to enable, comment out to disable
- Hardware-specific: pin mappings, I2C addresses, serial paths
- Motor tuning: PID coefficients, wheel geometry, speed limits
- Navigation: Stanley controller gains, GPS timeout, path tolerances

## Coding Conventions

- **C++ 14** standard (set in CMakeLists.txt)
- **Arduino idioms**: `millis()`, `CONSOLE.println()`, `setup()`/`loop()` lifecycle
- **Globals**: subsystem instances are global (`motor`, `maps`, `gps`, `stateEstimator`, `battery`)
- **Units**: speeds in m/s, angles in radians, distances in meters (except wheel geometry in cm/mm)
- **Conditional compilation**: use `#ifdef FEATURE` guards for optional code paths
- **Comments**: keep inline with existing style — `//` comments, no doxygen

## Alfred Hardware (SerialRobotDriver)

- **STM32 MCU** connected via UART `/dev/ttyS0` at 19200 baud
- **AT command protocol**: `AT+M,<right>,<left>,<mow>` (motor), `AT+S` (summary), `AT+V` (version)
- **IO Board**: TCA9548A I2C mux (0x70), PCA9555 expanders (0x20-0x22), MCP3421 ADC, MPU6050 IMU (0x69)
- **Wheel geometry**: WHEEL_BASE_CM=39, WHEEL_DIAMETER=205mm, TICKS_PER_REVOLUTION=320
- **Both motor directions swapped** (`MOTOR_LEFT_SWAP_DIRECTION 1`, `MOTOR_RIGHT_SWAP_DIRECTION 1`)

## Custom Patches (maintained in this fork)

### Two-wheel turn (`sunray/motor.cpp`)
When the inner wheel would be commanded slowly forward during a turn, drive it backward proportionally to prevent stalling on uneven terrain. Guarded by `#ifdef TWO_WHEEL_TURN_SPEED_THRESHOLD`.

### Disabled features (in config)
- BLE (`BLE_NAME` commented out)
- Lift sensor (`ENABLE_LIFT_DETECTION` commented out)
- Buzzer (`BUZZER_ENABLE` commented out)
- NTRIP (`ENABLE_NTRIP` commented out) — **not used in this fleet**. RTK corrections are delivered to the u-blox F9P through an external channel (not via Sunray). The Sunray Linux NTRIP client stays disabled; do not re-enable it or add NTRIP credentials. Intermittent `sol=0/1/2` (no fix / dead-reckoning / float) and `GpsRebootRecovery` events are base-station / radio / antenna issues, not NTRIP issues.
- Rain sensor (`RAIN_ENABLE false`)

## Docker Build

- Multi-stage Dockerfile: `debian:bookworm-slim` builder + runtime
- Build deps: cmake, g++, libbluetooth-dev, libssl-dev, libjpeg-dev
- Runtime deps: libbluetooth3, libssl3, libjpeg62-turbo
- Config hardcoded to `configs/config.h` in Dockerfile
- Runtime on mowers via Podman/Quadlet (daemonless, native systemd)
- Container runs privileged with host networking (serial, I2C, GPIO access)
- **Alpha builds**: CI passes `FIRMWARE_SHA` build arg → CMake defines it → firmware version becomes `Sunray,1.0.331-autoditac.1-alpha.<short-sha>`
- **Release builds**: no `FIRMWARE_SHA` → firmware version stays `Sunray,1.0.331-autoditac.1`

## Testing

- No formal unit test framework currently exists in upstream Sunray
- Validation is done by:
  1. Successful compilation (cmake + make)
  2. Log output analysis on mower (CONSOLE output)
  3. Functional testing in the field
- Future: consider adding GoogleTest for motor math, map operations, PID controller
