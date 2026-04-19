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

### 1.1 Terminology — "stalled wheel" vs. "slipping wheel"

Throughout this document a **stalled wheel** means *the wheel shaft is not rotating because the applied torque is insufficient to overcome resistance*. It does **not** mean "motor unpowered and coasting". A **slipping wheel** is the opposite: the shaft rotates (encoder happy) but the contact patch does not translate over the ground because lateral scrub or traction loss exceeds friction capacity.

Three distinct sub-modes matter, and they are physically distinct:

- **Dead-zone stall** — PWM is too low to overcome the motor's *own* static friction (cogging, brush stiction, gearbox drag). Happens even on blocks, with no external load. Low motor current.
- **Load stall** — PWM is high enough to spin a free wheel, but *external* resistance (tall grass, root, slope, obstacle) exceeds the delivered torque. High motor current, near `MOTOR_OVERLOAD_CURRENT`.
- **Traction slip** — shaft rotates as commanded, but the wheel loses grip and does not translate the chassis. Motor current is **low–normal** and encoder `rpmCurr ≈ rpmSet`. **The existing `sense()` one-wheel-turn detector cannot see this — the motor encoder is on the shaft, not on the ground.**

The three produce overlapping symptoms:

| Symptom | Dead-zone stall | Load stall | Traction slip |
|---|---|---|---|
| `rpmSet` | small (< 10) | any | any |
| `rpmCurr` (encoder) | ≈ 0 | ≈ 0 | ≈ `rpmSet` |
| Motor current `i` | **low** | **high** (near overload) | **low–normal** |
| GPS-pose vs. predicted | Both drift | Both drift | **Prediction turns, GPS doesn't** |
| Caught by `sense()` one-wheel logic | Yes | Yes | **No — invisible** |
| Caught by ADR-004 fix | Partially | No | No |
| Fix path | §5.1 / §5.2 (reshape kinematic request) | Overload recovery, slow-down | §5.6 (traction-aware turn-rate limit + GPS/IMU slip detection) |

The STEER log line in §6.1 includes `iL, iR` (to separate stall types) and now also needs to expose `GPS heading rate` vs. `commanded ω` so traction slip becomes visible post-mortem (see §6.1 update).

### 1.2 Is Alfred torque-limited or traction-limited? (unresolved)

A major open question for this analysis. Two competing hypotheses:

**H1 — torque-limited (dead-zone stall dominates)**
The ADR-004 regime and §2.2 envelope derivation assume this. Inner wheel commanded below break-away PWM → doesn't rotate → mower pivots on outer wheel.

**H2 — traction-limited (slip dominates)**
Alfred has **rear-wheel drive with a nose-heavy chassis** (mow motor + front-mounted mow disc). **Measured 2026-04-19:** 7.0 kg on the rear axle out of 16.0 kg total = **43.75 % rear weight** (bathroom scale, ±0.5 kg). Per drive wheel: ~3.5 kg × 9.81 m/s² ≈ **34 N normal force**. At a friction coefficient of 0.6 on dry grass, that yields ~**20 N traction per wheel**.

What this implies for the different motion regimes:

| Regime | Governing constraint | Approx. limit at measured 43.75 % rear |
|---|---|---|
| Straight-line drive | Static friction (tiny lateral force) | Not binding |
| Line-tracking arc, `v = 0.3 m/s`, tight turn | Lateral `a = v·ω ≤ μ·g·f_rear` ≈ 2.6 m/s² | `ω_max ≈ 8.6 rad/s` — not binding |
| Line-tracking arc, `v = 0.1 m/s` | same | `ω_max ≈ 26 rad/s` — not binding |
| **Rotate-in-place on grass** | Rear-wheel lateral scrub + **front caster break-out torque** | **Often binding** — the mower "scrubs around the nose" instead of pivoting |
| Rotate-in-place, slope / wet | Reduced μ, possibly asymmetric front-caster resistance | Often binding |

