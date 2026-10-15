#!/usr/bin/env python3
"""Build/package/flash supported ESP-IDF boards without Arduino tools."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / 'hardware/esp-idf'
BOARD = 'm5stack-stopwatch'
BUILD = ROOT / 'hardware/build' / BOARD
EXPECTED_PARTITIONS = [(1, 2, 0x9000, 0x5000, 'nvs'),
                       (1, 1, 0xe000, 0x1000, 'phy_init'),
                       (0, 0, 0x10000, 0xff0000, 'factory')]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def environment():
    # Optional machine-local configuration; never store personal paths in shared source.
    local = ROOT / '.idf-local.json'
    config = json.loads(local.read_text()) if local.exists() else {}
    env = os.environ.copy()
    for key in ('IDF_PATH', 'IDF_TOOLS_PATH', 'IDF_PYTHON_ENV_PATH'):
        if key not in env and key in config:
            env[key] = config[key]
    if not env.get('IDF_PATH'):
        raise ValueError('Activate ESP-IDF 5.5.3 with export.sh first.')
    idf = Path(env['IDF_PATH'])
    version = (idf / 'tools/cmake/version.cmake').read_text()
    for key, value in [('MAJOR', 5), ('MINOR', 5), ('PATCH', 3)]:
        if f'set(IDF_VERSION_{key} {value})' not in version:
            raise ValueError('This board profile requires ESP-IDF 5.5.3.')
    py = Path(env.get('IDF_PYTHON_ENV_PATH', sys.prefix)) / 'bin/python'
    paths = [str(py.parent)] + config.get('extra_bin', [])
    if env.get('IDF_TOOLS_PATH'):
        paths += [str(p) for p in (Path(env['IDF_TOOLS_PATH']) / 'tools').glob('*/**/bin')]
    env['PATH'] = os.pathsep.join(paths + [env['PATH']])
    # Honor normal shell proxy configuration; do not change global networking.
    return env, py, idf


def confined(root, name):
    path = (root / name).resolve()
    if not path.is_relative_to(root.resolve()) or not path.is_file():
        raise ValueError(f'Missing file or path outside artifact directory: {name}')
    return path


def check_partitions(path):
    data = path.read_bytes()
    result = []
    checksum = False
    for offset in range(0, len(data), 32):
        entry = data[offset:offset + 32]
        if len(entry) != 32:
            raise ValueError('Truncated partition entry')
        magic = struct.unpack_from('<H', entry)[0]
        if magic == 0xffff:
            break
        if magic == 0xebeb:
            if hashlib.md5(data[:offset]).digest() != entry[16:32]:
                raise ValueError('Partition checksum mismatch')
            checksum = True
            break
        if magic != 0x50aa:
            raise ValueError('Invalid partition table')
        _, kind, subtype, address, size, label, flags = struct.unpack('<HBBII16sI', entry)
        if flags:
            raise ValueError('Unexpected partition flags')
        result.append((kind, subtype, address, size, label.split(b'\0')[0].decode('ascii')))
    if result != EXPECTED_PARTITIONS or not checksum:
        raise ValueError('Unexpected board partition layout')


def check_layout(args, root=BUILD):
    if args.get('extra_esptool_args', {}).get('chip') != 'esp32s3':
        raise ValueError('Unexpected target chip')
    settings = args.get('flash_settings', {})
    if settings.get('flash_size') != '16MB' or settings.get('flash_mode') not in ('dio', 'qio') or settings.get('flash_freq') != '80m':
        raise ValueError('Unexpected flash configuration')
    files = args['flash_files']
    if len(files) != 3 or {int(k, 0) for k in files} != {0, 0x8000, 0x10000}:
        raise ValueError('Unexpected flash offsets; refusing to overwrite stored data')
    resolved = []
    for offset, name in files.items():
        path = confined(root, name)
        limit = {0: 0x8000, 0x8000: 0x1000, 0x10000: 0xff0000}[int(offset, 0)]
        if not 0 < path.stat().st_size <= limit:
            raise ValueError('Firmware exceeds its flash region or is empty')
        if int(offset, 0) == 0x8000:
            check_partitions(path)
        resolved.append((offset, path))
    return resolved


def audit(root=BUILD):
    description = json.loads((root / 'project_description.json').read_text())
    if description['target'] != 'esp32s3':
        raise ValueError('Unexpected build target')
    if any('arduino' in c.lower() for c in description['build_components']):
        raise ValueError('Arduino component found in native build')
    for command in json.loads((root / 'compile_commands.json').read_text()):
        text = command.get('command', '')
        if '-DARDUINO' in text or '/.arduino/' in text or 'NimBLE-Arduino' in text:
            raise ValueError('Arduino dependency found in compiler command')
    print('Dependency audit: PASS (no Arduino component, macro or cache path)')


def artifact_paths(root=BUILD):
    args = json.loads((root / 'flasher_args.json').read_text())
    paths = [p for _, p in check_layout(args, root)]
    return paths + [root / name for name in ('flasher_args.json', 'stopwatch_agent.elf',
            'stopwatch_agent.map', 'config/sdkconfig.h', 'project_description.json', 'compile_commands.json')]


def package():
    audit()
    paths = artifact_paths()
    files = [{'path': str(p.relative_to(BUILD)), 'size': p.stat().st_size, 'sha256': digest(p)} for p in paths]
    manifest = {'schema': 1, 'board': BOARD, 'target': 'esp32s3',
                'framework': 'ESP-IDF 5.5.3', 'flash_bytes': 16777216, 'files': files}
    (BUILD / 'firmware-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Package verified:', BUILD / 'firmware-manifest.json')


def verify(root=BUILD):
    root = root.resolve()
    manifest = json.loads((root / 'firmware-manifest.json').read_text())
    if (manifest.get('schema'), manifest.get('board'), manifest.get('target'), manifest.get('framework'), manifest.get('flash_bytes')) != (1, BOARD, 'esp32s3', 'ESP-IDF 5.5.3', 16777216):
        raise ValueError('Unexpected firmware manifest')
    expected = {p.relative_to(root).as_posix() for p in artifact_paths(root)}
    actual = [item['path'] for item in manifest['files']]
    if len(actual) != len(set(actual)) or set(actual) != expected:
        raise ValueError('Missing or duplicate artifact in manifest')
    for item in manifest['files']:
        path = confined(root, item['path'])
        if digest(path) != item['sha256'] or path.stat().st_size != item['size']:
            raise ValueError('Firmware changed after verification')
    audit(root)
    return check_layout(json.loads((root / 'flasher_args.json').read_text()), root)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--board', required=True, choices=[BOARD])
    parser.add_argument('--build', action='store_true')
    parser.add_argument('--verify', action='store_true')
    parser.add_argument('--flash', action='store_true')
    parser.add_argument('--port', help='Explicitly confirmed target serial port; never auto-selects')
    args = parser.parse_args()
    if not (args.build or args.verify or args.flash):
        parser.error('Choose --build, --verify and/or --flash')
    if args.verify and not (args.build or args.flash):
        verify()
        return
    env, py, idf = environment()
    if args.build:
        subprocess.run([str(py), str(idf / 'tools/idf.py'), '-C', str(PROJECT), '-B', str(BUILD),
                        '-DIDF_TARGET=esp32s3', f'-DBOARD={args.board}', 'build'], env=env, check=True)
        package()
    if args.verify:
        verify()
    if args.flash:
        if not args.port or not Path(args.port).exists():
            raise ValueError('Pass the confirmed StopWatch serial port using --port')
        files = verify()
        # Only identity is read here: no backup/readback and no full-chip erase.
        command = [str(py), '-m', 'esptool', '--chip', 'esp32s3', '--port', args.port]
        output = subprocess.check_output(command + ['flash_id'], env=env, text=True)
        print(output, flush=True)
        if '16MB' not in output:
            raise ValueError('Expected a StopWatch with 16 MB flash')
        settings = json.loads((BUILD / 'flasher_args.json').read_text())['flash_settings']
        flash = command + ['--baud', '460800', '--before', 'default_reset', '--after', 'hard_reset',
                           'write_flash', '--flash_mode', settings['flash_mode'], '--flash_freq', '80m', '--flash_size', '16MB']
        for offset, path in files:
            flash += [offset, str(path)]
        subprocess.run(flash, env=env, check=True)


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
