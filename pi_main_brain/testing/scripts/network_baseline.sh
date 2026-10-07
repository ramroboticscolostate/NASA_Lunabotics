#!/usr/bin/env bash
set -u

echo "========================================"
echo " Pi Network Baseline"
echo "========================================"
echo "Timestamp: $(date --iso-8601=seconds)"
echo "Hostname:  $(hostname)"

echo
echo "--- Interfaces ---"
ip -br addr

echo
echo "--- Routes ---"
ip route

echo
echo "--- Wi-Fi ---"
iw dev wlan0 link 2>/dev/null || echo "No wlan0 Wi-Fi link detected"

echo
echo "--- CPU / Memory ---"
uptime
free -h

echo
echo "--- CPU Temperature ---"
if [ -r /sys/class/thermal/thermal_zone0/temp ]; then
    awk '{printf "%.1f C\n", $1/1000}' /sys/class/thermal/thermal_zone0/temp
fi