So H2 is unlikely to dominate during *arc-following* at sensible speeds, but it **is** the plausible dominant mode during **rotate-in-place** (§2.4 triggers this at >20° heading error near waypoints) and during the transient at the start of tight turns where heading error is large before forward motion builds up.

This reshapes the problem:

- The current ADR-004 rotate-in-place clamp is **unreachable** (§4.4) → rotate-in-place currently relies entirely on the unicycle model outputs reaching break-away PWM. If traction is the limit, this silently fails.
- The STEER log's `wGps`/`wImu` vs. `w` comparison remains the discriminator — but expect the **clearest** H2 signature to appear during **pivot requests**, not during Stanley arc-following.

The front caster is a wildcard: if it doesn't swivel freely (bearing friction, dirt, grass wrap), the effective "rear weight fraction available for turning" is even lower than 43.75 % because some rear-wheel force is spent overcoming the caster's resistance to rotation. Q2 (caster type) remains important even with Q1 answered.

**Why the distinction matters for the fix:**

- If H1: §5.1/§5.2 envelope fix is sufficient.
- If H2: §5.1/§5.2 helps but is **not enough**. The robot would also need
  - (a) A **turn-rate cap** that respects `|ω_max| = μ·g·N_rear / (m·L/2)` rather than only the motor dead-zone cap (see §5.6).
  - (b) **Slip detection** comparing commanded ω against GPS/IMU-measured heading rate, to abort a turn when slip is detected.
  - (c) Possibly a **mechanical rebalance** (rear ballast or battery relocation) that is out of scope for firmware.
- Both fixes are compatible and complementary; if we implement both, we're robust to either mode.

**What we know vs. what we need to verify:**

| Fact | Status | Source |
|---|---|---|
| Drive wheels are at rear | Known | `FREEWHEEL_IS_AT_BACKSIDE = false` in config |
| Mow disc mounted at front | Known | Mechanical drawing |
| CoG position / rear weight fraction | **Measured: 43.75 % (7.0/16.0 kg)** | Bathroom scale, 2026-04-19, ±0.5 kg |
| Front caster type (swivel / resistance) | **Unknown** | Q2 in §3.1 |
| Motor current during a failed turn | **Unknown** | Needs STEER log (§6.1) |
| GPS/IMU heading rate during a failed turn | **Unknown** | Needs STEER log extended with `omega_gps` |
| Break-away PWM per wheel | **Unknown** | T1/T2 bench test (§6.3) |
| Tyre friction coefficient on grass | **Unknown** | Hard to measure directly; inferred from slip events |

**We cannot pick between H1 and H2 from static code analysis.** The debugging plan in §6 is now explicitly designed to discriminate them on the first instrumented run.

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

> **Q1.** ~~Weight distribution front-to-rear~~ **Answered 2026-04-19: rear axle carries 7.0 kg of 16.0 kg total → 43.75 % rear, 56.25 % front.**
>
> **Q2.** Front support type: single swivel castor, two fixed skids, or two fixed wheels? Trailing (aligns with motion) or leading (resists it)? **Is the caster bearing free-spinning, or does it have noticeable break-out torque when turned by hand?**
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

### 5.6 Traction-aware turn-rate cap and slip detection (if H2 is confirmed)

If the §1.2 debugging confirms H2 (traction-limited), the envelope in §5.1 needs a second constraint layered on top — the **friction envelope**:

$$|\omega_{\mathrm{traction}}(v)| \le \frac{2\,\mu\,g\,N_{\mathrm{rear\,fraction}}}{L}$$

where `N_rear_fraction` is the fraction of chassis weight on the drive wheels. For Alfred with **measured 43.75 % rear weight** (7.0 / 16.0 kg), the gross cap is 2 × 0.6 × 9.81 × 0.4375 / 0.39 ≈ **13 rad/s** — well above any Stanley output, so during arc-following this is **not binding**. Traction slip is expected to dominate instead during **rotate-in-place** (front-caster break-out + rear-wheel lateral scrub) where this simple envelope does not apply; see §1.2 regime table.

