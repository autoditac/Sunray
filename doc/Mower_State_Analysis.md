# Mower State Analysis: Robin & Batman

## Date: 2026-04-16

---

## Robin (ssh robin)

### System
- **Hostname**: robin
- **OS**: Debian GNU/Linux 12 (bookworm)
- **Arch**: aarch64 (Raspberry Pi 4B)
- **Kernel**: 6.12.25+rpt-rpi-v8

### Sunray State
- **Base commit**: `30630f1` (new config parameter: TARGET_ANGLE_TOLERANCE)
- **Branch**: `master`
- **Commits behind upstream**: **152** (upstream is at `eb4891c`)
- **Repo structure**: Uses `alfred/` directory (not `linux/`)
- **Service**: Running since 2025-10-26 (5+ months uptime!)
- **Binary**: `/home/pi/Sunray/alfred/build/sunray`
- **Service script path**: `/home/pi/Sunray/alfred/start_sunray.sh`
- **Firmware version**: ~1.0.327 era (based on commit timeline)

### Code Changes (Uncommitted)
**2 files modified, uncommitted:**

1. **`sunray/src/op/Op.cpp`**: Removed `RelocalizationOp` instance and `onRelocalization()` method
2. **`sunray/src/op/RelocalizationOp.cpp`**: **Deleted entirely**

**Purpose**: RelocalizationOp was added for lidar-based relocalization. Robin doesn't use lidar, so this was removed - possibly to fix a compilation error or unwanted behavior.

**Risk**: Upstream still has RelocalizationOp. This change will need to be reconciled - either by keeping the removal (if no lidar) or by leaving it in (it should only activate if lidar calls it).

### Config (`sunray/config.h` - compiled from `config_alfred.h`)
- **Standard Alfred config** - matches stock `config_alfred.h` from that commit
- Key settings:
  - `ENABLE_LIFT_DETECTION 1` ✓
  - `MOW_TOGGLE_DIR true`
  - `RAIN_ENABLE true`
  - `BUZZER_ENABLE 1` ✓
  - `SERIAL_ROBOT_PATH "/dev/ttyS0"`
  - NTRIP: sapos NRW (`www.sapos-nw-ntrip.de`)

### Local Branches (not active)
- `feature/backport_fixes_for_failing_map_upload`
- `feature/two-wheel-turn`
- `main`

---

## Batman (ssh batman)

### System
- **Hostname**: batman
- **OS**: Debian GNU/Linux 12 (bookworm)
- **Arch**: aarch64 (Raspberry Pi 4B)
- **Kernel**: 6.12.20+rpt-rpi-v8

### Sunray State
- **Base commit**: `d934b50` (add ublox decode test) — much older than robin!
- **Active branch**: `feature/backport_fixes_for_failing_map_upload`
- **Commits behind upstream**: **574** (!) — very far behind
- **Repo structure**: Uses `alfred/` directory
- **Service**: Running since 2025-11-15 (5+ months uptime!)
- **Binary**: `/home/pi/Sunray/alfred/build/sunray`
- **Firmware version**: ~1.0.324 era

### Custom Commits (4 commits on top of master)

1. **`5ccdde5`** - "refactor: enhance wifi client handling and improve map point data types"
   - `httpserver.cpp`: Added wifi client availability wait, verbose logging on slow HTTP
   - `map.cpp`: Extended getCenter() range (9999→99999), added DOCK_FRONT_SIDE logic, flexible getDockingPos with idx parameter, new dock helper methods, fixed trackReverse logic
   - `map.h`: **Changed Point from `short` to `float` on Linux** (critical fix for large gardens!), added `isBetweenLastAndNextToLastDockPoint()`, `isTargetingLastDockPoint()`, `isTargetingNextToLastDockPoint()`

2. **`5e87218`** - "add .gitignore to exclude alfred build artifacts"

3. **`45b6517`** - "fix: update buzzer enable configuration and correct serial robot path"
   - Config: changed BUZZER_ENABLE and SERIAL_ROBOT_PATH

