#!/usr/bin/env python3

import argparse
import csv
import json
import re
import subprocess
import time
from datetime import datetime
from pathlib import Path

try:
    import psutil
except ImportError:
    raise SystemExit(
        "Missing dependency: psutil\n"
        "Activate the project venv, then run: pip install -r requirements.txt"
    )


def run_command(command):
    try:
        result = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=3,
            check=False,
        )
        return result.stdout.strip()
    except (subprocess.SubprocessError, FileNotFoundError):
        return ""


def get_default_gateway():
    output = run_command(["ip", "route"])
    for line in output.splitlines():
        if line.startswith("default "):
            parts = line.split()
            if "via" in parts:
                return parts[parts.index("via") + 1]
    return ""


def get_ping_rtt_ms(target):
    if not target:
        return None

    output = run_command(["ping", "-c", "1", "-W", "1", target])
    match = re.search(r"time[=<]([\d.]+)\s*ms", output)

    return float(match.group(1)) if match else None


def get_wifi_info(interface):
    output = run_command(["iw", "dev", interface, "link"])

    signal_dbm = None
    tx_bitrate_mbps = None

    signal_match = re.search(r"signal:\s*(-?[\d.]+)\s*dBm", output)
    if signal_match:
        signal_dbm = float(signal_match.group(1))

    bitrate_match = re.search(r"tx bitrate:\s*([\d.]+)\s*MBit/s", output)
    if bitrate_match:
        tx_bitrate_mbps = float(bitrate_match.group(1))

    return signal_dbm, tx_bitrate_mbps


def get_cpu_temp_c():
    path = Path("/sys/class/thermal/thermal_zone0/temp")
    try:
        return round(int(path.read_text().strip()) / 1000.0, 2)
    except (OSError, ValueError):
        return None


def get_interface_bytes(interface):
    counters = psutil.net_io_counters(pernic=True).get(interface)
    if counters is None:
        return None, None
    return counters.bytes_recv, counters.bytes_sent


def load_config(path):
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default="config.json")
    args = parser.parse_args()

    config = load_config(args.config)

    test_name = config.get("test_name", "unnamed_test")
    interface = config.get("interface", "wlan0")
    ping_target = config.get("ping_target", "AUTO_GATEWAY")
    interval = float(config.get("sample_interval_seconds", 1))
    duration = float(config.get("duration_seconds", 60))
    results_directory = Path(config.get("results_directory", "results"))

    gateway = get_default_gateway()

    if ping_target == "AUTO_GATEWAY":
        ping_target = gateway

    results_directory.mkdir(parents=True, exist_ok=True)
    run_timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    output_path = results_directory / f"rover_metrics_{run_timestamp}.csv"

    fields = [
        "timestamp",
        "elapsed_seconds",
        "test_name",
        "interface",
        "gateway",
        "ping_target",
        "ping_rtt_ms",
        "wifi_signal_dbm",
        "wifi_tx_bitrate_mbps",
        "cpu_percent",
        "memory_percent",
        "cpu_temp_c",
        "rx_bytes",
        "tx_bytes",
    ]

    psutil.cpu_percent(interval=None)
    start = time.monotonic()

    with output_path.open("w", newline="", encoding="utf-8") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=fields)
        writer.writeheader()

        try:
            while True:
                elapsed = time.monotonic() - start

                if elapsed > duration:
                    break

                signal_dbm, bitrate_mbps = get_wifi_info(interface)
                rx_bytes, tx_bytes = get_interface_bytes(interface)

                row = {
                    "timestamp": datetime.now().isoformat(timespec="seconds"),
                    "elapsed_seconds": round(elapsed, 2),
                    "test_name": test_name,
                    "interface": interface,
                    "gateway": gateway,
                    "ping_target": ping_target,
                    "ping_rtt_ms": get_ping_rtt_ms(ping_target),
                    "wifi_signal_dbm": signal_dbm,
                    "wifi_tx_bitrate_mbps": bitrate_mbps,
                    "cpu_percent": psutil.cpu_percent(interval=None),
                    "memory_percent": psutil.virtual_memory().percent,
                    "cpu_temp_c": get_cpu_temp_c(),
                    "rx_bytes": rx_bytes,
                    "tx_bytes": tx_bytes,
                }

                writer.writerow(row)
                csv_file.flush()

                print(
                    f"{row['timestamp']} | "
                    f"RTT={row['ping_rtt_ms']} ms | "
                    f"RSSI={row['wifi_signal_dbm']} dBm | "
                    f"CPU={row['cpu_percent']}% | "
                    f"Temp={row['cpu_temp_c']} C"
                )

                remaining = interval - ((time.monotonic() - start) % interval)
                time.sleep(max(0.05, remaining))

        except KeyboardInterrupt:
            print("\nStopped by user.")

    print(f"\nSaved results to {output_path}")


if __name__ == "__main__":
    main()
