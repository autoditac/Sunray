# Update & Docker Deployment Plan for Mower-B & Mower-A

## Goals
1. Update both mowers to latest Sunray firmware (1.0.331+)
2. Retain custom config per mower
3. Fix and carry the two-wheel-turn feature (critical for Alfred's heavy front chassis)
4. Move from bare-metal systemd service to Docker-based deployment
5. Set up a personal GitHub fork with CI/CD for building Docker images

## User Preferences (confirmed)
- **Two-wheel-turn**: YES — must work. Alfred's heavy front causes inner wheel stall on uneven terrain
- **BLE/Bluetooth**: Disabled — only WiFi/CaSSAndRA used
- **Audio/TTS/Buzzer**: Disabled — beeps not wanted
- **NTRIP**: Not used — RTK corrections come via CaSSAndRA
- **Lift sensor**: Disabled — causes constant false triggers, stopping the mower

---

## Phase 1: Fork & Repository Setup

### 1.1 Fork on GitHub
- Fork `https://github.com/Ardumower/Sunray` to personal GitHub account
- Clone the fork locally

### 1.2 Branch Strategy
```
upstream/master  ← track official releases
       │
       ▼
  main (your fork)  ← rebased on upstream, carries your patches
       │
       ├── config/mower-b    ← mower-b-specific config.h
       ├── config/mower-a   ← mower-a-specific config.h
       │
       └── feature/two-wheel-turn  ← optional motor enhancement
```

### 1.3 Custom Patches to Carry Forward
Since most of mower-a's map/dock fixes are already upstream, the remaining custom changes are minimal:

**Shared code patch (both mowers):**
- **Two-wheel-turn fix** in `sunray/motor.cpp` — NEW improved implementation (see below)

**Shared config changes (both mowers):**
- BLE disabled (comment out `BLE_NAME`)
- Lift sensor disabled (comment out `ENABLE_LIFT_DETECTION` + `LIFT_OBSTACLE_AVOIDANCE`)
- Buzzer disabled (comment out `BUZZER_ENABLE`)
- Two-wheel-turn config defines added (`TWO_WHEEL_TURN_SPEED_THRESHOLD`, `TWO_WHEEL_TURN_INNER_FACTOR`)

**Per-mower config differences:**
- Mower-B: WiFi credentials, hostname
- Mower-A: WiFi credentials, hostname, rain detection disabled, `MOW_TOGGLE_DIR false`

### 1.4 Two-Wheel-Turn Fix (Analysis & New Implementation)
Mower-A's original implementation had 3 bugs:
1. **Threshold too low** (0.02 m/s) — never triggered during gentle mowing curves where stalling actually occurs
2. **Triggered during pure rotation** when inner wheel was already going backward, then REDUCED the backward speed via the -0.05 cap
3. **Backward speed too conservative** — -0.05 m/s is barely driveable for the PID controller

New implementation (already applied to local repo):
- Only triggers when inner wheel is slow but **still positive** (`lspeed > 0 && lspeed < threshold`)
- Higher threshold (0.12 m/s) catches gentle curves where stalling happens
- Proportional backward speed (30% of outer wheel) — no hard cap
- Guarded by `#ifdef`, excluded during docking/undocking

**Optional feature:**
- Two-wheel-turn motor modification (needs testing on latest codebase)

---

## Phase 2: Docker Image Build

### 2.1 Dockerfile

```dockerfile
# Multi-stage build for Sunray Alfred on RPi 4B (aarch64)
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y \
    cmake g++ git \
    libbluetooth-dev libssl-dev libjpeg-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . /build/Sunray

ARG CONFIG_FILE=linux/config_alfred.h
RUN cd /build/Sunray/linux && \
    mkdir -p build && cd build && \
    cmake -D CONFIG_FILE=/build/Sunray/${CONFIG_FILE} .. && \
    make -j$(nproc)

FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y \
    libbluetooth3 libssl3 libjpeg62-turbo \
    i2c-tools pulseaudio-utils \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /build/Sunray/linux/build/sunray /opt/sunray/sunray
COPY --from=builder /build/Sunray/linux/start_sunray.sh /opt/sunray/
COPY --from=builder /build/Sunray/linux/config_files/ /opt/sunray/config_files/
COPY --from=builder /build/Sunray/ros/scripts/ /opt/sunray/ros/scripts/

WORKDIR /opt/sunray
# State/map persistence
VOLUME ["/opt/sunray/data"]

# HTTP server port
EXPOSE 80

ENTRYPOINT ["/opt/sunray/start_sunray.sh"]
```

### 2.2 docker-compose.yml (per mower)

```yaml
version: '3.8'
services:
  sunray:
    image: ghcr.io/<your-user>/sunray:latest
    container_name: sunray
    restart: unless-stopped
    privileged: true  # needed for I2C, serial, GPIO, CAN
    network_mode: host  # needed for BLE, mDNS, port 80
    volumes:
      - /dev:/dev  # serial ports, I2C
      - sunray-data:/opt/sunray/data  # persistent state/maps
      - /sys:/sys:ro  # CPU temp, GPIO
    devices:
      - /dev/ttyS0:/dev/ttyS0  # STM32 MCU serial
      - /dev/i2c-1:/dev/i2c-1  # IO board I2C
    environment:
      - TZ=Europe/Copenhagen

volumes:
  sunray-data:
```

### 2.3 Hardware Access Requirements
The container needs **privileged** mode because:
- `/dev/ttyS0` - UART to STM32 MCU (motor control)
- `/dev/i2c-1` - I2C bus (IMU, IO board, EEPROM, ADC)
- `/dev/serial/by-id/usb-u-blox*` - USB GPS receiver
- Bluetooth (BLE advertising)
- Network host mode for mDNS and port 80

### 2.4 start_sunray.sh Adaptation
The existing `start_sunray.sh` checks for Docker (`/.dockerenv`) and refuses to run inside. This check needs to be **removed** or adapted. The script also:
- Sets up CAN interface (not needed for Alfred)
- Starts dbus-monitor
- Starts pulseaudio (for TTS)
- Launches the sunray binary

For Docker, create a simpler entrypoint that:
1. Starts dbus-monitor in background
2. Optionally starts pulseaudio
3. Exec's the sunray binary

---

## Phase 3: GitHub Actions CI/CD

### 3.1 Workflow: `.github/workflows/build.yml`

```yaml
name: Build Sunray Docker Image
on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

jobs:
  build:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        config:
          - name: mower-b
            file: configs/mower-b/config.h
          - name: mower-a
            file: configs/mower-a/config.h
    steps:
      - uses: actions/checkout@v4
      
      - uses: docker/setup-qemu-action@v3
        with:
          platforms: linux/arm64
      
      - uses: docker/setup-buildx-action@v3
      
      - uses: docker/login-action@v3
        with:
          registry: ghcr.io
          username: ${{ github.actor }}
          password: ${{ secrets.GITHUB_TOKEN }}
      
      - uses: docker/build-push-action@v5
        with:
          context: .
          platforms: linux/arm64
          push: ${{ github.event_name != 'pull_request' }}
          build-args: CONFIG_FILE=${{ matrix.config.file }}
          tags: |
            ghcr.io/${{ github.repository_owner }}/sunray-${{ matrix.config.name }}:latest
            ghcr.io/${{ github.repository_owner }}/sunray-${{ matrix.config.name }}:${{ github.sha }}
```

### 3.2 Config Management
Store per-mower configs in the fork:
```
configs/
  mower-b/
    config.h      ← mower-b-specific config
  mower-a/
    config.h      ← mower-a-specific config
```

---

## Phase 4: Deployment to Mowers

### 4.1 Prerequisites on Each Pi
```bash
# Install Docker
curl -fsSL https://get.docker.com | sh
sudo usermod -aG docker pi

# Install docker-compose (or use docker compose v2)
sudo apt install docker-compose-plugin
```

### 4.2 Migration Steps (per mower)

1. **Backup current state**:
   ```bash
   # Save map and state
   cp ~/Sunray/alfred/map.bin ~/backup/
   cp ~/Sunray/alfred/state.bin ~/backup/
   # Save current config
   cp ~/Sunray/sunray/config.h ~/backup/config.h.bak  # mower-b
   cp ~/Sunray/alfred/config.h ~/backup/config.h.bak   # mower-a
   ```

2. **Stop old service**:
   ```bash
   sudo systemctl stop sunray
   sudo systemctl disable sunray
   ```

3. **Deploy Docker**:
   ```bash
   mkdir -p ~/sunray-docker
   cd ~/sunray-docker
   # Create docker-compose.yml (see above)
   # Copy map.bin and state.bin into volume
   docker compose pull
   docker compose up -d
   ```

4. **Verify**:
   - Check logs: `docker compose logs -f`
   - Test CaSSAndRA connection
   - Verify GPS fix
   - Test motor control via CaSSAndRA joystick

5. **Rollback if needed**:
   ```bash
   docker compose down
   sudo systemctl enable sunray
   sudo systemctl start sunray
   ```

---

## Phase 5: Keeping Updated

### Sync with Upstream
```bash
git remote add upstream https://github.com/Ardumower/Sunray.git
git fetch upstream
git rebase upstream/master
# Resolve conflicts if any
git push --force-with-lease
```

GitHub Actions will automatically build new images on push.

### Updating Mowers
```bash
ssh mower-b 'cd ~/sunray-docker && docker compose pull && docker compose up -d'
ssh mower-a 'cd ~/sunray-docker && docker compose pull && docker compose up -d'
```

---

## Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|------------|
| Docker `privileged` mode security | Low (isolated network) | Only needed for hardware access |
| Serial port timing in container | Medium | Docker adds negligible latency for 19200 baud |
| I2C timing sensitivity | Low | Kernel driver handles timing, not userspace |
| Map/state data loss | Medium | Volume mount + backup before migration |
| Upstream breaking changes | Low | Pin to tested commits, test before deploying |
| BLE not working in container | Medium | Host network mode should work; test early |
| Container startup order | Low | systemd + docker restart policy |
| Rollback complexity | Low | Old systemd service remains, just re-enable |

---

## Estimated Effort

| Task | Effort |
|------|--------|
| Fork repo, set up branch structure | Small |
| Create per-mower config.h files | Small |
| Write Dockerfile + docker-compose | Medium |
| Adapt start_sunray.sh for Docker | Small |
| Set up GitHub Actions | Medium |
| First cross-compile build (QEMU arm64) | Medium (build time) |
| Deploy to mower-b (easier, less changes) | Small |
| Deploy to mower-a | Small |
| Test & validate both mowers | Medium |
| Port two-wheel-turn to latest (optional) | Small-Medium |
