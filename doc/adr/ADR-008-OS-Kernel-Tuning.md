# ADR-008 — Host OS Kernel Tuning (CPU Governor, Swappiness)

| Field | Value |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-04-17 |
| **Author** | Rouven Sacha |

---

## Context

The Alfred mower runs on a Raspberry Pi 4B (4 GB) with Debian Trixie 13.4 and kernel `6.12.75+rpt-rpi-v8 #1 SMP PREEMPT`. The OS defaults are tuned for interactive desktop use, not a dedicated real-time mower controller.

Two specific defaults hurt mowing performance:

### CPU governor: `ondemand`

The default `ondemand` governor scales CPU frequency dynamically (600 MHz – 1.5 GHz). When sunray's main loop completes in <1ms, the governor sees low utilization and drops to 600 MHz. When a burst of GPS data arrives or a PID cycle needs computation, the governor takes 10-50ms to ramp back up. This latency is visible as occasional loop time spikes.

### Swappiness: `60`

The default `vm.swappiness=60` means the kernel actively swaps out pages even when there's plenty of free RAM. Combined with the mower's SD card storage (slow I/O), swap-in during a PID cycle could cause 10-100ms latency spikes.

## Decision

### CPU governor → `performance`

Set all 4 cores to the `performance` governor (constant 1.5 GHz). Made persistent via a systemd oneshot service:

```ini
# /etc/systemd/system/cpu-performance.service
[Unit]
Description=Set CPU governor to performance
After=sysinit.target

[Service]
Type=oneshot
ExecStart=/bin/sh -c 'for f in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do echo performance > $f; done'
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
```

Power impact: The RPi 4B draws ~1W more at constant 1.5 GHz vs idle-scaled. Acceptable for a battery-powered mower that's only powered during mowing sessions.

### Swappiness → `10`

Reduced via sysctl:

```ini
# /etc/sysctl.d/99-sunray.conf
vm.swappiness=10
```

This tells the kernel to strongly prefer evicting file cache over swapping out anonymous pages. The sunray container uses ~36 MB RSS out of 3.7 GB total — swapping should never occur.

## Consequences

- CPU always runs at maximum frequency — no ramp-up latency
- Slight increase in idle power (~1W), negligible for mower use case
- Near-zero chance of swap-related latency spikes
- Settings survive reboots via systemd service and sysctl.d
- If the Pi is repurposed for desktop use, these settings should be reverted

## Host files changed

| File | Change |
|---|---|
| `/etc/systemd/system/cpu-performance.service` | New: oneshot service to set governor=performance |
| `/etc/sysctl.d/99-sunray.conf` | New: `vm.swappiness=10` |
