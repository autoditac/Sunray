# Sunray State Machine

Mermaid version of `doc/Sunray_fsm.png`. Generated from the Op source files in `sunray/src/op/`.

## State Machine

```mermaid
stateDiagram-v2
    [*] --> Idle

    %% === Idle ===
    Idle --> Charge : Charger connected

    %% === Charge ===
    Charge --> Idle : Charger disconnected\n(manual dock)
    Charge --> Mow : Charging complete\n(auto-start)

    %% === Mow (main mowing loop) ===
    state Mow {
        [*] --> Mowing
        Mowing --> EscapeReverse : Obstacle / Motor overload
        Mowing --> EscapeForward : Rotation stuck
        EscapeReverse --> Mowing : Add virtual obstacle,\nre-plan path
        EscapeForward --> Mowing : Continue operation
        Mowing --> GpsWaitFix : Fix timeout
        Mowing --> GpsWaitFloat : No GPS signal
        GpsWaitFix --> Mowing : Fix recovered
        GpsWaitFloat --> Mowing : Float/Fix recovered
        Mowing --> KidnapWait : Kidnapped\n(GPS jump)
        KidnapWait --> Mowing : Position recovered
        KidnapWait --> GpsRebootRecovery : 3 retries failed
        GpsRebootRecovery --> Mowing : GPS rebooted,\nretry after 30s
    }

    %% === Mow entry/exit ===
    Idle --> ImuCalibration : Start command
    ImuCalibration --> Mow : Calibration done\n(15s)
    Mow --> Dock : Mowing finished /\nBattery low /\nRain
    Mow --> Idle : Stop command
    Mow --> Error : No route / Motor error /\nSensor failure

    %% === Dock (navigate to charger) ===
    state Dock {
        [*] --> Undocking : From charge
        [*] --> Docking : Navigate to dock
        Docking --> DockEscapeReverse : Obstacle
        DockEscapeReverse --> Docking : Continue docking
        Undocking --> Docking : Undock complete
    }

    Dock --> Charge : Charger connected
    Dock --> Error : Docking failed /\nMotor error
    Dock --> Idle : Timeout / Stop

    %% === Error ===
    Error --> Charge : Charger connected
    Error --> Idle : Stop command

    %% === Operator commands (from any state) ===
    note right of Idle
        Operator commands (AT+S, AT+M, etc.)
        can force transitions:
        Stop → Idle
        Start → Mow
        Dock → Dock
    end note
```

## Architecture Overview

```mermaid
graph LR
    App["App Communication<br/>AT+M, AT+S, ..."] --> Robot

    Robot --> SM["State Machine<br/>(Op classes)"]
    Robot --> LT["Line Tracker<br/>(PID steering)"]
    Robot --> PP["Path Planner<br/>(obstacle avoidance)"]
    Robot --> SE["State Estimator<br/>(GPS + IMU fusion)"]

    SM --> |"setLinearAngularSpeed()"| Motor["Motor Control"]
    LT --> |"steeringAngle"| Motor
    PP --> |"waypoints"| LT
    SE --> |"stateX, stateY, stateDelta"| LT
    SE --> |"stateX, stateY"| PP
```

## Op Classes

| Op class | Source file | Purpose |
|---|---|---|
| `IdleOp` | `src/op/IdleOp.cpp` | Motors off, waiting for command |
| `MowOp` | `src/op/MowOp.cpp` | Main mowing loop with path following |
| `DockOp` | `src/op/DockOp.cpp` | Navigate to charger via dock points |
| `ChargeOp` | `src/op/ChargeOp.cpp` | Charging, auto-start on completion |
| `ErrorOp` | `src/op/ErrorOp.cpp` | Emergency stop on critical failure |
| `EscapeReverseOp` | `src/op/EscapeReverseOp.cpp` | Reverse 3s on obstacle |
| `EscapeForwardOp` | `src/op/EscapeForwardOp.cpp` | Forward 2s on rotation stuck |
| `GpsWaitFixOp` | `src/op/GpsWaitFixOp.cpp` | Wait for RTK fix |
| `GpsWaitFloatOp` | `src/op/GpsWaitFloatOp.cpp` | Wait for float or fix |
| `GpsRebootRecoveryOp` | `src/op/GpsRebootRecoveryOp.cpp` | Reboot GPS, wait 30s |
| `KidnapWaitOp` | `src/op/KidnapWait.cpp` | Wait after GPS jump (kidnap detect) |
| `ImuCalibrationOp` | `src/op/ImuCalibrationOp.cpp` | 15s gyro calibration at startup |
| `RelocalizationOp` | `src/op/RelocalizationOp.cpp` | LiDAR relocalization (ROS mode) |
