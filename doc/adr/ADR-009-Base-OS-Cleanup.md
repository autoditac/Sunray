# ADR-009 — Base OS Cleanup (Headless Mower)

| Field | Value |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-04-17 |
| **Author** | Rouven Sacha |

---

## Context

The Alfred mower's Raspberry Pi 4B shipped with a full Raspberry Pi Desktop (Trixie) image. This included ~30 userspace processes and hundreds of packages that are unnecessary for a headless mower controller:

| Package group | Examples | RAM impact |
|---|---|---|
| Display server / compositor | labwc, wayvnc, Wayland, X11, Mesa | ~40 MB |
| Audio stack | pipewire, wireplumber, PulseAudio | ~60 MB |
| Camera / motion | motion (webcam daemon) | ~50 MB |
| Desktop environment | RPD themes, preferences, utilities, wallpapers | ~20 MB |
| Bluetooth stack | bluez, bluetoothd | ~15 MB |
| Cellular modem | ModemManager | ~30 MB |
| Mail transfer agent | exim4 | ~10 MB |
| GPS daemon | gpsd | ~5 MB (and grabbing /dev/ttyACM0) |
| Web browser | Chromium | Disk only |
| Toolkit libraries | GTK3/4, Qt6, CUPS, Xwayland | Disk only |

Total: ~30 running services, ~225 MB RSS, ~1.5 GB disk.

### Specific issues

- **gpsd** was grabbing `/dev/ttyACM0` (the ublox F9P GPS), competing with sunray for the serial device
- **motion** consumed 50+ MB RSS running a camera daemon with no camera attached
- **pipewire + wireplumber** consumed 60+ MB for audio on a headless system
- **exim4** was a mail transfer agent with no email to transfer

## Decision

Remove all GUI, audio, camera, Bluetooth, modem, mail, and desktop packages. Keep only the essential services needed for a headless mower:

### Packages removed (616 total)

Major groups purged via `apt-get autoremove --purge`:

```
rpd-wayland-all rpd-x-all rpd-wayland-core rpd-wayland-extras
rpd-x-core rpd-x-extras labwc wayvnc motion bluez modemmanager
pipewire wireplumber exim4-daemon-light gpsd rtkit polkitd
rpd-theme rpd-preferences rpd-utilities rpd-applications
rpd-developer rpd-graphics rpd-common rpd-wallpaper
rpd-wallpaper-trixie rpd-plym-splash rpi-chromium-mods gui-updater
```

### Services retained

| Service | Purpose |
|---|---|
| `systemd-journald` | System logging |
| `systemd-timesyncd` | NTP time sync (critical for GPS) |
| `systemd-udevd` | Device management |
| `NetworkManager` + `wpa_supplicant` | WiFi connectivity |
| `avahi-daemon` | mDNS (`batman.local` resolution) |
| `cron` | Scheduled tasks |
| `dbus-daemon` | System message bus |
| `agetty` | Console login (emergency access) |
| `podman` (conmon, netavark) | Container runtime for sunray |

### Services explicitly stopped and disabled before removal

```bash
systemctl stop motion wayvnc wayvnc-control bluetooth \
  ModemManager gpsd avahi-daemon exim4
systemctl disable motion wayvnc wayvnc-control bluetooth \
  ModemManager gpsd exim4
loginctl disable-linger pi  # stop user session services
```

Avahi was initially removed but reinstalled per requirement for `batman.local` mDNS resolution.

## Consequences

- Process count: ~30 → ~12
- RAM usage: ~430 MB → ~372 MB (~60 MB freed)
- Disk usage: ~1.5 GB freed
- No more serial port contention from gpsd
- No unnecessary CPU wakes from audio/camera/bluetooth polling
- System boots faster with fewer services
- If GUI access is ever needed, use `ssh -X` or reinstall `rpd-wayland-core`
- `polkitd` remains as a dependency of NetworkManager/systemd-logind (9 MB, harmless)
- Changes are host-side only — the sunray container image is unaffected

## Host changes

| Action | Detail |
|---|---|
| `apt-get autoremove --purge` | 616 packages removed |
| `apt-get install avahi-daemon` | Reinstalled for mDNS |
| `systemctl disable` | motion, wayvnc, bluetooth, ModemManager, gpsd, exim4 |
