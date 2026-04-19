# Steering Control Deep-Dive Analysis (2026-04-19)

| Field | Value |
|---|---|
| **Status** | In Progress |
| **Date** | 2026-04-19 |
| **Author** | Rouven Sacha |
| **Scope** | Re-analysis of Sunray steering for Alfred; validation of ADR-004 fix; proposal for remaining stall issues |
| **Related** | [ADR-004](adr/ADR-004-Two-Wheel-Turn-Fix.md), [Motor_Control_Alfred.md](Motor_Control_Alfred.md) |

---

## 1. Purpose

Despite the ADR-004 fix currently deployed on the mowers, batman continues to exhibit one-wheel-turn stalls. This document:

1. Re-derives the full steering control pipeline (Stanley → unicycle → PID → PWM)
2. Analyses Alfred's chassis mechanics and where the pipeline fails on it
3. Audits the currently deployed ADR-004 patch for correctness — **identifies three concrete bugs**
4. Proposes a physically-grounded replacement algorithm
5. Defines an instrumented debugging plan (including video capture points)

Read top-to-bottom — sections depend on each other.

### 1.1 Terminology — "stalled wheel"

Throughout this document a **stalled wheel** means *the wheel is being commanded but is not rotating because the applied torque is insufficient to overcome resistance*. It does **not** mean "motor unpowered and coasting". Two sub-types matter, and they are physically distinct:

- **Dead-zone stall** — PWM is too low to overcome the motor's *own* static friction (cogging, brush stiction, gearbox drag). Happens even on blocks, with no external load. This is the dominant failure mode of the current ADR-004 regime: we command a tiny RPM setpoint, the PID outputs a small PWM, and the motor never breaks away.
- **Load stall** — PWM is high enough to spin a free wheel, but *external* resistance (tall grass, root, slope, obstacle) exceeds the delivered torque. Targeted by the existing `MOTOR_OVERLOAD_CURRENT` / `sense()` logic.

Both produce the same observable: `rpmSet > 0, rpmCurr ≈ 0`. They are distinguished by motor current:

| Symptom | Dead-zone stall | Load stall |
|---|---|---|
| `rpmSet` | small (< 10) | any |
| `rpmCurr` | ≈ 0 | ≈ 0 |
| Motor current `i` | **low** | **high** (near overload) |
| Fix path | §5.1 / §5.2 (reshape kinematic request) | overload recovery, slow-down, row spacing |

The STEER log line in §6.1 includes `iL, iR` specifically so the two can be separated in post-mortem.

---

## 2. The Steering Control Pipeline

```
LineTracker (Stanley) ──(v, ω)──▶ Motor::setLinearAngularSpeed ──(rpmSet)──▶
                                  (unicycle + MIN_WHEEL_SPEED)
  Motor::control (per-wheel PID) ──(PWM)──▶ SerialRobotDriver AT+M ──▶ STM32
```

### 2.1 Stanley controller — `LineTracker.cpp:176`

```cpp
angular = p * trackerDiffDelta
        + atan2(k * lateralError, STANLEY_CONTROL_K_SOFT + fabs(linearSpeedSet));
```

Alfred tuning (`config_alfred.h:445`):