4. **`7df8227`** - "feat: add docking configuration option and refactor command processing"
   - Config: added docking config option
   - `httpserver.cpp`: AT command processing tweak
   - `map.cpp/h`: Map data type fix

### Uncommitted Changes (config.h only)
```diff
- ENABLE_LIFT_DETECTION  1        → commented out (disabled)
- LIFT_OBSTACLE_AVOIDANCE 1      → commented out (disabled)
- MOW_TOGGLE_DIR  true           → false
- RAIN_ENABLE true               → false
```

### Config Differences vs Robin
| Setting | Robin | Batman | Notes |
|---------|-------|--------|-------|
| ENABLE_LIFT_DETECTION | 1 (enabled) | commented out | Batman disabled lift sensor |
| LIFT_OBSTACLE_AVOIDANCE | 1 (enabled) | commented out | |
| MOW_TOGGLE_DIR | true | false | Batman: always same direction |
| RAIN_ENABLE | true | false | Batman: ignore rain |
| BUZZER_ENABLE | 1 | (missing/changed) | Batman modified buzzer |
| NTRIP_HOST | www.sapos-nw-ntrip.de | 195.227.70.119 | Batman uses IP directly |

### Feature Branches (inactive but present)
- `feature/two-wheel-turn` - Custom motor control for inner wheel reverse during turns
- `main` - older branch

---

## Upstream Changes Analysis (since batman's base d934b50)

### 574 commits of upstream changes. Key areas:

#### Already Incorporated Upstream (batman's fixes merged!)
Many of batman's custom changes have been **independently implemented upstream**:
- ✅ `Point` type changed to `float` on Linux (`map.h`)
- ✅ `getDockingPos` with idx parameter
- ✅ `DOCK_FRONT_SIDE` support in map.cpp
- ✅ `getCenter()` range expanded to 99999
- ✅ `isBetweenLastAndNextToLastDockPoint()` and helper methods
- ✅ `trackReverse` DOCK_FRONT_SIDE-aware logic
- ✅ `resetImuTimeout` refactored to `stateEstimator.resetImuTimeout()`

#### New Upstream Features NOT on Mowers
- WebSocket client support (ENABLE_WS_CLIENT, TLS)
- HttpServer refactored into a class
- Camera streaming (CameraStreamer)
- Relay board control via CAN
- IP address send via CAN
- Thread management for sendIpAddress/sendWifiSignal
- Code refactoring: robot.cpp, LineTracker, Stats, Comm, StateEstimator
- April-tag visual docking support
- DOCK_GUIDANCE_SHEET option
- DOCK_DETECT_OBSTACLE_IN_DOCK option
- ODO_TEST_PWM_SPEED config option
- MOTOR_MOW_SWAP_DIRECTION config option

#### Potential Conflict Areas
1. **`httpserver.cpp`**: Completely refactored upstream → batman's wifi verbose changes cannot be cherry-picked cleanly. However, batman's approach (wait for client available) may no longer be needed with the upstream refactoring.

2. **`map.cpp/map.h`**: Most of batman's changes are already upstream. Clean merge expected.

3. **RelocalizationOp** (robin): Upstream still has it. It's harmless if lidar isn't used - recommend keeping it.

4. **Motor two-wheel-turn** (feature branch): Not upstream. Clean patch against motor.cpp - should apply with minor adjustments.

---

## Summary

| Aspect | Robin | Batman |
|--------|-------|--------|
| Base commit | 30630f1 (~mid 2025) | d934b50 (~late 2024) |
| Behind upstream | 152 commits | 574 commits |
| Custom code changes | Small (RelocalizationOp removal) | Significant (map/dock/http fixes) |
| Config customization | Stock Alfred | Modified (no lift, no rain, no mow toggle) |
| Merge difficulty | Easy | Medium (code already upstream, http refactored) |
| Has custom branches | Yes (not active) | Yes (active on feature branch) |
