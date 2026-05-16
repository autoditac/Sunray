# ADR-011 - GNSS Receiver Configuration Owned by Ansible

| Field | Value |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-05-16 |
| **Author** | Rouven Sacha |

---

## Context

Alfred mowers use a u-blox ZED-F9P receiver for GNSS/RTK positioning. Sunray
talks to the receiver directly through `/dev/ttyACM0` and expects a stable
receiver profile for USB output, UART protocol routing, navigation filters, and
message rates.

Historically, Sunray could configure the receiver at startup when
`GPS_CONFIG=true`. That path is implemented in `UBLOX::begin()` and
`UBLOX::configure()` and sends u-blox VALSET writes to the RAM layer only. This
has three problems for the Alfred deployment:

| Problem | Effect |
|---|---|
| Configuration is runtime-only | A power cycle, firmware update, or receiver reset can bring back stale FLASH/BBR settings |
| Ownership is split | Sunray and deployment automation can drift or overwrite each other |
| Startup touches hardware state | Normal application startup changes receiver configuration instead of only consuming GNSS data |

The receiver also has settings outside Sunray's normal application concern:
UART1/UART2 enablement, RTCM input routing, USB/NMEA/UBX protocol selection, and
message outputs used by diagnostics and operations. These are host deployment
decisions, not mower control-loop behavior.

### Options considered

| Option | Pros | Cons |
|---|---|---|
| **Keep `GPS_CONFIG=true` in Sunray** | Works with upstream runtime behavior; receiver can be corrected on every startup | Writes only RAM, duplicates deployment configuration, hides drift until restart |
| **Persist Sunray's runtime settings from Sunray** | Single binary could own receiver setup | Firmware would need to write BBR/FLASH and manage deployment-specific UART/USB policy |
| **Let Ansible own the persistent receiver profile** | Declarative, version-controlled, writes RAM/BBR/FLASH, keeps Sunray focused on GNSS consumption | Receiver profile must be applied before deploying a Sunray build with `GPS_CONFIG=false` |

## Decision

For Alfred deployments, the u-blox receiver profile is owned by the
[alfred-ansible](https://github.com/autoditac/alfred-ansible) role. Sunray does
not configure the receiver at application startup.

Alfred Sunray configuration therefore sets:

```cpp
#define GPS_REBOOT_RECOVERY  true
#define GPS_CONFIG           false
```

The Ansible `gps` tag persists the Sunray-compatible ZED-F9P profile to
RAM | BBR | FLASH with `ubxtool`. That profile includes:

| Area | Owned settings |
|---|---|
| Receiver firmware prerequisite | ZED-F9P with HPG 1.32 firmware |
| Port enablement | USB and UARTs enabled as required; unused I2C disabled |
| Protocol routing | UBX/RTCM/NMEA input and output policy for USB, UART1, and UART2 |
| Navigation behavior | Dynamic model, fix mode, minimum elevation, C/N0 filtering, DGNSS timeout |
| Message rates | RELPOSNED, HPPOSLLH, VELNED, RXM_RTCM, NAV_SIG, MON_COMMS, and selected NMEA outputs |

`GPS_REBOOT_RECOVERY=true` remains enabled because it performs a GNSS warm
restart for recovery. It must not be used as a general configuration mechanism
and does not replace the persistent Ansible profile.

## Consequences

- Sunray startup no longer rewrites the receiver profile.
- The receiver profile survives application restarts, GNSS warm restarts, power
  cycles, and normal container updates.
- Receiver configuration changes are reviewed and deployed through
  `alfred-ansible --tags gps` instead of firmware code changes.
- Live receiver checks and profile writes should stop `sunray.service` first so
  `ubxtool` has exclusive access to `/dev/ttyACM0`.
- `gpsd.service` and `gpsd.socket` remain masked/inactive on Alfred mowers;
  Sunray and maintenance tools access the receiver directly.
- A new mower or a receiver after firmware flashing must receive the Ansible
  `gps` profile before relying on a Sunray build with `GPS_CONFIG=false`.

## Implementation notes

| File / component | Responsibility |
|---|---|
| `configs/config.h` | Alfred Docker build configuration: `GPS_CONFIG=false` |
| `linux/config_alfred.h` | Alfred Linux reference configuration: `GPS_CONFIG=false` |
| `sunray/src/ublox/ublox.cpp` | Keeps upstream runtime configuration path behind `GPS_CONFIG` |
| `alfred-ansible` role, `gps` tag | Installs tooling, masks gpsd, writes the persistent F9P profile, records the applied profile |

## References

- [ADR-010](ADR-010-WiFi-Management-OS-Level.md) - OS-level ownership for deployment concerns
- [alfred-ansible](https://github.com/autoditac/alfred-ansible) - persistent F9P receiver profile via the `gps` tag
- u-blox ZED-F9P expected firmware: `FWVER=HPG 1.32`, `PROTVER=27.31`
