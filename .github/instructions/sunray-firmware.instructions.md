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
- Config selection: `cmake -DCONFIG_FILE=../../configs/robin.h ..`
- Default: copies `linux/config.h` → `sunray/config.h`
- Source globs: all `sunray/**/*.cpp` + `linux/src/**/*.cpp` + `sunray/sunray.ino`
- Excludes: `agcm4|due|esp` patterns (other platform targets)

## Config Management

### Per-mower configs live in `configs/`

| File | Mower | Key differences |
|---|---|---|
| `configs/robin.h` | Robin | WiFi credentials |
| `configs/batman.h` | Batman | WiFi credentials, rain off, MOW_TOGGLE_DIR false |

### Shared config base: `linux/config_alfred.h`

This is the upstream Alfred template. Per-mower configs are copies with mower-specific overrides.

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
- NTRIP (`ENABLE_NTRIP` commented out)
- Rain sensor (`RAIN_ENABLE false`)

## Docker Build

- Multi-stage Dockerfile: `debian:bookworm-slim` builder + runtime
- Build deps: cmake, g++, libbluetooth-dev, libssl-dev, libjpeg-dev
- Runtime deps: libbluetooth3, libssl3, libjpeg62-turbo
- Config passed via `--build-arg CONFIG_FILE=configs/<mower>.h`
- Container runs privileged with host networking (serial, I2C, GPIO access)

## Testing

- No formal unit test framework currently exists in upstream Sunray
- Validation is done by:
  1. Successful compilation (cmake + make)
  2. Log output analysis on mower (CONSOLE output)
  3. Functional testing in the field
- Future: consider adding GoogleTest for motor math, map operations, PID controller
