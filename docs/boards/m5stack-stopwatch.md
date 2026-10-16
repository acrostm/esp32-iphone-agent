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
B changes brightness. The three pages cover connection state, task progress and diagnostics.
The task page shows phase, step, action, duration and the latest four operation
records. A new task or terminal result selects this page automatically. Results
remain visible after completion; missing task heartbeats for 15 seconds show
`STATUS LOST` instead of implying that execution is still healthy. Details show video rate and memory. Credentials and phone
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
For firmware diagnostics, connect the board to the Mac. For actual HID task
execution, move the board USB cable to the iPhone; a USB connection only to the
Mac cannot control the phone. Reconnect the unlocked phone to the Mac afterward
to retrieve saved App logs. Do not interrupt a running task just to retrieve logs.
See the upstream README for the full app workflow. Acceptance below covers this
StopWatch model; other ESP32-S3 displays require their own board profile and tests.

### Paired over Bluetooth but Wi-Fi does not connect

Bluetooth pairing and Wi-Fi association are separate steps. Once BLE is ready,
the app starts an encrypted read of the board's hotspot details. A single read is
shared by concurrent callers and times out after 30 seconds instead of waiting
forever; the 5-second notification watchdog does not interrupt that pairing read.
Open **Settings → Board → Wi-Fi** to see the network, password, copy action and
any connection error. The manual credentials are displayed as soon as they are
read, including when iOS cancels or never completes automatic joining.

A build without the Hotspot Configuration entitlement cannot join automatically.
Copy the password into iOS Settings → Wi-Fi, join the matching `ESP32Agent-XXXX`,
then return to the app and enable screen mirroring. A failed encrypted credential
read is a Bluetooth/pairing issue, not proof of a wrong Wi-Fi password. Retrying
shows the actual read error or timeout. Never publish the hotspot password in logs.

### Video readiness and task feedback

Task startup waits in the foreground for an actual decoded video frame before
sending the phone Home. A checked Screen Mirroring entry alone does not prove
video readiness. The native `status` response includes `ui:1` (task display
protocol), `vcfg` (codec configuration bytes), `vaus` (buffered access units),
`vwait` (waiting for a keyframe) and `vdrop` (GOP overflows). `diag` returns a
bounded list of protocol events without image payloads or credentials. Native
`snap` failures distinguish missing configuration, missing keyframes and missing
frames. Reconnect Screen Mirroring after a board restart.

During continuous streaming, already-delivered access units are reclaimed before
history fills the fixed PSRAM buffer. Unsent dependencies remain intact. Once the
cached IDR has been reclaimed, a separate whole-GOP snapshot cannot be replayed;
`snap` reports this explicitly instead of sending undecodable P-frames. A new
stream after that point requires a fresh keyframe/mirror session. This protects
an uninterrupted stream; it does not manufacture keyframes after packet loss.
Task status uses an independent request path, so a long snapshot cannot hold
terminal updates behind the HID FIFO. Startup tolerates a brief BLE reconnect,
and stream mode waits for a frame captured after Home rather than forcing a
whole-GOP snapshot. Startup failures are shown directly on the App Home screen.

The native-only `task <json>` command accepts bounded `phase`, `action`, `step`,
`ms`, `elapsed`, `code` and `reset` fields. The app sends normalized action names
and a two-second heartbeat, without prompts or typed text. Existing boards that
do not advertise `ui:1` receive no task display commands. Live Activity feedback
is also maintained on the phone. See the measured acceptance scope below.

Build: PASS (ESP-IDF 5.5.3, Apple Silicon)

Host tests: PASS (request parsing, task state/timing and stale heartbeats, shared C
protocols, 10,000-frame cache reclamation, 14 packaging/dependency tests)

Baseline device tests (before the repeated-run/text follow-up): PASS on 2026-10-16: flash hashes, USB
ping/status, user-confirmed display, A/B buttons and uptime, encrypted BLE
credential read, hotspot connection, AirPlay reception and App decoding
(718 × 1560 observed). The user confirmed the task “return Home, then finish”
completed successfully and the board displayed `done`. This is a basic task
regression, not a long-duration or arbitrary-task certification.

Unverified: automatic hotspot joining with a paid signing profile, other screen
models, battery readings, power consumption, suspend/wake and sustained or complex
agent tasks. Free Personal Team automatic joining remains unsupported by the
current signing configuration; manual joining is a temporary fallback, not the
completed automatic-networking feature.


## Repeated runs and text input

Task completion keeps continuous video running. Pause/resume preserves the native
transmit cursor even after delivered history has been reclaimed; a paused snapshot
hands its decoded position back to the stream. A new subscriber still requires an
independently decodable keyframe. The `vreplay` and `vstream` status fields distinguish
cached snapshot availability from the stream switch. Native live mode never falls
back to replaying an old GOP into the active decoder. A stale/broken image can remain
visible for review, but does not count as a healthy live screen.

In **Settings → Model → Advanced → Text input**, choose Automatic, Keyboard (ASCII)
or Clipboard. All models use the same entry path. ASCII text is split to the transport
budget; line breaks and tabs use separate keyboard commands. Unicode uses the configured
clipboard bridge; `paste TEXT` normalizes to the same action as `type TEXT`. The bridge
writes a local-only clipboard item with a two-minute expiration and sends Cmd+V only
after observing the clipboard change. It does not prove that the target field accepted
the paste. iOS clipboard availability, paste permission and field focus still require
physical-phone acceptance; failed clipboard preparation is reported rather than silently
sending unsupported Unicode to the HID keyboard. Clipboard mode replaces the clipboard.
A visible caret is sufficient focus evidence when a hardware keyboard hides the software
keyboard. The next screenshot must confirm the intended text before submission.

Typing/click/relative-move/key timeouts are not automatically replayed: their outcome
may already have reached the phone. Native control timeouts preserve live mode instead
of switching to a snapshot whose IDR might no longer exist.

**Temperature** supports Automatic compatibility, Omit and Custom. Explicit Omit/Custom
settings override Extra body JSON. Only Automatic removes temperature in response to an
explicit unsupported-parameter/value error. Reasoning-only empty replies get one larger
bounded retry budget; a second empty reply is an error, not a successful connection test.

Validation for these follow-up changes: 86 App tests pass, including 20 consecutive
in-memory runs, input chunking/Unicode routing, no duplicate text after timeout and
snapshot/live isolation. The native cursor test covers 1,000 pause/resume transitions;
shared C/sanitizer and package checks also pass. **Device tests for these follow-up
changes: NOT RUN.** The connected board could be read, but its current USB application
could not be switched remotely into ROM download mode; exclusive HID capture was denied
by macOS. No test typing was sent to the Mac. Phone background clipboard behavior,
repeated real tasks and the newly built firmware still need device acceptance.
