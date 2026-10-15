**English** · [简体中文](m5stack-stopwatch.zh_CN.md)

# M5Stack StopWatch C152

Maintainer: **jiachen**. Profile ID: `m5stack-stopwatch`.

## Scope

The native bridge uses ESP-IDF's NimBLE, Wi-Fi, NVS and FreeRTOS, Espressif
TinyUSB, and official M5Unified/M5GFX display/power drivers. The native build does
not install or link Arduino IDE/CLI/core or NimBLE-Arduino. The existing Arduino
implementation remains available for upstream devices through a separate entry.

M5Unified is pinned to `e125e73f28b6b83e621e48df432af426bb2ff935` for its native
StopWatch support. M5GFX is pinned to 0.2.33; all managed dependencies and hashes
are recorded in `hardware/esp-idf/dependencies.lock`. No driver sources are copied
into this repository. C AirPlay/bplist/GOP code is shared with upstream without
compiling its Arduino wrappers; GPL notices and licensing still apply.

The UI shows USB, BLE, Wi-Fi, AirPlay, IP, battery and uptime. A changes pages;
B changes brightness. Details show video rate and memory. Credentials and phone
screen content are never displayed. Display calls run on the UI task; commands
and video run on workers. Video yields between batches and reserves BLE buffers
for control messages. Initialization calls execute even when assertions are disabled.

External 5 V output, speaker, microphone, IMU and RTC are disabled. The rear
UART/USB mux is not changed. This does not preserve the factory stopwatch app or
implement every factory feature. Battery accuracy and power behavior need measurement.

## Build on Apple Silicon or Linux

Install ESP-IDF **5.5.3** with its ESP32-S3 tools using the
[official ESP-IDF instructions](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/get-started/).
Activate `export.sh`; use Python 3.10 or newer for the wrapper. Normal IDF
component management fetches the locked dependencies; no Arduino tools are needed.
The official M5 driver API is used directly through ESP-IDF CMake integration.

```bash
python3 hardware/tests/native/run.py
python3 hardware/build.py --board m5stack-stopwatch --build
python3 hardware/build.py --board m5stack-stopwatch --verify
```

Alternatively double-click `Build-StopWatch.command` from a configured environment.
An optional ignored `.idf-local.json` may hold `IDF_PATH`, `IDF_TOOLS_PATH`,
`IDF_PYTHON_ENV_PATH` and an `extra_bin` list. Do not commit machine-specific paths.
CI runs the native job separately from Arduino firmware and audits the compiler
commands for Arduino macros/cache paths as well as the component list.

The profile enables 16 MB quad flash and 8 MB octal PSRAM. A wrong board or failed
PSRAM/display initialization disables the bridge. The native USB device is
**StopWatch iPhone Agent** with CDC diagnostics and mouse/keyboard/consumer HID.
No pointer movement or typing is sent on startup. `0 ping` and `1 status` are
read-only CDC commands. Typing is ASCII; each mouse move is limited to ±4096 per axis.

## Flash and restore

Enter the board's download mode and confirm its current serial port, then run:

```bash
python3 hardware/build.py --board m5stack-stopwatch --flash --port /dev/cu.CONFIRMED_PORT
```

Or use `Flash-StopWatch.command`, which builds first and asks for the port.
The wrapper verifies manifest hashes, target, flash capacity, partition checksum
and layout before writing bootloader (`0x0`), partition table (`0x8000`) and
application (`0x10000`). It never writes NVS/PHY explicitly or erases the whole chip.
Factory settings can still become incompatible with the new layout. Do not use
the upstream 8 MB Arduino flash script for this board.

For factory recovery follow
[M5Stack's official guide](https://docs.m5stack.com/en/guide/stopwatch/restore_factory)
and select the exact StopWatch firmware. M5Burner is optional for factory restore;
it is not part of this native build pipeline.

## iPhone and acceptance

Use the existing `ios/IPhoneAgent.xcodeproj`. Set your own bundle identifier and
signing team for both IPhoneAgent and AgentActivity. Never commit
`ios/Config/Local.xcconfig`, signing material or model keys. A free Personal Team
can use empty `IA_ENTITLEMENTS` and `IA_WIDGET_ENTITLEMENTS` in local configuration
and join the hotspot manually. Enable Developer Mode and run on the unlocked phone.

Connect the board to the phone's data port, select `ESP32 Agent XXXX` over BLE,
join `ESP32Agent-XXXX` with credentials read by the app over encrypted BLE, enable
screen mirroring to **ESP32 Agent**, and enable AssistiveTouch and calibrate.
Keep cellular Internet available for the model API while on the board's hotspot.
See the upstream README for the full app workflow. Phone pairing, actual video
throughput and complete agent task execution still need acceptance testing.

Build: PASS (ESP-IDF 5.5.3, Apple Silicon)

Host tests: PASS (request parsing, screen state/timing, shared C tests, 14 packaging/dependency tests)

Device tests: PASS for this firmware: flash hashes, USB ping/status, and
user-confirmed display, A/B buttons and advancing uptime.

Unverified: BLE pairing, AirPlay, HID
calibration, battery readings, power consumption, suspend/wake and sustained agent tasks.
