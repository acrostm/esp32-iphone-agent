#!/usr/bin/env python3
"""Host tests for native firmware; requires a host C/C++ compiler, not ESP-IDF."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[3]
NATIVE = ROOT / 'hardware/esp-idf/main'
HERE = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='stopwatch-tests-') as tmp:
    for name in ('protocol', 'model', 'task', 'stream'):
        target = str(Path(tmp) / name)
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-I' + str(NATIVE), str(HERE / f'test_{name}.cpp'),
                        '-o', target], check=True)
        subprocess.run([target], check=True)
    target = str(Path(tmp) / 'shared-core')
    shared = ROOT / 'hardware/hid_bridge/src'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(ROOT / 'hardware/tests/test_main.c'),
                    str(shared / 'bplist/bplist.c'), str(shared / 'video/gop.c'), '-o', target], check=True)
    subprocess.run([target], check=True)
subprocess.run([sys.executable, str(HERE / 'test_tools.py')], check=True)
print('Native host tests: PASS')
