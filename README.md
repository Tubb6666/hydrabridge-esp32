# Hydra 64HD BLE Controller

ESP32-S3 controller that talks to **Aqua Illumination Hydra 64HD** lights directly over BLE using the reverse-engineered myAI / Mobius protocol, with a Modbus RTU slave register map for PLC integration (designed against a Productivity 2000 P2000 / CODESYS CPU).

**Target hardware**: Waveshare Industrial ESP32-S3 Control Board with onboard RS485 + CAN. Any ESP32-S3 dev board works for the BLE/MQTT/web paths; the RS485 path needs a wired-up transceiver.

## What works today

| Surface | Status |
|---|---|
| FSCI / myAI protocol codec | ✅ Byte-exact verified against captured hardware traces (CRC, builder, parser, reassembly) |
| Channel model + presets | ✅ 9-channel Hydra 64HD set, 5 built-in presets (`off`, `on`, `moonlight`, `blue_moonlight`, `test_25`) |
| LiveDemoScene payload builder | ✅ Reproduces every captured On/Off/LBM/Moonlight TX frame byte-for-byte |
| Light registry + command queue | ✅ 4 lights, 4 groups, per-light FIFO with coalescing, NVS persistence |
| Command engine | ✅ Unified `ce_request_t` → validate → expand → enqueue from any source |
| Modbus RTU slave | ✅ ESP-Modbus driver starts at boot; snapshot-on-`command_seq` dispatch wired |
| Modbus register map | ✅ Full spec map (system + 4 light + 4 group blocks) with result codes 1:1 to `ce_result_t` |
| MQTT JSON parser | ✅ Every payload shape from the spec |
| OTA partition + rollback | ✅ Two app slots, `esp_ota_mark_app_valid_cancel_rollback` on good boot |
| NimBLE central (scan / connect / GATT) | ⏳ Stubbed; first hardware session |
| BLE worker task (drains queue → writes) | ⏳ Stubbed; first hardware session |
| MQTT client lifecycle | ⏳ Parser ready; connect/subscribe/publish TODO |
| Web UI | ⏳ Component scaffolded; HTML + REST handlers TODO |
| Home Assistant discovery | ⏳ TODO |

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
  mqtt_bridge/               JSON command-payload parser (MQTT client lifecycle TBD)
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

## Build

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

Currently **159 tests** across 12 test files. Every captured CRC, every captured TX/RX frame, every preset, every Modbus register dispatch path, and every MQTT payload shape is covered.

## RS485 / Modbus quick start

Power on the board → at boot the controller becomes a Modbus RTU slave on UART1:

| Setting | Default | Override |
|---|---|---|
| Slave address | `10` | NVS (`mb_cfg` namespace) |
| Baud | `19200` | NVS |
| Format | `8E1` | NVS |
| UART TX / RX / DE pins | `17 / 18 / 4` | NVS (confirm against your board's silkscreen) |

A Modbus master reads:

```
register 0  → 0xA164    (magic; if you see this the controller is alive)
register 1  → 1         (register-map version)
register 16 → 1         (modbus_status = slave_ready)
```

To turn light 0 on:

```
FC16 base=1000 count=12 payload=[seq=1, code=2 (ON), 0,0,0,0,0, preset=0, 0,0,0, 0]
```

The controller picks up the `command_seq` increment, dispatches via `command_engine`, and writes the result code to register `1003`. Full register layout in [`components/modbus_interface/include/modbus_registers.h`](components/modbus_interface/include/modbus_registers.h).

## References

- [BLE protocol reference](docs/ble-protocol-reference.md) — clean technical writeup
- [Implementation plan](docs/esp32-hydra64hd-controller-plan.md) — 1763-line spec
- [Reverse-engineering log](docs/myai-ble-reverse-engineering.md) — capture-by-capture research
