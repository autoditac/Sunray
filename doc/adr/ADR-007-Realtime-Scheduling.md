# ADR-007 — Real-Time Scheduling, mlockall, and UART Low-Latency

| Field | Value |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-04-17 |
| **Author** | Rouven Sacha |

---

## Context

After fixing the main loop timing issue (ADR-006), the firmware loop ran at ~0.3ms mean. However, analysis of the Linux runtime environment revealed three latency sources that could cause occasional timing jitter:

### 1. Broken `thread_set_priority()` — SCHED_OTHER instead of SCHED_FIFO

`linux/src/wiring_thread.c` called `sched_setscheduler(0, SCHED_OTHER, &sched)` with priority 65. Since `SCHED_OTHER` max priority is 0, the priority was silently clamped — effectively a no-op. The upstream code had `SCHED_FIFO` commented out in `wiring_main.cpp`. All threads ran as `TS` (timesharing) at priority 0, competing with every other process on the system.

### 2. No memory locking — page faults cause latency spikes

The sunray process did not call `mlockall()`. On a system with 3.7 GB RAM this is rarely a problem, but rare page faults during mowing could cause 1-10ms latency spikes — enough to trigger PID cycle time warnings.

### 3. UART `/dev/ttyS0` without low-latency flag

The serial port to the STM32 MCU (`/dev/ttyS0` at 19200 baud) did not have the `ASYNC_LOW_LATENCY` flag set. Without it, the kernel buffers serial data for up to one timer tick (4ms at `CONFIG_HZ=250`) before delivering it to userspace, adding unnecessary latency to the motor control loop.

## Decision

### Fix `thread_set_priority()` to use SCHED_FIFO

Changed `wiring_thread.c` to use `SCHED_FIFO` when priority > 0, with graceful fallback to `SCHED_OTHER` if the container lacks `CAP_SYS_NICE`:

```c
if (pri > 0) {
    int max_fifo = sched_get_priority_max(SCHED_FIFO);
    sched.sched_priority = (pri > max_fifo) ? max_fifo : pri;
    int ret = sched_setscheduler(0, SCHED_FIFO, &sched);
    if (ret != 0) {
        // fallback to SCHED_OTHER
    }
}
```

### Enable SCHED_FIFO on the arduino-loop thread

Uncommented and configured the `pthread_attr_setschedpolicy(&attr, SCHED_FIFO)` in `wiring_main.cpp`. The loop thread runs at `SCHED_FIFO` priority 94 (max - 5), the main thread at priority 65.

### Add `mlockall(MCL_CURRENT | MCL_FUTURE)`

Added to `main()` before thread creation. Locks all current and future pages, preventing page faults. Warns on failure but does not abort.

### UART low-latency via container entrypoint

Created a small C helper (`linux/tools/serial_lowlatency.c`) that sets the `ASYNC_LOW_LATENCY` flag on a serial device via `TIOCSSERIAL` ioctl. The Docker entrypoint runs it on `/dev/ttyS0` before starting sunray.

### Resulting thread priorities

| Thread | Policy | Priority | Role |
|---|---|---|---|
| `sunray` (main) | SCHED_FIFO | 65 | Signal handling, process management |
| `arduino-loop` | SCHED_FIFO | 94 | Main firmware loop (setup/loop) |
| BLE server | SCHED_FIFO | 94 | BLE GATT server (inherits from loop thread) |
| BLE accept | SCHED_OTHER | 0 | Blocks on l2cap accept(), no RT needed |

## Consequences

- The firmware loop runs with real-time priority, preempting all normal processes
- Page faults eliminated — process RSS is locked at ~36 MB
- UART response time reduced by up to 4ms per transaction
- Container must run with `--privileged` or `CAP_SYS_NICE` (already the case for device access)
- If RT scheduling fails (e.g., non-privileged container), firmware still works but may see occasional jitter

## Files changed

| File | Change |
|---|---|
| `linux/src/wiring_thread.c` | Fixed `thread_set_priority()` to use SCHED_FIFO with fallback |
| `linux/src/wiring_main.cpp` | Added `mlockall()`, enabled SCHED_FIFO on loop thread, added `<sched.h>` and `<sys/mman.h>` |
| `linux/tools/serial_lowlatency.c` | New: C helper to set ASYNC_LOW_LATENCY on serial ports |
| `Dockerfile` | Builds and copies `serial_lowlatency` helper |
| `docker-entrypoint.sh` | Runs `serial_lowlatency /dev/ttyS0` before starting sunray |