| Gain | Alfred | Upstream | Effect |
|---|---|---|---|
| `P_NORMAL` | **2.0** | 3.0 | Heading-error proportional |
| `K_NORMAL` | **0.5** | 1.0 | Lateral cross-track gain |
| `K_SOFT`   | **0.2** | 0.001 | Softening at low speed (fix #144) |

**Behavioural envelope** (heading error = 0, pure lateral correction):

| `|v|` (m/s) | `lateralError` (m) | `angular = atan2(0.5·e, 0.2+|v|)` |
|---|---|---|
| 0.30 | 0.10 | ≈ 0.10 rad/s |
| 0.10 | 0.10 | ≈ 0.17 rad/s |
| 0.10 | 0.30 | **≈ 0.46 rad/s** |
| 0.05 | 0.30 | **≈ 0.54 rad/s** |
| 0.05 | 0.50 | **≈ 0.79 rad/s** |

The bottom rows are the dangerous regime — arising whenever the mower slows near a waypoint (~0.1 m/s), overloads (`MOTOR_OVERLOAD_SPEED = 0.1`), or hits a GPS-float zone — while still off-track.

### 2.2 Unicycle model — `motor.cpp:156`

```cpp
rspeed = linearSpeedSet + angular * L/2;   // L = 0.39 m
lspeed = linearSpeedSet - angular * L/2;   // L/2 = 0.195 m
```

**Inner-wheel dead zone** entered whenever `|inner| < MIN_WHEEL_SPEED (0.05)`:

$$|v| - 0.195 \cdot |\omega| < 0.05 \quad\Longleftrightarrow\quad |\omega| > \frac{|v| - 0.05}{0.195}$$

At `v = 0.10` the threshold is **0.256 rad/s**. Stanley output of 0.46 rad/s is **80 % over** the boundary.
At `v = 0.05`, threshold is **0** — *any* turn stalls the inner wheel.

**This is the geometric heart of the problem. It is not a PID tuning issue — it is the kinematic model asking for a wheel speed the hardware cannot deliver.**

### 2.3 PID → PWM — `motor.cpp:508`

Per-wheel `Kp=0.5, Ki=0.01, Kd=0.01`. **Velocity-form accumulator**:

```cpp
motorLeftPWMCurr = motorLeftPWMCurr + motorLeftPID.y;
```

- If commanded RPM is unreachable (stalled inner wheel), integrator winds up until anti-wind-up clamps at `±255`.
- During wind-down after the command drops, residual PWM **continues to turn the mower**.
- Control cycle gated at 50 ms.

### 2.4 Rotate-in-place vs. line-tracking — `LineTracker.cpp:82`

- Near waypoint (< 0.5 m) or far off path (> 0.5 m): heading error > 20° → rotate at **0.506 rad/s**.
- Mid-segment: rotate only at > 45°.
- **Critical gap**: angles between 20° and 45° in mid-segment are handled by Stanley — precisely the turns that stall the inner wheel at low speed.

---

## 3. Alfred Chassis Mechanics

| Property | Value | Implication |
|---|---|---|
| Mass | 16 kg | High static friction |
| Wheelbase | 39 cm | Short — rotations have small leverage |
| Drive wheels | Rear, 205 mm | Large — low PWM = low torque |
| Front | Passive caster/skid (nose-heavy) | Fights direction change on sticky ground |
| Mow disc | Front-mounted | Moves CoG forward |
| PID output | PWM ±255 | Asymmetric static-friction break-away |

**Observed failure modes:**

1. Inner wheel commanded to low forward speed → PWM below break-away → stall
2. Mower pivots on outer wheel alone → digs turf
3. PID integrator winds up → when command drops, residual PWM continues
4. Stanley re-amplifies angular because robot didn't reach expected pose → escalation until GPS-stale/obstacle recovery

### 3.1 Hardware questions — please answer before final tuning

> **Q1.** Weight distribution front-to-rear (tilt onto one axis on a bathroom scale)?
>
> **Q2.** Front support type: single swivel castor, two fixed skids, or two fixed wheels? Trailing (aligns with motion) or leading (resists it)?
>
> **Q3.** Tyre material: rubber lug, smooth plastic, foam?
>
> **Q4.** **Break-away PWM per wheel on level concrete**: step `AT+M,<pwm>,0,0` and `AT+M,0,<pwm>,0` from 0 upward; note the PWM at first visible motion.
>
> **Q5.** Stall-current observations in `ERROR motor overload` logs vs. `MOTOR_OVERLOAD_CURRENT = 1.2 A`. Symmetric L/R?
>
> **Q6.** Rear-wheel camber or toe-in? Unequal rolling resistance is an unmodelled bias source.
>
> **Q7.** Straight-line drift on flat ground with `angular = 0` — consistent left/right bias, in cm per metre?

---

## 4. Audit of the Currently-Deployed ADR-004 Fix

Currently in `sunray/motor.cpp` lines 159–240.

### 4.1 Bug #1 — Dead-zone gap for small negative inner-wheel commands

```cpp
if (linearSpeedSet > 0.01f) {
  if (lspeed >= 0 && lspeed < MIN_WHEEL_SPEED && rspeed >= MIN_WHEEL_SPEED) {
```

Counter-example: `linear = 0.10`, `angular = 0.56`:

- `lspeed = 0.10 − 0.109 = −0.009` → in `(−0.05, 0)`
- `lspeed >= 0` is **false** → no clamp
- Wheel stalls in the negative dead zone — **exactly the scenario the fix was intended to prevent**.

### 4.2 Bug #2 — Angular rate silently altered; controller sees phantom error

When `lspeed` clamped to `MIN_WHEEL_SPEED`:

$$\omega_{\mathrm{actual}} = \frac{r_{\mathrm{speed}} - \mathrm{MIN\_WHEEL\_SPEED}}{L}$$

Worked example: Stanley asked `ω = 0.46`, `v = 0.10`:

- `rspeed = 0.190`, `lspeed_cmd = 0.010` → clamped to 0.050
- `ω_actual = (0.190 − 0.050)/0.39 = 0.359` — **22 % reduction**
- `v_actual = 0.120` — **20 % overshoot**

Result: robot turns slower, moves faster than requested. Lateral error grows → Stanley cranks angular → clamp kicks harder → **positive feedback loop** until saturation.

### 4.3 Bug #3 — Chattering at clamp boundary

No hysteresis. Tiny oscillation of `lspeed` around `MIN_WHEEL_SPEED` (from encoder-LPF noise through Stanley cross-track) toggles the clamp at 50 Hz → PID derivative spike → PWM ripple → mechanical whine.

### 4.4 Rotate-in-place branch is unreachable

```cpp
if (lspeed >= 0 && lspeed < MIN_WHEEL_SPEED
    && rspeed >= 0 && rspeed < MIN_WHEEL_SPEED && ...)
```

For any non-trivial pivot (`linear=0, angular=0.5`): `lspeed=-0.098, rspeed=+0.098` — both **outside** `[0, MIN_WHEEL_SPEED)`. The counter-rotate clamp **never runs**.

### 4.5 Verdict

| ADR-004 claim | Verdict |
|---|---|
| Guarantees minimum wheel speed | **False** — small negative inner-wheel commands slip through |
| Preserves commanded angular rate | **False** — silently alters both v and ω |
| Prevents stall during tracking | **Partial** — catches some cases, misses low-speed regime |
| Counter-rotates during rotation-in-place | **Unreachable** for typical pivot |

The ADR decision was correct in spirit; **the implementation is buggy and needs replacement, not tuning.**

---

## 5. Proposed Replacement Algorithm

### 5.1 Geometric angular-rate envelope in the controller (PRIMARY FIX)

Before `setLinearAngularSpeed`, reshape the (v, ω) request so the unicycle output is physically realisable:

$$|\omega_{\max}(v)| = \frac{\max(0, |v| - \mathrm{MIN\_WHEEL\_SPEED})}{L/2}$$

If desired `|ω|` exceeds the envelope:

- `|ω| > PIVOT_ANGULAR_THRESHOLD` (≈ 0.4 rad/s) → **pivot** (`v = 0`)
- else → **slow down** to `v_required = MIN_WHEEL_SPEED + (L/2)·|ω|`

```cpp
#ifdef MIN_WHEEL_SPEED
const float L_half = WHEEL_BASE_CM / 100.0f / 2.0f;
float v = linear, w = angular;
float w_envelope = fmax(0.0f, fabs(v) - MIN_WHEEL_SPEED) / L_half;
if (fabs(w) > w_envelope) {
  if (fabs(w) > PIVOT_ANGULAR_THRESHOLD) {
    v = 0.0f;                                     // rotate in place
  } else {
    float v_required = MIN_WHEEL_SPEED + L_half * fabs(w);
    v = copysignf(v_required, v);                 // slow down
  }
}
linear = v; angular = w;
#endif
```

### 5.2 Motor-level dead-zone safety net (SECONDARY)

Correct & hysteretic, with closed-loop feedback to Stanley:

```cpp
const float ENTER = MIN_WHEEL_SPEED;         // 0.050
const float EXIT  = MIN_WHEEL_SPEED * 1.4f;  // 0.070

auto clampOut = [&](float& s, int sign){
  if (sign > 0 && s > -ENTER && s < ENTER)  s = ENTER;
  if (sign < 0 && s > -ENTER && s < ENTER)  s = -ENTER;
};
int travelSign = (fabs(linearSpeedSet) >= 0.01f)
               ? (linearSpeedSet > 0 ? 1 : -1)
               : (angularSpeedSet > 0 ? 1 : -1);
clampOut(lspeed, travelSign);
clampOut(rspeed, travelSign);

// feed back the ACTUAL commanded values so Stanley's next cycle uses correct v
linearSpeedSet  = 0.5f * (rspeed + lspeed);
angularSpeedSet = (rspeed - lspeed) / (L_half * 2.0f);
```

The feedback step is critical — it closes the loop and kills the positive feedback in §4.2.

### 5.3 PID integrator bleed under suspected stall

```cpp
if (fabs(motorLeftPID.y) >= pwmMax && fabs(motorLeftRpmCurr) < 1.0f)
  motorLeftPID.esum *= 0.9f;   // bleed during stall
```

Only if §6 logs show wind-up > 3 s in practice.

### 5.4 Config additions — `config_alfred.h`

```cpp
#define MIN_WHEEL_SPEED              0.05f    // unchanged
#define PIVOT_ANGULAR_THRESHOLD      0.40f    // rad/s
#define MIN_WHEEL_SPEED_HYSTERESIS   1.4f     // exit = 1.4 × enter
```

### 5.5 Why this is not just more clamping

The current fix operates **inside the unicycle model** and clamps one wheel while lying to the controller about the other. The proposed fix operates **before the unicycle model**, reshaping the (v, ω) request so the model's output is physically realisable, and uses the motor-level clamp only as a safety net with controller feedback.

---

## 6. Debugging Plan

### 6.1 Structured log line (10 Hz during motion)

Add at end of `Motor::control()`:

```cpp
static unsigned long lastSteerLog = 0;
if (fabs(motorLeftRpmSet) + fabs(motorRightRpmSet) > 0.5f
    && millis() - lastSteerLog > 100) {
  lastSteerLog = millis();
  CONSOLE.print("STEER:");
  CONSOLE.print(" v=");       CONSOLE.print(linearSpeedSet, 3);
  CONSOLE.print(" w=");       CONSOLE.print(angularSpeedSet, 3);
  CONSOLE.print(" rpmLset="); CONSOLE.print(motorLeftRpmSet, 1);
  CONSOLE.print(" rpmRset="); CONSOLE.print(motorRightRpmSet, 1);
  CONSOLE.print(" rpmL=");    CONSOLE.print(motorLeftRpmCurrLP, 1);
  CONSOLE.print(" rpmR=");    CONSOLE.print(motorRightRpmCurrLP, 1);
  CONSOLE.print(" pwmL=");    CONSOLE.print(motorLeftPWMCurr);
  CONSOLE.print(" pwmR=");    CONSOLE.print(motorRightPWMCurr);
  CONSOLE.print(" iL=");      CONSOLE.print(motorLeftSenseLP, 2);
  CONSOLE.print(" iR=");      CONSOLE.print(motorRightSenseLP, 2);
  CONSOLE.print(" lat=");     CONSOLE.print(stateEstimator.lateralError, 3);
  CONSOLE.println();
}
```

Rationale: `v, w` = what was asked for; `rpmset` = unicycle translation; `rpm` = what wheels did; `pwm, i` = actuator effort. Stalls visible as `rpmset ≠ rpm` + `pwm saturating` + `i rising`.

### 6.2 Video recording — record on batman, note wall-clock start

| # | Condition | What to capture | Duration |
|---|---|---|---|
| V1 | First Stanley correction after GPS jump | Straight mowing, induce ~20 cm sideways lateral error | 30 s |
| V2 | Tight U-turn at row end | Normal pattern, one row-end turn | 60 s |
| V3 | Rotate-in-place | Pivot command via CaSSAndRA or manual AT test | 30 s |
| V4 | Standstill → curve | Dock exit onto a curve | 30 s |
| V5 | Overload recovery | Tall grass or small obstacle | 60 s |

Overhead phone camera ~2 m height is ideal; waist-height diagonal also fine.

### 6.3 Synthetic bench tests — wheels off ground

| Test | Sequence | Measures |
|---|---|---|
| T1 Break-away L | 0,5,0 → 0,10,0 → … 0,30,0 (1 s each) | Left static-friction PWM |
| T2 Break-away R | 5,0,0 → 10,0,0 → … 30,0,0 | Right static-friction PWM, asymmetry |
| T3 Dead-zone sweep | v=0.08, ω stepped 0.1→0.8 rad/s | ω where inner stalls |
| T4 Wind-up | Block one wheel, drive 5 s, release | Saturation + overshoot when freed |

### 6.4 Log collection recipe

```bash
ssh pi@batman.local 'sudo journalctl -u sunray.service --since "30 min ago" \
  --no-pager -o short-iso \
  | grep -E "STEER:|ONE-WHEEL:|MIN_WHEEL_SPEED|overload|obstacle|KIDNAP"' \
  > ~/batman-steer-$(date +%Y%m%d-%H%M).log
```

### 6.5 Validation criteria — over 10-min mowing session

1. `ONE-WHEEL:` events → **zero**
2. Any remaining `MIN_WHEEL_SPEED adj:` with `|w_actual - w_requested|/|w_requested| < 0.1`
3. **Zero** `ERROR motor overload` from turn-stalls
4. **Zero** `KIDNAP_DETECT` from path drift > 0.5 m
5. Video V2: no wheel spin-up digging turf on row-end turn

---

## 7. Roll-out Plan

One feature/fix per PR (Sunray fork workflow):

1. **Phase 0** — Merge this analysis, no firmware change
2. **Phase 1** — Add STEER logging (§6.1), deploy `:alpha`, collect V1–V5 baselines
3. **Phase 2** — Implement §5.1 + §5.2, deploy `:alpha`, re-record V1–V5
4. **Phase 3** — Tune `PIVOT_ANGULAR_THRESHOLD`, hysteresis, `K_SOFT` from logs
5. **Phase 4** — 48 h continuous mow on batman, confirm §6.5 criteria
6. **Phase 5** — Git tag → `:latest` → auto-update to robin

---

## 8. Open Questions for the Operator

1. Answers to **§3.1 Q1–Q7** (chassis measurements)
2. Can **break-away PWM tests T1, T2** be run this week on blocks?
3. Is there a historical log from **before ADR-004** was applied? (A/B baseline)
4. Row-end turn preference: slow-down-plus-wider-arc, or always rotate-in-place?

---

## 9. Appendix — Relevant Source Locations

| File | Lines | Concern |
|---|---|---|
| [sunray/LineTracker.cpp](../sunray/LineTracker.cpp) | 28–80 | Rotation-vs-tracking decision |
| [sunray/LineTracker.cpp](../sunray/LineTracker.cpp) | 140–180 | Stanley + softening |
| [sunray/motor.cpp](../sunray/motor.cpp) | 142–245 | Unicycle + ADR-004 clamp |
| [sunray/motor.cpp](../sunray/motor.cpp) | 440–540 | sense() + one-wheel logging |
| [sunray/motor.cpp](../sunray/motor.cpp) | 508–570 | control() PID → PWM |
| [linux/config_alfred.h](../linux/config_alfred.h) | 113, 161, 168–173, 445–454 | All relevant tunables |

---

## 10. Appendix — Glossary

- **Dead zone (motor)**: PWM range where commanded torque cannot overcome static friction; wheel does not rotate.
- **Dead zone (kinematic)**: Inner-wheel speed range `|v| < MIN_WHEEL_SPEED`, where the unicycle command maps to a motor PWM inside the motor dead zone.
- **Stanley controller**: Path-tracking law combining heading error (P term) with lateral cross-track error normalised by speed (atan2 term).
- **Pivot / rotate-in-place**: `linear = 0`, both wheels commanded equal-and-opposite.
- **Break-away PWM**: Lowest PWM that rotates the wheel on a given surface.
