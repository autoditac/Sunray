#!/bin/bash
# Docker entrypoint for Sunray Alfred firmware
# Simplified from linux/start_sunray.sh — no BLE, CAN, audio setup needed

set -e

echo "=== Sunray Docker Entrypoint ==="
echo "Date: $(date)"
echo "Hostname: $(hostname)"

# Forward SIGTERM to the sunray process for graceful shutdown
trap 'kill -TERM "$SUNRAY_PID" 2>/dev/null; wait "$SUNRAY_PID"' SIGTERM SIGINT

echo "Starting sunray..."
exec /usr/bin/stdbuf -oL -eL /opt/sunray/sunray "$@"