More interesting: the **effective** turn rate under slip is:

$$\omega_{\mathrm{effective}} = \mathrm{min}(\omega_{\mathrm{dead\text{-}zone\ envelope}}, \omega_{\mathrm{traction\ envelope}})$$

Implementation — extend the §5.1 block:

```cpp
#ifdef TRACTION_LIMIT_TURN_RATE
const float w_traction = 2.0f * MU_GRASS * 9.81f * REAR_WEIGHT_FRACTION / (WHEEL_BASE_CM / 100.0f);
float w_limit = fmin(w_envelope, w_traction);
if (fabs(w) > w_limit) w = copysignf(w_limit, w);
#endif
```

With config entries:

```cpp
#define TRACTION_LIMIT_TURN_RATE     1
#define MU_GRASS                     0.6f    // conservative dry grass
#define REAR_WEIGHT_FRACTION         0.4375f // measured 2026-04-19: 7.0/16.0 kg
```

**Slip detection (online):** once per control cycle, compare commanded `ω` against measured heading rate from IMU gyroscope (already available, `imu.gyroZ`) or GPS heading differentiation:

```cpp
float omega_measured = imu.gyroZ;   // or (heading - lastHeading)/dt from GPS
float slip_ratio = (fabs(angularSpeedSet) > 0.1f)
                 ? fabs(angularSpeedSet - omega_measured) / fabs(angularSpeedSet)
                 : 0.0f;
if (slip_ratio > SLIP_THRESHOLD && slip_duration_ms > 500) {
  // abort current tight turn, switch to rotate-in-place at lower rate
  // or request replanning
  CONSOLE.println("SLIP: traction lost during turn — aborting arc");
  // fall-back: pivot-in-place at reduced rate until heading error < 10°
}
```

This is the missing feedback loop: the motor encoder lies (it reports shaft rotation, not ground motion); IMU/GPS heading rate is the only honest signal during a slip event. **No slip-detection currently exists in Sunray.**

### 5.7 Layering of the proposed fixes

Three layers, each catching a different failure mode, in order of where they act:

1. **§5.1 (controller envelope)** — request-shaping. Prevents commanding unrealisable (v, ω). Dead-zone aware.
2. **§5.6 (traction envelope + slip detection)** — physics-aware clamp + online abort. Traction aware.
3. **§5.2 (motor-level clamp with feedback)** — safety net if the above are bypassed or miscalibrated.
4. **§5.3 (PID integrator bleed)** — wind-up protection during residual stalls.

Phase 2 of the roll-out (§7) implements layers 1 and 3. Phase 2b (conditional on H2 confirmation) adds layer 2.

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
  CONSOLE.print(" wGps=");    CONSOLE.print(stateEstimator.angularSpeedMeasured, 3);
  CONSOLE.print(" wImu=");    CONSOLE.print(imu.gyroZ, 3);
  CONSOLE.println();
}
```

Rationale: `v, w` = what was asked for; `rpmset` = unicycle translation; `rpm` = what wheels did (encoder, shaft side); `pwm, i` = actuator effort; **`wGps, wImu` = what the chassis actually did over the ground**.

Failure-mode discrimination from a single log line:

| Mode | `rpmset` | `rpm` (enc) | `i` | `wImu` vs. `w` |
|---|---|---|---|---|
| Dead-zone stall | > 5 | ≈ 0 | low | `wImu < w` |
| Load stall | any | ≈ 0 | **high** | `wImu < w` |
| Traction slip | any | **≈ `rpmset`** | low–normal | **`wImu ≪ w`** |
| Healthy turn | any | ≈ `rpmset` | normal | `wImu ≈ w` |

This table is the primary diagnostic output of Phase 1 logging — it resolves §1.2 H1 vs. H2 directly.

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
