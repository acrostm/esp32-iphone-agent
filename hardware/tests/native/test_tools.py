import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('native_build', ROOT / 'hardware/build.py')
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


def partition_bytes(partitions=tool.EXPECTED_PARTITIONS):
    data = b''.join(struct.pack('<HBBII16sI', 0x50aa, kind, subtype, address, size,
                             label.encode(), 0) for kind, subtype, address, size, label in partitions)
    return data + b'\xeb\xeb' + b'\xff' * 14 + hashlib.md5(data).digest() + b'\xff' * 32


class Layout(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name).resolve()
        for name in ('boot.bin', 'app.bin'):
            (self.root / name).write_bytes(b'\xe9' * 32)
        (self.root / 'part.bin').write_bytes(partition_bytes())
        self.args = {'extra_esptool_args': {'chip': 'esp32s3'},
                     'flash_settings': {'flash_size': '16MB', 'flash_mode': 'dio', 'flash_freq': '80m'},
                     'flash_files': {'0x0': 'boot.bin', '0x8000': 'part.bin', '0x10000': 'app.bin'}}

    def tearDown(self):
        self.tmp.cleanup()

    def test_valid(self):
        self.assertEqual(len(tool.check_layout(self.args, self.root)), 3)

    def test_wrong_chip(self):
        self.args['extra_esptool_args']['chip'] = 'esp32c3'
        with self.assertRaises(ValueError):
            tool.check_layout(self.args, self.root)

    def test_wrong_flash(self):
        self.args['flash_settings']['flash_size'] = '8MB'
        with self.assertRaises(ValueError):
            tool.check_layout(self.args, self.root)

    def test_nvs_write(self):
        self.args['flash_files']['0x9000'] = 'app.bin'
        with self.assertRaises(ValueError):
            tool.check_layout(self.args, self.root)

    def test_path_escape(self):
        self.args['flash_files']['0x0'] = '../boot.bin'
        with self.assertRaises(ValueError):
            tool.check_layout(self.args, self.root)

    def test_missing(self):
        (self.root / 'app.bin').unlink()
        with self.assertRaises(ValueError):
            tool.check_layout(self.args, self.root)

    def test_oversize(self):
        (self.root / 'part.bin').write_bytes(b'a' * 4097)
        with self.assertRaises(ValueError):
            tool.check_layout(self.args, self.root)

    def test_checksum(self):
        data = bytearray(partition_bytes())
        data[8] ^= 1
        (self.root / 'part.bin').write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'checksum'):
            tool.check_layout(self.args, self.root)

    def test_overlapping_layout(self):
        parts = list(tool.EXPECTED_PARTITIONS)
        parts[2] = (0, 0, 0x9000, 0xff0000, 'factory')
        (self.root / 'part.bin').write_bytes(partition_bytes(parts))
        with self.assertRaisesRegex(ValueError, 'layout'):
            tool.check_layout(self.args, self.root)

    def prepare_manifest(self):
        (self.root / 'flasher_args.json').write_text(json.dumps(self.args))
        (self.root / 'config').mkdir()
        for name in ('stopwatch_agent.elf', 'stopwatch_agent.map', 'config/sdkconfig.h'):
            (self.root / name).write_text('test')
        (self.root / 'project_description.json').write_text(json.dumps({'target': 'esp32s3', 'build_components': ['bt']}))
        (self.root / 'compile_commands.json').write_text('[]')
        manifest = {'schema': 1, 'board': tool.BOARD, 'target': 'esp32s3',
                    'framework': 'ESP-IDF 5.5.3', 'flash_bytes': 16777216,
                    'files': [{'path': p.relative_to(self.root).as_posix(),
                               'sha256': tool.digest(p), 'size': p.stat().st_size}
                              for p in tool.artifact_paths(self.root)]}
        (self.root / 'firmware-manifest.json').write_text(json.dumps(manifest))
        return manifest

    def test_manifest_verifies(self):
        self.prepare_manifest()
        self.assertEqual(len(tool.verify(self.root)), 3)

    def test_changed_firmware(self):
        self.prepare_manifest()
        (self.root / 'app.bin').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'changed'):
            tool.verify(self.root)

    def test_missing_manifest_entry(self):
        manifest = self.prepare_manifest()
        manifest['files'].pop()
        (self.root / 'firmware-manifest.json').write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, 'Missing'):
            tool.verify(self.root)

    def test_arduino_component(self):
        self.prepare_manifest()
        (self.root / 'project_description.json').write_text(json.dumps({'target': 'esp32s3', 'build_components': ['arduino']}))
        with self.assertRaisesRegex(ValueError, 'Arduino'):
            tool.audit(self.root)

    def test_arduino_macro(self):
        self.prepare_manifest()
        (self.root / 'compile_commands.json').write_text(json.dumps([{'command': 'cc -DARDUINO=10800'}]))
        with self.assertRaisesRegex(ValueError, 'Arduino'):
            tool.audit(self.root)


if __name__ == '__main__':
    unittest.main()
