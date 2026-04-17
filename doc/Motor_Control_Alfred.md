# Alfred Motor Control & Chassis Details

## Alfred Hardware Overview

- **Chassis**: Plastic housing, ~65cm×45cm×28.5cm, 16kg
- **Drive motors**: Brushless motors (2× gear/traction)
- **Mow motor**: 1× mowing motor with rotating blade disc (3 blades)
- **Mow width**: 24cm
- **Max slope**: 30°
- **Max mow speed**: 0.5 m/s
- **Wheels**: 205mm diameter, 39cm wheel base
- **Battery**: Li-ion 3Ah or 7Ah, ~28-32V nominal
- **Computer**: Originally BananaPi, can be replaced with Raspberry Pi 4B
- **MCU**: STM32 on IO-board (handles motor drivers, sensors, ADC)
- **GPS**: u-blox F9P RTK receiver
- **IMU**: MPU6050 (I2C address 0x69)

## Architecture: Linux ↔ STM32 MCU

```
┌──────────────────────┐    Serial (19200 baud)    ┌─────────────────────┐
│   Raspberry Pi 4B    │ ────── /dev/ttyS0 ──────▶ │   STM32 MCU         │
│   (Sunray Linux)     │ ◀───── /dev/ttyS0 ──────  │   (rm18 firmware)   │
│                      │                            │                     │
│  - Navigation        │   AT+M,r,l,m  →           │  - Motor H-bridges  │
│  - GPS processing    │   ← M,tickR,tickL,tickM,  │  - Encoder counting │
│  - Map management    │     chargeV,bumper,lift,   │  - Current sensing  │
│  - HTTP server       │     stop                   │  - Battery ADC      │
│  - State estimation  │                            │  - Bumper input     │
│  - PID motor control │   AT+S  →                  │  - Lift sensor      │
│  - IMU (via I2C)     │   ← S,batV,chgV,chgA,     │  - Rain sensor      │
│  - IO board (I2C)    │     lift,bump,rain,fault,  │  - Stop button      │
│                      │     mowA,leftA,rightA,temp │                     │
└──────────────────────┘                            └─────────────────────┘
```

## Serial Protocol (AT commands to STM32)

### Motor command: `AT+M,<rightPWM>,<leftPWM>,<mowPWM>`
- PWM range: -255 to +255
- Sent every 20ms (MOTOR_COMMAND_INTERVAL)
- Response: `M,<ticksRight>,<ticksLeft>,<ticksMow>,<chargeV>,<bumperLeft>,<lift>,<stop>`

### Summary request: `AT+S`
- Response: `S,<batV>,<chgV>,<chgA>,<lift>,<bumperLeft>,<rain>,<motorFault>,<mowCurr>,<leftCurr>,<rightCurr>,<batTemp>`

### Version request: `AT+V`
- Response: `V,<name>,<version>`

All commands have CRC checksum appended: `,0xHH\r\n`

## IO Board (I2C Bus)

The Alfred IO board has multiple I2C devices accessed through a **TCA9548A multiplexer** at 0x70:

| Mux Channel | Device | Address | Function |
|-------------|--------|---------|----------|
| 0 | MPU6050 | 0x69 | IMU (older PCB without buzzer) |
| 4 | MPU6050 | 0x69 | IMU (newer PCB with buzzer) |
| 5 | BL24C256A | 0x50 | EEPROM |
| 6 | MCP3421 | - | ADC |
| 7 | BNO055 | - | Alternative IMU |

### I/O Port Expanders (PCA9555):
- **EX1** (0x21): IMU power, fan power, ADC mux control
- **EX2** (0x20): Buzzer, SWD programming port switch
- **EX3** (0x22): Panel LEDs (3× dual-color green/red)

### ADC Multiplexer (DG408):
8 channels: battery cells (1-3), MCU analog, AD0-AD2, total battery voltage

## Motor Control Loop

The motor control runs at **~50ms intervals** in `Motor::run()`:

