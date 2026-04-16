# Sunray Firmware Architecture Deep Dive

## Overview

Sunray is a multi-platform firmware for RTK-GPS-guided autonomous robot mowers. It supports:
- **Ardumower**: Arduino MCU (Due/Grand Central M4) - compiled directly for the MCU
- **Alfred**: Linux (Raspberry Pi / BananaPi) + STM32 MCU via serial - compiled for Linux
- **owlRobotPlatform / SMARTMOW-DIY**: Linux + CAN bus
- **Simulator**: Linux with simulated hardware

**Current upstream version**: `Sunray,1.0.331` (commit `eb4891c`)

## High-Level Architecture

```
┌─────────────────────────────────────────────────────┐
│                    Sunray Main Loop                  │
│  sunray.ino → setup() / loop()                       │
│  - robot.begin() → robot.run() @ ~50ms cycle         │
├──────────────┬──────────────┬────────────────────────┤
│  Navigation  │   Motor      │   Communication        │
│  - Map       │   Control    │   - HTTP Server         │
│  - GPS/RTK   │   - PID      │   - WebSocket Client    │
│  - IMU       │   - Ramp     │   - BLE                 │
│  - State     │   - Overload │   - MQTT                │
│    Estimator │   - Fault    │   - NTRIP (RTK corr.)   │
│  - Line      │     Recovery │                        │
│    Tracker   │              │                        │
│  - Operations│              │                        │
│    (FSM)     │              │                        │
├──────────────┴──────────────┴────────────────────────┤
│              Hardware Abstraction Layer               │
│  RobotDriver (abstract)                              │
│    ├── AmRobotDriver      (Ardumower PCB direct)     │
│    ├── SerialRobotDriver  (Alfred: serial to STM32)  │
│    ├── CanRobotDriver     (owlPlatform: CAN bus)     │
│    └── SimRobotDriver     (Simulator)                │
├──────────────────────────────────────────────────────┤
│  MotorDriver / BatteryDriver / BumperDriver / etc.   │
│  (each has platform-specific implementations)        │
└──────────────────────────────────────────────────────┘
```

## Compilation for Alfred (Linux)

The build system uses **CMake** (`linux/CMakeLists.txt`):

1. `linux/` directory contains:
   - `CMakeLists.txt` - CMake build definition
   - `config_alfred.h` - Default Alfred configuration
   - `service.sh` - Service management script (build, install, start/stop)
   - `start_sunray.sh` - Runtime startup script
   - `src/` - Linux-specific Arduino API wrapper (Arduino.h, Wire, Serial, BLE, etc.)
   - `firmware/` - STM32 MCU firmware (`rm18.ino`)

2. Build process:
   - `config_alfred.h` (or custom `config.h`) is **copied** to `sunray/config.h`
   - CMake compiles all `sunray/**/*.cpp` + `linux/src/**/*.cpp` → single `sunray` binary
   - Binary runs as systemd service on the Pi

3. Key preprocessor defines:
   - `DRV_SERIAL_ROBOT` → selects `SerialRobotDriver` for Alfred
   - `__linux__` → enables Linux-specific code paths

## Driver Selection (Compile-Time)

In `config.h`:
```cpp
#define DRV_SERIAL_ROBOT  1     // Alfred
//#define DRV_CAN_ROBOT  1     // owlPlatform
//#define DRV_ARDUMOWER  1     // Ardumower PCB
//#define DRV_SIM_ROBOT  1     // Simulator
```

This controls which driver classes are instantiated in `robot.cpp`.

## Operation State Machine (FSM)

The robot uses an **Operation** pattern (`sunray/src/op/`):
- `IdleOp` - Robot idle
- `MowOp` - Mowing operation
- `DockOp` - Docking to charging station
- `UndockOp` - Leaving dock
- `ChargeOp` - Charging
- `GpsWaitFixOp/GpsWaitFloatOp` - Waiting for GPS
- `GpsRebootRecoveryOp` - GPS recovery
- `ImuCalibrationOp` - IMU calibration
- `RelocalizationOp` - Lidar relocalization (new in recent versions)
- `EscapeReverseOp/EscapeForwardOp` - Obstacle escape

Active operation changes via `Op::changeOp()`. Each Op has `begin()`, `run()`, `end()`.

## Map & Navigation

- **Map** (`map.cpp/h`): Manages perimeter, exclusion zones, dock points, mow paths
- **Pathfinder**: A* pathfinder for obstacle avoidance routes
- **Stanley Controller** (`LineTracker.cpp`): Lateral+angular PID for path tracking
- **State Estimator** (`StateEstimator.cpp`): Fuses GPS RTK + IMU + odometry

Map coordinates use **local ENU** (East-North-Up) relative to a base point.

### Point data type (important for Alfred):
- Arduino: `short px, py` → max ±327m (centimeter resolution)
- Linux (since ~v1.0.325): `float px, py` → unlimited range

## Communication

### HTTP Server (`httpserver.cpp`)
- Serves the web API on port 80
- Receives AT commands from Sunray App / CaSSAndRA
- Recent refactoring (v1.0.328+): Extracted into `HttpServer` class with WebSocket support

### AT Command Protocol
The robot accepts AT commands via HTTP/BLE/Serial:
- `AT+S` - Request summary (battery, sensors)
- `AT+M,<r>,<l>,<m>` - Motor PWM command
- `AT+V` - Version query
- `AT+C,...` - Configuration/control commands
- `AT+W,...` - Map waypoints upload

### NTRIP Client
Optional RTK correction data from an NTRIP caster (e.g., SAPOS). Configured in `config.h`.

### CaSSAndRA
External management application (not the Sunray PDF app). Connects to Sunray's HTTP API.

## Docking System

Key config options:
- `DOCK_FRONT_SIDE` - Dock with front (true) or back (false)
- `DOCK_IGNORE_GPS` - Use IMU-only near dock
- `DOCK_RETRY_TOUCH` - Retry if charging contacts lost
- `DOCK_RELEASE_BRAKES` - Release electrical brakes in dock

Docking traverses stored dock points. Undocking reverses them.

## Version History (Alfred-relevant)

| Version | Key Changes |
|---------|------------|
| 1.0.288 | First beta: more stable BLE/WiFi |
| 1.0.294 | Retry dock touch |
| 1.0.298 | Memory leak fix |
| 1.0.302 | Mow motor bugfix |
| 1.0.303 | I2C bugfix (sporadic crashes) |
| 1.0.305 | Dock touch recovery |
| 1.0.307 | Timetable support |
| 1.0.310 | Improved RTK course estimation |
| 1.0.314 | Lift sensor bugfix |
| 1.0.315 | Undock GPS fix |
| 1.0.324 | Disabled cutter RPM monitoring (unreliable) |
| 1.0.325 | Map points float for Linux, DOCK_FRONT_SIDE |
| 1.0.331 | Latest: WebSocket, TLS, code refactoring |
