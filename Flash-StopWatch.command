#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
python3 hardware/build.py --board m5stack-stopwatch --build
printf '%s\n' 'Flashing replaces the application and partition table; factory data may be incompatible.'
printf '%s\n' 'No whole-chip erase. Enter the explicitly confirmed StopWatch serial port; empty cancels.'
read -r -p 'StopWatch port: ' target_port
[[ -n "$target_port" ]] || exit 0
python3 hardware/build.py --board m5stack-stopwatch --flash --port "$target_port"
