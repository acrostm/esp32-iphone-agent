#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
python3 hardware/build.py --board m5stack-stopwatch --build
