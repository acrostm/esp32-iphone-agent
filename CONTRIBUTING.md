# Contributing

Issues and pull requests are welcome. Keep changes small and focused, and match the surrounding code.

Before sending a change, run the checks for what you touched:

```bash
# app (Xcode 26+)
cd ios && xcodegen && xcodebuild test -scheme IPhoneAgent -destination 'platform=iOS Simulator,name=iPhone 17 Pro'
# firmware
hardware/flash.sh --build-only && make -C hardware/tests
# legacy server
cd server && pip install -e ".[dev]" && ruff check . && pytest
```

Never commit secrets: `ios/Config/Local.xcconfig` and `hardware/hid_bridge/secrets.h` are git-ignored on purpose.
Run data exported from the app is redacted, but check it before attaching it to an issue.

Code under `hardware/hid_bridge/src/airplay/` is GPL-3.0 (ported from UxPlay); contributions there are GPL-3.0 too.

## Native ESP-IDF boards

Use the [hardware support guide](docs/hardware-support.md) when adding a model.
For StopWatch, run `python3 hardware/tests/native/run.py` and
`python3 hardware/build.py --board m5stack-stopwatch --build --verify` in ESP-IDF 5.5.3.
The existing Arduino firmware command above is for the upstream hardware path.
Report build, host tests and device acceptance separately; do not commit firmware
binaries, local signing config or device logs.
