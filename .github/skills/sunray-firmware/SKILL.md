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

### Native build (on RPi)

```bash
cd linux
mkdir -p build && cd build
cmake -DCONFIG_FILE=../../configs/config.h ..
make -j$(nproc)
```

### Cross-compile via Docker (on x86_64 workstation)

```bash
docker buildx build --platform linux/arm64 -t sunray .
```

### Config selection

The CMake build copies the config file to `sunray/config.h` before compiling:

```bash
# Use the shared config (default in Dockerfile)
cmake -DCONFIG_FILE=../../configs/config.h ..

# Or use the upstream template directly
cmake ..
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

Activates when inner wheel is slow but positive. Drives it backward proportionally. Excluded during docking/undocking. Guarded by `#ifdef`.

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
