# ADR-010 — WiFi Management at OS Level, Not Firmware

| Field | Value |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-04-18 |
| **Author** | Rouven Sacha |

---

## Context

Upstream Sunray manages WiFi from inside the firmware:

| Firmware responsibility | Implementation | Problem |
|---|---|---|
| WiFi signal monitoring | `wpa_cli -i wlan0 status` via `Process::runShellCommand` every 7 s | Forks a shell in the control loop (see [ADR-006](ADR-006-Process-Fork-Removal.md)) |
| WiFi LED status | Sets an LED state based on `wpa_cli` output | LED state is stale, updated only every 7 s |
| WiFi reconnection | None — assumed to be handled by the OS | Correct assumption, but the monitoring adds overhead for no action |

This made sense on the original BananaPi where WiFi was unstable and operators used the Sunray Android app directly. On our RPi 4B setup, WiFi is managed by NetworkManager, the control interface is CaSSAndRA (HTTP), and the firmware runs inside a Podman container where `wpa_cli` is not available.

### Options considered

| Option | Pros | Cons |
|---|---|---|
| **Install `wpa_cli` in the container** | Upstream compatibility | Bloats image, still forks shells in control loop |
| **Move WiFi monitoring to a background thread** | No main loop impact | Still firmware managing what the OS should own |
| **Remove WiFi monitoring from firmware, manage at OS level** | Clean separation of concerns, zero firmware overhead | WiFi LED updates require OS-level setup |

## Decision

Remove all WiFi management from the Sunray firmware. The OS owns WiFi entirely:

| Concern | OS-level solution |
|---|---|
| Connection management | NetworkManager (`preconfigured` connection profile) |
| Power save | `/etc/NetworkManager/conf.d/wifi-powersave.conf` — disabled |
| Band selection | `nmcli con modify ... 802-11-wireless.band bg` — forced 2.4 GHz |
| Regulatory domain | `cfg80211.ieee80211_regdom=DK` in kernel cmdline |
| WiFi status LED | Kernel `ledtrig-netdev` module on ACT LED — solid when linked, blinks on rx/tx |

All of these are deployed and persisted via the [alfred-ansible](https://github.com/autoditac/alfred-ansible) role (`tuning` tag).

## Consequences

- Firmware has zero WiFi-related code paths in the control loop
- WiFi LED reacts instantly to link state changes (kernel trigger vs 7 s polling)
- WiFi configuration is declarative and version-controlled in the Ansible role
- `wpa_cli` is not needed in the container image (smaller image, fewer dependencies)
- If WiFi drops, NetworkManager reconnects automatically — the firmware does not need to know

## References

- [`21de4d6`](https://github.com/autoditac/Sunray/commit/21de4d6) — Disable `updateWifiConnectionState()`, replace Process forks with direct sysfs
- [`a50ca74`](https://github.com/autoditac/alfred-ansible/commit/a50ca74) — WiFi LED indicator via kernel `ledtrig-netdev`
- [ADR-006](ADR-006-Process-Fork-Removal.md) — Process fork removal (parent decision)
- [ADR-008](ADR-008-OS-Kernel-Tuning.md) — OS-level tuning (WiFi power save, band, regdomain)
