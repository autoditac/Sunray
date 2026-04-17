# ADR-006 — Remove Process::runShellCommand from Main Loop

| Field | Value |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-04-17 |
| **Author** | Rouven Sacha |

---

## Context

After deploying the containerized Sunray firmware on an RPi 4B, the main loop ran at **1 Hz instead of ~50 Hz**, causing PID cycle time warnings (`Ta=1.01 TaMax=0.10`) and unmet motor communication frequency (`motorFreq=1/1`).

### Investigation

Profiling with `perf record` on the `arduino-loop` thread revealed the root cause:

```
38.14%  arduino-loop  [kernel.kallsyms]  [k] el0_svc
         --- Process::available() → Process::read() → Stream::timedRead() → Stream::readString()

20.61%  arduino-loop  libc.so.6          [.] readlinkat
         --- Process::available() → Process::read() → Stream::timedRead() → Stream::readString()
```

**~67% of CPU** was spent in `Process::runShellCommand()` calls — specifically `readlinkat` syscalls for pipe I/O. Three call sites were identified:

| Call site | Command | Frequency | Impact |
|---|---|---|---|
| `robot.cpp:1180` | `ps -eo pcpu,pid,user,args \| sort -k 1 -r \| head -3` | Every loop when `loopTimeMax > 500ms` | **Root cause**: ~800ms per fork, creates feedback loop |
| `SerialRobotDriver.cpp` | `cat /sys/class/thermal/thermal_zone0/temp` | Every 59s | 10-50ms blocking |
| `SerialRobotDriver.cpp` | `wpa_cli -i wlan0 status` | Every 7s | 10-50ms blocking, `wpa_cli` not in container |
| `SerialBatteryDriver.cpp` | `cat /sys/class/thermal/thermal_zone1/temp` | Every 57s | 10-50ms blocking |

The `ps` monitoring in `robot.cpp` created a **vicious feedback loop**: the loop was already >500ms from normal GPS byte-by-byte I/O (~14K ioctl+read pairs per iteration), so `loopTimeMax > 500` was always true. This triggered `ps` every iteration, which added ~800ms, which kept `loopTimeMax` high, which triggered `ps` again.

### Options considered

| Option | Pros | Cons |
|---|---|---|
| **Remove Process forks, use direct sysfs** | Zero overhead, no fork, no pipe | Loses `ps` monitoring output |
| **Move Process calls to background thread** | Keeps monitoring | Adds threading complexity, unnecessary |
| **Reduce ps frequency with cooldown** | Simple fix | Still forks shells periodically, still 800ms each time |

## Decision

1. **Disable** the `ps` monitoring block in `robot.cpp` entirely (commented out with explanation). The loop time is already logged — the `ps` output was diagnostic-only and counterproductive since it was the cause of the very problem it tried to diagnose.

2. **Replace** CPU temperature (`thermal_zone0`) and battery temperature (`thermal_zone1`) `Process::runShellCommand("cat ...")` with direct `fopen()`/`fgets()`/`fclose()` on the sysfs file. This is non-blocking and takes <1ms.

3. **Disable** WiFi connection state monitoring (`wpa_cli`) entirely. This was a BananaPi-era feature; the container doesn't have `wpa_cli` and the host OS manages WiFi.

4. **Remove** unused `Process` member variables from `SerialRobotDriver.h` and `SerialBatteryDriver`.

## Consequences

- Main loop should run at designed frequency (~50 Hz) instead of 1 Hz
- PID motor control gets proper cycle time (20ms instead of 1000ms)
- CPU and battery temperature still monitored via direct sysfs reads
- WiFi LED status no longer updated (acceptable — LEDs show GPS and error state which are more important)
- `Process.h` include remains for one-time startup calls (`pwd`, `ip link show`)
- Loop time diagnostics still printed every 10s (just without the `ps` output)

## Files changed

| File | Change |
|---|---|
| `sunray/robot.cpp` | Commented out `Process p; p.runShellCommand("ps ...")` block |
| `sunray/src/driver/SerialRobotDriver.cpp` | Replaced `updateCpuTemperature()` and `updateBatteryTemperature()` with direct sysfs, disabled `updateWifiConnectionState()` |
| `sunray/src/driver/SerialRobotDriver.h` | Removed `Process cpuTempProcess`, `wifiStatusProcess`, `batteryTempProcess` members |
