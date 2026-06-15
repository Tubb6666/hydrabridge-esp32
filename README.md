# HydraBridge ESP32

## Legal Notice

HydraBridge ESP32 is an independent, open-source interoperability project. It is not affiliated with, endorsed by, sponsored by, or approved by AquaIllumination, Mobius®, or their owners.

This project was reverse engineered using a clean-room method solely to enable local interoperability with the author's personally owned AquaIllumination Hydra® lights in a personal PLC reef controller. The repository is intended to contain independently written code, protocol documentation, and functional facts needed for interoperability only; it does not include vendor firmware, vendor source code, vendor application code, copyrighted artwork, cloud services, or product keys.

Under U.S. law, this project is intended to rely on the interoperability reverse-engineering provisions of [17 U.S.C. § 1201(f)](https://www.law.cornell.edu/uscode/text/17/1201), which permit identifying, analyzing, and sharing information necessary for interoperability of independently created computer programs, and on fair-use principles under [17 U.S.C. § 107](https://www.law.cornell.edu/uscode/text/17/107). U.S. courts have recognized this kind of software reverse engineering and clean-room reimplementation for compatibility in cases including:

- [Sega Enterprises Ltd. v. Accolade, Inc., 977 F.2d 1510 (9th Cir. 1992)](https://www.copyright.gov/fair-use/summaries/segaenters-accolade-9thcir1992.pdf), recognizing reverse engineering of software to access unprotected functional elements needed for compatibility as fair use.
- [Sony Computer Entertainment, Inc. v. Connectix Corp., 203 F.3d 596 (9th Cir. 2000)](https://law.justia.com/cases/federal/appellate-courts/F3/203/596/474793/), recognizing intermediate copying during reverse engineering as fair use when used to build a non-infringing interoperable implementation.
- [Lexmark International, Inc. v. Static Control Components, Inc., 387 F.3d 522 (6th Cir. 2004)](https://law.justia.com/cases/federal/appellate-courts/F3/387/522/532493/), rejecting an overbroad use of the DMCA to block interoperable replacement components and warning against using access controls to create replacement-part monopolies.
- [Computer Associates International, Inc. v. Altai, Inc., 982 F.2d 693 (2d Cir. 1992)](https://digital-law-online.info/cases/23PQ2D1241.htm), applying abstraction-filtration-comparison principles and recognizing that independently rewritten software constrained by functional requirements can avoid copyright infringement.

This notice is provided for project context only and is not legal advice. Laws vary by jurisdiction, and downstream users are responsible for ensuring their own use complies with applicable law, license terms, and device ownership rights.

HydraBridge ESP32 is an open-source gateway that connects AquaIllumination Hydra® lights to industrial and home automation systems.

Local control for AquaIllumination Hydra® lights over RS485 and MQTT.

Features include:

- Automatic Hydra® light discovery
- Local BLE communication
- MQTT integration
- RS485 communication
- Home Assistant auto-discovery
- Reusable lighting profiles
- Scheduled lighting control
- Sunrise and sunset ramping
- Multi-light group support
- Fully local operation with no cloud dependency

This ESP32-S3 controller talks to **AquaIllumination Hydra®** lights directly over BLE using the reverse-engineered myAI / Mobius® protocol, with MQTT integration and a Modbus RTU slave register map for PLC integration.

**Target hardware**: Waveshare Industrial ESP32-S3 Control Board with onboard RS485 + CAN. Any ESP32-S3 dev board works for the BLE/MQTT/web paths; the RS485 path needs a wired-up transceiver.

## What works today

| Surface | Status |
|---|---|
| FSCI / myAI protocol codec | ✅ Byte-exact verified against captured hardware traces (CRC, builder, parser, reassembly) |
| Channel model + presets | ✅ 9-channel Hydra® 64HD set, 5 built-in presets (`off`, `on`, `moonlight`, `blue_moonlight`, `test_25`) |
| LiveDemoScene payload builder | ✅ Reproduces every captured On/Off/LBM/Moonlight TX frame byte-for-byte |
| Light registry + command queue | ✅ 4 lights, 4 groups, per-light FIFO with coalescing, NVS persistence |
| Command engine | ✅ Unified `ce_request_t` → validate → expand → enqueue from any source |
| Modbus RTU slave | ✅ Optional ESP-Modbus RTU slave; disabled by default, configurable from the web UI |
| Modbus register map | ✅ Full spec map (system + 4 light + 4 group blocks) with result codes 1:1 to `ce_result_t` |
| MQTT bridge | ✅ Optional MQTT client; disabled by default; configurable from the web UI; light/group command subscriptions and result topics |
| OTA partition + rollback | ✅ Two app slots, `esp_ota_mark_app_valid_cancel_rollback` on good boot |
| WiFi setup portal | ✅ First-boot setup hotspot at `HydraBridge-Setup` / `http://192.168.1.10/`, configurable from Settings |
| NimBLE central (scan / connect / GATT) | ⏳ Stubbed; first hardware session |
| BLE worker task (drains queue → writes) | ⏳ Stubbed; first hardware session |
| Web UI | ✅ Local browser UI for discovery, lights, groups, profiles, RS485, and MQTT settings |
| Home Assistant discovery | ✅ Optional MQTT discovery publishing when MQTT is enabled and the HA checkbox is on |

Today: a PLC can speak Modbus to the controller and see the queue accept commands. The light won't actually light until the BLE worker lands in the first hardware session.

## Repo layout

```
main/                        ESP-IDF app entrypoint + idf_component.yml
components/
  fsci_codec/                CRC + frame builder + parser + reassembly
  hydra64hd_protocol/        SupportedColorChannels read + LiveDemoScene write payload builders
  channel_model/             Canonical 9-channel set, name lookup, validation
  preset_engine/             5 built-in presets
  light_registry/            Registered lights + named groups (NVS-backed)
  command_queue/             Per-light bounded FIFO with coalescing
  command_engine/            ce_request_t → ce_result_t pipeline (Modbus / MQTT / web all converge here)
  modbus_interface/          Holding-register store + ESP-Modbus driver + RS485 UART wiring
  mqtt_bridge/               Optional MQTT client + JSON command-payload parser
  ble_light_client/          NimBLE central — stubbed
  config_store/              Per-category NVS-backed config (controller / modbus / wifi / mqtt)
  event_log/                 Bounded ring + password redaction
  ota_update/                Web-upload OTA + rollback cancel
  web_ui/                    HTTP server — stubbed
docs/
  esp32-hydra64hd-controller-plan.md   Full implementation spec
  myai-ble-reverse-engineering.md      Capture-by-capture research log
  ble-protocol-reference.md            Clean technical reference for the BLE protocol
host_tests/                  Unity-based host tests for pure-C modules
```

## Quickstart

Nontechnical users should start with the release firmware zip instead of building from source:

1. Download the latest `hydrabridge-esp32-firmware-*.zip` from GitHub Releases.
2. Flash it with `flash.sh` on Linux/macOS or `flash.ps1` on Windows.
3. Connect to the first-boot `HydraBridge-Setup` WiFi hotspot.
4. Open `http://192.168.1.10/` and save your home WiFi settings.

Full steps are in [docs/quickstart.md](docs/quickstart.md).

## Build From Source

Install ESP-IDF 5.4+, then:

```bash
. ~/esp/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

`esp-modbus` is pulled automatically via the IDF Component Registry on first configure.

## Host tests

Pure-C modules (protocol codec, channel model, presets, registries, queue, engine, Modbus store, MQTT parser) compile to a Linux binary using Unity:

```bash
. ~/esp/esp-idf/export.sh   # Unity is pulled from $IDF_PATH/components/unity
cmake -S host_tests -B host_tests/build
cmake --build host_tests/build
./host_tests/build/host_tests
```

Currently **170 tests** across 12 test files. Every captured CRC, every captured TX/RX frame, every preset, every Modbus register dispatch path, and every MQTT payload shape is covered.

## RS485 / Modbus quick start

RS485 is disabled by default. Enable it from the web UI `RS485 Slave` section, then configure the external Modbus master with matching settings:

| Setting | Default | Override |
|---|---|---|
| Slave address | `10` | NVS (`mb_cfg` namespace) |
| Baud | `19200` | NVS |
| Data/stop bits | `8` / `1` | Fixed by bundled ESP-Modbus serial port |
| Parity | `even` | Web UI / NVS |
| UART TX / RX / DE pins | `17 / 18 / 4` | Web UI / NVS |

A Modbus master reads, once RS485 is enabled:

```
register 0  → 0xA164    (magic; if you see this the controller is alive)
register 1  → 1         (register-map version)
register 16 → 1         (modbus_status = slave_ready)
```

To turn light 0 on, write command code first and sequence last:

```
register 1006 → 2   (command_code = ON)
register 1005 → 1   (command_seq = new sequence)
```

The controller picks up the `command_seq` increment, dispatches via `command_engine`, and writes the result code to register `1003`. Full usage docs are in [`docs/rs485-modbus-protocol.md`](docs/rs485-modbus-protocol.md), with constants in [`components/modbus_interface/include/modbus_registers.h`](components/modbus_interface/include/modbus_registers.h).

## References

- [BLE protocol reference](docs/ble-protocol-reference.md) — clean technical writeup
- [RS485 / Modbus protocol](docs/rs485-modbus-protocol.md) — register map and command examples
- [Implementation plan](docs/esp32-hydra64hd-controller-plan.md) — 1763-line spec
- [Reverse-engineering log](docs/myai-ble-reverse-engineering.md) — capture-by-capture research

## License

HydraBridge ESP32 is released under the [MIT License](LICENSE).

## Trademark Notice

Hydra® and Mobius® are registered trademarks of ECOTECH, LLC. EcoTech Marine and AquaIllumination are brands associated with EcoTech, LLC and/or Aperture, LLC. HydraBridge ESP32 is an independent interoperability project and is not sanctioned, authorized, affiliated with, endorsed by, sponsored by, maintained by, or supported by EcoTech, LLC, EcoTech Marine, AquaIllumination, Aperture, LLC, or any of their respective parents, subsidiaries, affiliates, successors, or assigns. Use of third-party names and marks is solely nominative, to identify compatible hardware and protocols, and does not imply any commercial relationship, approval, warranty, or fitness representation by those trademark owners.
