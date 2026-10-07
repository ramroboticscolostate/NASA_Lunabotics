# Testing CMDS and notes

## USE

## Working Directory

Run the logger from the `testing/` directory so the relative `results/` path is created in the correct location.

```bash
cd pi_main_brain/testing
```
**USE PYTHON VIRTUAL ENV when running tests be sure in C:**

create virtual env:

```bash
python3 -m venv .venv
source .venv/bin/activate
```

VENV should be active now

INSTALL (i will add requirements.txt if dependency list grows)

```bash
pip install psutil
```
BASELINE SCRIPT:
```bash
./scripts/network_baseline.sh
```

LOGGER SCRIPT:

```bash
python scripts/rover_logger.py --config config.json
```

## Files
- 'config.json'
  Used for quick changes and easy additions for code to reference like testName, interface, and result path

- 'scripts/network_baseline.sh'
  Collects a quick snapshot of network interface, route, wifi state, and bas9c system data

- 'scripts/rover_logger.py'
  Collects time-series data:
    - Ping RTT
    - Wifi Ssignal strength
    - Wifi Transmit bitrate
    - cpu usage/temp
    - RX/TX byte counters

- 'results/'
Default location for generated test results

