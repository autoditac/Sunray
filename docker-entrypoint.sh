#!/bin/bash
# Docker entrypoint for Sunray Alfred firmware
# Simplified from linux/start_sunray.sh — no BLE, CAN, audio setup needed

set -e

echo "=== Sunray Docker Entrypoint ==="
echo "Date: $(date)"
echo "Hostname: $(hostname)"

# Set UART low_latency flag to reduce serial response time (1 tick = 4ms at HZ=250)
if [ -e /dev/ttyS0 ]; then
  /opt/sunray/serial_lowlatency /dev/ttyS0 || echo "WARN: could not set ttyS0 low_latency"
fi

# Forward SIGTERM to the sunray process for graceful shutdown
trap 'kill -TERM "$SUNRAY_PID" 2>/dev/null; wait "$SUNRAY_PID"' SIGTERM SIGINT

# Persist map.bin / state.bin across container updates.
# Sunray's Storage.cpp and map.cpp open these files with a *relative*
# path, so they land in the CWD.  The named volume is mounted at
# /opt/sunray/data — run the binary from that directory so state
# survives `podman auto-update`.
mkdir -p /opt/sunray/data
cd /opt/sunray/data

echo "Starting sunray (cwd=$(pwd))..."
exec /usr/bin/stdbuf -oL -eL /opt/sunray/sunray "$@"
