# Contributing to HydraBridge ESP32

Thanks for helping improve HydraBridge ESP32. Contributions can be code, testing, documentation, bug reports, device captures, or practical feedback from real reef systems.

## Ways to Contribute

- Open issues for bugs, confusing setup steps, missing documentation, or feature requests.
- Test firmware builds on supported ESP32-S3 hardware and report what worked or failed.
- Improve documentation, quickstart steps, screenshots, wiring notes, and troubleshooting guides.
- Submit pull requests for focused fixes or features.
- Review open pull requests and test changes before they are merged.
- Share safe, local automation examples for MQTT, RS485/Modbus, Home Assistant, or PLC use.

## Pull Requests

Before opening a pull request:

- Keep changes focused on one bug or feature.
- Run the host tests when changing shared logic.
- Run an ESP-IDF build when changing firmware code.
- Update docs when behavior, settings, APIs, wiring, or setup steps change.
- Avoid committing secrets, WiFi credentials, private MQTT broker details, device keys, or personal network data.

Useful local checks:

```bash
source ~/esp/esp-idf/export.sh
cmake -S host_tests -B host_tests/build
cmake --build host_tests/build
./host_tests/build/host_tests
idf.py build
```

## Bug Reports

Good bug reports include:

- Firmware version or commit hash.
- ESP32 board model.
- Serial log around the failure.
- Browser/API steps to reproduce the issue.
- Whether the issue happens after reboot, after factory reset, or only after changing settings.
- Screenshots when the web UI is involved.

Please remove private values from logs and screenshots before posting them.

## Helping With More Mobius Devices

You do not need to be a developer to help add support for more Mobius devices.

If you own Mobius-compatible devices other than AquaIllumination Hydra lights, one of the most useful contributions is Bluetooth traffic captures. A USB Nordic Bluetooth sniffer lets you capture BLE communication between the official app and your device, then provide packet capture files that can be analyzed and used to implement additional device support.

Helpful hardware:

- Nordic nRF52840 USB Dongle, or another Nordic device supported by the nRF Sniffer for Bluetooth LE.
- A computer that can run Wireshark.
- The official app and the Mobius device you want to capture.

Useful captures:

- Device discovery / pairing.
- Reading device information.
- Turning the device on and off.
- Changing a simple setting.
- Applying a preset or scene.

When sharing captures:

- Prefer `.pcapng` files from Wireshark.
- Include the device model and firmware version if visible.
- Describe exactly what actions you performed while capturing.
- Do not include captures from devices or networks you do not own or have permission to inspect.

Open an issue before buying hardware if you are unsure whether your device is useful for the project.

## Legal and Safety Notes

Only contribute material you have the right to share. Do not submit vendor source code, decompiled application code, copyrighted artwork, cloud credentials, product keys, or private user data.

HydraBridge ESP32 controls aquarium lighting. Test changes carefully, start with low intensities, and use this project at your own risk.
