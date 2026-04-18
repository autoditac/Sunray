# Alfred System Setup

System configuration for Alfred mowers running Sunray on Raspberry Pi 4B.

## Target Platform

| Component | Value |
|---|---|
| Board | Raspberry Pi 4 Model B |
| OS | Debian Trixie 13 (aarch64) |
| Container runtime | Podman 5.x with Quadlet |
| MCU | STM32F103VET (512K flash, 64K SRAM) |
| GPS | u-blox F9P (USB) |
| Serial | `/dev/ttyS0` @ 19200 baud (RPi ↔ MCU) |

## Services

| Service | Purpose | Port |
|---|---|---|
| `sunray.service` | Mower firmware (Podman Quadlet) | 80 |
| `cassandra.service` | CaSSAndRA mower management | 8050 |
| `alfred-dashboard.service` | Status & control UI (Podman Quadlet) | 3000 |

## MCU Toolchain (Cross-Compile on x86_64)

The STM32 Arduino core does not ship arm64 toolchain binaries, so MCU firmware
must be compiled on an x86_64 workstation and the `.bin` copied to the mower.

### Install arduino-cli

```bash
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
# Installs to ~/bin or ~/.local/bin
```

### Install STM32 core

```bash
arduino-cli config add board_manager.additional_urls \
  https://github.com/stm32duino/BoardManagerFiles/raw/main/package_stmicroelectronics_index.json

arduino-cli core update-index
arduino-cli core install STMicroelectronics:stm32
```

### Compile MCU firmware

The sketch file must be in a directory matching its name (Arduino convention):

```bash
mkdir -p /tmp/rm18
cp linux/firmware/rm18.ino /tmp/rm18/rm18.ino

arduino-cli compile \
  --fqbn "STMicroelectronics:stm32:GenF1:pnum=GENERIC_F103VETX" \
  --output-dir /tmp/rm18-build \
  /tmp/rm18/rm18.ino
```

Output: `/tmp/rm18-build/rm18.ino.bin` (~41 KB)

### Copy binary to mower

```bash
scp /tmp/rm18-build/rm18.ino.bin mower:/tmp/
ssh mower "sudo mkdir -p /home/pi/sunray_install/firmware/ && \
            sudo cp /tmp/rm18.ino.bin /home/pi/sunray_install/firmware/"
```

## SWD Flashing (on Raspberry Pi)

### Install OpenOCD

```bash
sudo apt-get install -y openocd
```

### SWD Wiring (RPi 4B header → Alfred J1 connector)

```
Physical Pin  BCM GPIO  Function       J1 Pin
-----------  --------  ----------     ------
P18          GPIO 24   SWDIO          2 (sda)
P22          GPIO 25   SWCLK          3 (clk)
P16          GPIO 23   SRST (main)    1 (rst)
GND          GND       GND            4 (GND)
```

J1 is the **bottom** SWD connector on the RM18/RM24 board (main MCU).
The top connector is for the perimeter MCU (SRST on GPIO 20/physical P24).

### OpenOCD config: `swd-pi.ocd`

Uses the `linuxgpiod` driver (required on modern kernels where sysfs GPIO is
deprecated):

```
adapter driver linuxgpiod
adapter gpio swdio 24 -chip 0
adapter gpio swclk 25 -chip 0
adapter gpio srst 23 -chip 0
transport select swd
reset_config srst_only
reset_config srst_nogate
reset_config connect_assert_srst
source [find target/stm32f1x.cfg]
adapter srst delay 100
adapter srst pulse_width 100
```

> **Note**: The upstream repo uses `sysfsgpio` with BananaPi GPIO numbers
> (10, 47, 11). On RPi 4B with kernel 6.x+, use `linuxgpiod` with BCM
> GPIO numbers (24, 25, 23) instead.

### Backup current firmware

```bash
sudo openocd \
  -f /home/pi/Sunray/alfred/config_files/openocd/swd-pi.ocd \
  -f /home/pi/Sunray/alfred/config_files/openocd/save-firmware.ocd
```

Dumps 512 KB to `/home/pi/sunray_install/firmware/dump.bin`.

### Flash new firmware

```bash
# Stop sunray first
sudo systemctl stop sunray

# Flash
sudo openocd \
  -f /home/pi/Sunray/alfred/config_files/openocd/swd-pi.ocd \
  -f /home/pi/Sunray/alfred/config_files/openocd/flash-sunray-compat.ocd

# Restart
sudo systemctl start sunray
```

The flash script programs `rm18.ino.bin` to `0x08000000`, verifies, and resets.

### Verify

Check MCU version in sunray logs:

```bash
sudo journalctl -u sunray --no-pager -n 100 | grep "MCU FIRMWARE"
# MCU FIRMWARE: RM18,1.1.16
```

## Container Updates

All three services (sunray, cassandra, alfred-dashboard) are configured with
`AutoUpdate=registry` in their Quadlet unit files.  Podman's built-in
auto-update mechanism checks the container registry for newer images and
restarts services that have updates.

### How it works

1. Each `.container` Quadlet file declares `AutoUpdate=registry`.
2. `podman auto-update` compares the local image digest against the registry
   (`ghcr.io/autoditac/<image>:latest`).
3. If a newer digest is found, Podman pulls the image and restarts the
   systemd service.

### CI tagging strategy

- **Push to `main`** — CI builds an image tagged with `:sha` only.
- **Release tag** (e.g. `1.0.331-autoditac.1`) — CI tags `:sha`, `:version`,
  and `:latest`.

This means `:latest` on the registry only advances when a release is created.
Regular development pushes build images but do not trigger auto-updates on
the mower.

### Safe auto-update (`alfred-safe-update`)

The raw `podman-auto-update.timer` is disabled.  Instead,
`alfred-safe-update.timer` runs daily at 03:00 (+30 min jitter) and performs
two safety checks before calling `podman auto-update`:

1. **Mower state** — queries the Alfred Dashboard API (`/api/status`).
   Only proceeds when `operation` is IDLE (0) or CHARGE (2).  Skips when
   mowing (1) or docking (4).
2. **CaSSAndRA schedule** — reads `schedulecfg.json`.  Skips if a mowing
   schedule starts within 30 minutes.

If either check fails, the update is skipped until the next timer run.

Enable the timer (managed by the Ansible role):

```bash
sudo systemctl enable --now alfred-safe-update.timer
```

Verify the timer is active:

```bash
sudo systemctl list-timers alfred-safe-update.timer
```

### Manual update check

Dry-run (shows which containers have pending updates without applying):

```bash
sudo podman auto-update --dry-run
```

Apply updates immediately (bypasses safety checks):

```bash
sudo podman auto-update
```

### Manual deployment (without registry)

When building images locally (not pushed to a registry), transfer them
directly to the mower:

```bash
# From workstation:
docker save ghcr.io/autoditac/sunray:latest | ssh mower sudo podman load
ssh mower "sudo systemctl restart sunray"
```

## Package Dependencies (on mower)

```bash
# Container runtime
sudo apt-get install -y podman

# SWD flashing
sudo apt-get install -y openocd

# GPIO access for OpenOCD (linuxgpiod)
sudo apt-get install -y libgpiod2

# Useful diagnostics
sudo apt-get install -y gpiodetect gpiomon
```
