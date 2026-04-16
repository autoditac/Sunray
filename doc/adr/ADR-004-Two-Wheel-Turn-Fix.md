# ADR-004 — Two-Wheel-Turn Fix for Uneven Terrain

| Field | Value |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-04-16 |
| **Author** | Rouven Sacha |

---

## Context

Alfred mowers use a unicycle kinematic model where the inner wheel slows down during turns:

```
VR = V + omega * L/2
VL = V - omega * L/2
```

On uneven terrain, when the inner wheel is commanded at a low positive speed (near zero), it stalls due to ground resistance. The PID controller cannot compensate because the setpoint is legitimately low. This causes the mower to either stop turning or push sideways, leading to path tracking errors and obstacle escape failures.

The batman mower exhibited this problem repeatedly — wheels would stall during turns on sloped terrain, triggering GPS timeout recovery loops.

### Root cause analysis

Three issues in the original code path:
1. No handling for the "slow inner wheel during turns" case
2. PID tuning alone cannot fix mechanical stall at low speeds
3. The unicycle model assumes ideal ground contact

### Options considered

| Option | Pros | Cons |
|---|---|---|
| **Increase minimum wheel speed** | Simple | Breaks tight turns, changes path geometry |
| **PID tuning** | No code change | Can't fix mechanical stall |
| **Drive inner wheel backward** | Physically correct — creates pivot | Needs careful thresholds, changes turn behavior |
| **Skip problematic turns** | Avoids the issue | Not a real solution |

## Decision

When the inner wheel would be commanded at a low positive speed during a turn, **drive it backward proportionally** to the outer wheel speed. This creates a differential pivot that works with ground resistance instead of against it.

Implementation in `sunray/motor.cpp` within `setLinearAngularSpeed()`:

```cpp
#ifdef TWO_WHEEL_TURN_SPEED_THRESHOLD
  // inner wheel stall prevention: drive backward when slow
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
#define TWO_WHEEL_TURN_SPEED_THRESHOLD  0.12  // m/s — below this, inner wheel stalls
#define TWO_WHEEL_TURN_INNER_FACTOR     0.3   // backward speed as fraction of outer
```

### Guard conditions

- Only activates when inner wheel speed is **positive but below threshold** (not during reverse or stop)
- Excluded during **docking/undocking** (precise positioning needed)
- Guarded by `#ifdef` so it can be disabled per-mower via config

## Consequences

- Turn radius changes slightly (tighter turns due to backward inner wheel)
- Path tracking controller (Stanley) adapts naturally since actual heading converges faster
- Must be tested on both mowers — terrain characteristics differ
- Values (0.12 m/s threshold, 30% factor) may need per-mower tuning in future