```
Motor::run()  (every 50ms)
  ├── sense()              ← read motor currents from SerialMotorDriver
  ├── checkFault()         ← motor driver fault signal
  ├── checkCurrentTooHighError()
  ├── checkCurrentTooLowError()
  ├── checkMowRpmFault()
  ├── checkOdometryError()
  ├── getMotorEncoderTicks() ← delta ticks since last call
  ├── calculate RPM from ticks
  └── control()            ← PID controller → speedPWM()
```

### Unicycle Model
```
V     = (VR + VL) / 2       =>  VR = V + omega * L/2
omega = (VR - VL) / L       =>  VL = V - omega * L/2
```
Where L = wheelBaseCm/100 (m), V = linear speed (m/s), omega = angular speed (rad/s)

### PID Controller
- Per-wheel PID (left + right independently)
- Kp = 0.5, Ki = 0.01, Kd = 0.01 (Alfred defaults)
- Low-pass filter on encoder signal (Tf = 0.0 by default)
- Output: PWM value (-255 to +255)
- Motor direction swapping: both left and right swapped for Alfred (`MOTOR_LEFT_SWAP_DIRECTION`, `MOTOR_RIGHT_SWAP_DIRECTION`)

### Linear Speed Ramp
- Optional (`USE_LINEAR_SPEED_RAMP`): Exponential moving average filter
- `linearSpeedSet = 0.9 * linearSpeedSet + 0.1 * linear`
- Disabled by default for Alfred

### Motor Safety
- **Fault detection**: Motor driver signals, configurable (`ENABLE_FAULT_DETECTION true`)
- **Current limits**: 
  - Gear motors: fault 3.0A, overload 1.2A, too-low 0.005A
  - Mow motor: fault 8.0A, overload 2.0A, too-low 0.005A
- **Overload behavior**: Robot slows down (not stop) with `ENABLE_OVERLOAD_DETECTION false`
- **Recovery**: Up to 10 successive faults before error, with 1s/10s recovery intervals
- **Odometry error**: Disabled by default for Alfred (`ENABLE_ODOMETRY_ERROR_DETECTION false`)
- **Mow RPM fault**: Disabled since v1.0.324 (`ENABLE_RPM_FAULT_DETECTION false`)

### Electrical Brakes
- `releaseBrakesWhenZero`: When traction motors at zero, release after 2s delay
- Configurable for dock via `DOCK_RELEASE_BRAKES`

## Odometry

- **Ticks per revolution**: 320 (RM24 motor)
- **Ticks per cm** = ticksPerRevolution / (wheelDiameter_mm/10) / π ≈ 320 / 20.5 / 3.14 ≈ 4.97
- Direction inferred from PWM sign (not hardware quadrature on Linux)

## Alfred-Specific Config Highlights

| Parameter | Value | Notes |
|-----------|-------|-------|
| WHEEL_BASE_CM | 39 | cm |
| WHEEL_DIAMETER | 205 | mm |
| TICKS_PER_REVOLUTION | 320 | RM24 motor |
| SERIAL_ROBOT_PATH | /dev/ttyS0 | UART to STM32 |
| ROBOT_BAUDRATE | 19200 | |
| FREEWHEEL_IS_AT_BACKSIDE | false | |
| MOWER_SIZE | 60 | cm |
| MOTOR_LEFT_SWAP_DIRECTION | 1 | Both swapped |
| MOTOR_RIGHT_SWAP_DIRECTION | 1 | Both swapped |
| MOW_MOTOR_COUNT | 1 | |
| GPS via | USB serial | /dev/serial/by-id/usb-u-blox... |
| IMU | MPU6050 | I2C 0x69 |

## Two-Wheel Turn (Custom Feature)

A custom patch modifies the unicycle model to allow the inner wheel to drive **backward** during tight turns. This prevents the mower from pivoting on one wheel, reducing turf damage:

```cpp
// If inner wheel speed drops below threshold, reverse it proportionally
if (lspeed_initial < MIN_FORWARD_SPEED_FOR_ONE_WHEEL_TURN) {
    lspeed = max(-rspeed_initial * 0.5f, INNER_WHEEL_BACKWARD_SPEED);
}
```

This feature is disabled during undocking to avoid dock contact issues.
