# ADR-004 — Two-Wheel-Turn Fix for Uneven Terrain

| Field | Value |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-04-16 |
| **Author** | Rouven Sacha |

---

## Context

Alfred mowers use a unicycle kinematic model where turns are driven by a speed differential between the two rear wheels:

```
VR = V + omega * L/2
VL = V - omega * L/2
```

During a turn, the outer wheel speeds up while the inner wheel slows to near-zero. This means effectively **only the outer wheel drives the turn** — the inner wheel is near-stationary and contributes no turning torque.

Alfred has a nose-heavy weight distribution. The single outer wheel often cannot generate enough traction to pivot this heavy front end. It spins freely, losing grip and digging ruts into soft ground instead of turning the mower. This is one of the most common complaints from Alfred owners — the mower buries itself on turns, especially on wet or uneven terrain.

The batman mower exhibited this problem repeatedly — the outer wheel would spin and dig, the mower wouldn't turn, triggering GPS timeout recovery loops and leaving it stuck.

### Root cause analysis

Three issues in the original code path:
1. Only the outer wheel drives the turn — the inner wheel is near-stationary and contributes no torque
2. The heavy nose requires more force to pivot than a single wheel can provide on soft ground
3. PID controller is working correctly — the outer wheel reaches its commanded speed, but it has no traction, so the speed is achieved by spinning freely

### Options considered

| Option | Pros | Cons |
|---|---|---|
| **Increase minimum wheel speed** | Simple | Breaks tight turns, changes path geometry |
| **PID tuning** | No code change | Can't fix mechanical stall |
| **Drive inner wheel backward** | Both wheels contribute torque — creates a true differential pivot | Needs careful thresholds, changes turn behavior |
| **Skip problematic turns** | Avoids the issue | Not a real solution |

## Decision

When the inner wheel would be commanded at a low positive speed during a turn, **drive it backward proportionally** to the outer wheel speed. This uses both wheels to create a differential pivot, distributing the turning force across both drive wheels instead of relying on a single wheel that lacks traction.

Implementation in `sunray/motor.cpp` within `setLinearAngularSpeed()`:

```cpp
#ifdef TWO_WHEEL_TURN_SPEED_THRESHOLD
  // Differential pivot: when inner wheel would be near-stationary, drive it
  // backward so both wheels contribute turning torque (reduces load on outer wheel)
  if (leftSpeed > 0 && leftSpeed < TWO_WHEEL_TURN_SPEED_THRESHOLD && 
      fabs(rightSpeed) > TWO_WHEEL_TURN_SPEED_THRESHOLD) {
    leftSpeed = -fabs(rightSpeed) * TWO_WHEEL_TURN_INNER_FACTOR;
  }
  // mirror for right wheel
  if (rightSpeed > 0 && rightSpeed < TWO_WHEEL_TURN_SPEED_THRESHOLD && 
      fabs(leftSpeed) > TWO_WHEEL_TURN_SPEED_THRESHOLD) {
    rightSpeed = -fabs(leftSpeed) * TWO_WHEEL_TURN_INNER_FACTOR;
  }
#endif
```

Config defines:
```cpp
#define TWO_WHEEL_TURN_SPEED_THRESHOLD  0.12  // m/s — below this, inner wheel is idle
#define TWO_WHEEL_TURN_INNER_FACTOR     0.3   // backward speed as fraction of outer
```

### Guard conditions

- Only activates when inner wheel speed is **positive but below threshold** — i.e. it would be near-stationary during a turn
- Excluded during **docking/undocking** (precise positioning needed)
- Guarded by `#ifdef` so it can be disabled per-mower via config

## Consequences

- Turn radius changes slightly (tighter turns due to backward inner wheel)
- Path tracking controller (Stanley) adapts naturally since actual heading converges faster
- Must be tested on both mowers — terrain characteristics differ
- Values (0.12 m/s threshold, 30% factor) may need per-mower tuning in future
