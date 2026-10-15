**English** · [简体中文](hardware-support.zh_CN.md)

# Hardware support

| Board ID | Hardware | Firmware path | Framework | Validation |
| --- | --- | --- | --- | --- |
| upstream generic S3 | Original 8 MB S3 / 8 MB OPI PSRAM configuration | `hardware/hid_bridge`, `hardware/flash.sh` | Arduino ESP32 | Existing upstream CI |
| `m5stack-stopwatch` | M5Stack StopWatch C152, S3, 16 MB flash, 8 MB OPI PSRAM, 466×466 round CO5300 | `hardware/esp-idf` | ESP-IDF 5.5.3; no Arduino | Build/host tests and basic device regression passed; phone acceptance pending |

See [StopWatch setup and acceptance](boards/m5stack-stopwatch.md).
A screen and an ESP32-S3 chip alone do not establish compatibility. Other boards
need their own display/power/pin mapping and acceptance testing; unsupported board
IDs fail configuration. Keep existing upstream hardware support when adding boards.

## Adding a board

1. Add `hardware/esp-idf/boards/<vendor-model>/board.json`, `sdkconfig.defaults`,
   and `partitions.csv`; record chip, actual flash/PSRAM, display and maintainer.
   Never infer flash or display pins from another S3 board.
2. Add a board-specific initialization/UI implementation and explicitly select it
   in CMake. The current UI checks M5Unified's StopWatch detection before enabling
   the bridge. A new profile alone must not enable unsupported hardware.
3. Extend `hardware/build.py`'s supported IDs, expected partitions and output path;
   do not weaken an existing board's flash guards. Keep one output directory per board.
4. Pin dependencies, add host tests and a CI matrix entry, and document the new
   profile in both languages. Keep generated SDK config, dependencies, caches,
   personal paths, credentials and device logs out of Git.
5. Record build, host tests and device tests separately. Hardware acceptance must
   include display/buttons, USB diagnostics, BLE pairing, AirPlay, HID calibration,
   sustained operation, power and suspend/wake. Do not equate compilation with
   complete phone control.

Build artifacts are generated under `hardware/build/<board-id>/`, including the
BIN, ELF, MAP and SHA-256 manifest. Source and the dependency lock belong in Git;
firmware binaries belong in CI artifacts/releases, not source commits. A flash
operation requires an explicit port; it never guesses from the first USB device.
