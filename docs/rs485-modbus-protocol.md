# RS485 / Modbus RTU Protocol

This controller can act as a Modbus RTU slave so another controller, PLC, or automation device can command registered Hydra® lights.

## Physical Wiring

Default GPIO wiring:

| Signal | ESP32 GPIO | Notes |
| --- | ---: | --- |
| RX | 18 | Connect to RS485 transceiver RO |
| TX | 17 | Connect to RS485 transceiver DI |
| DE/RTS | 4 | Connect to transceiver DE and /RE control if using half-duplex auto-direction |
| GND | GND | Must share ground with the RS485 transceiver/controller |

The firmware uses ESP-IDF UART RS485 half-duplex mode. In this mode the UART driver controls the RTS pin as the transmit-enable signal.

## Enabling

RS485 is disabled by default. Enable it from the web UI:

1. Open the controller web UI.
2. In the `RS485 Slave` section, check `Enable RS485 slave`.
3. Set address, baud, parity, UART, TX, RX, and DE/RTS pins.
4. Click `Save RS485`.

The settings are saved to NVS and applied immediately. The current runtime state is also available through:

```http
GET /api/config/modbus
```

Default serial settings:

| Setting | Default |
| --- | --- |
| Mode | Modbus RTU slave |
| Enabled | false |
| Slave address | 10 |
| Baud | 19200 |
| Data bits | 8 |
| Parity | even |
| Stop bits | 1 |
| UART port | 1 |

Note: the bundled ESP-Modbus serial port applies 8 data bits and 1 stop bit internally. The UI exposes only settings that are applied at runtime.

## Register Addressing

All addresses below are zero-based holding-register offsets. Some Modbus tools display holding register offset `0` as `40001`. If your master uses 40001-style addressing, add 1 to the zero-based offset.

All values are unsigned 16-bit registers unless noted. Multi-register integers are little-word order: low word first, high word second.

Supported Modbus functions:

| Function | Use |
| --- | --- |
| 0x03 Read Holding Registers | Read status, mirrors, and command result registers |
| 0x06 Write Single Holding Register | Write one command/config register |
| 0x10 Write Multiple Holding Registers | Preferred for command blocks |

## System Registers

| Offset | Name | Access | Meaning |
| ---: | --- | --- | --- |
| 0 | magic | R | `0xA164` |
| 1 | register_map_version | R | `1` |
| 5 | controller_status | R | Bitfield |
| 6 | config_flags | R | Bitfield |
| 7 | registered_light_count | R | Number of registered lights |
| 8 | registered_group_count | R | Number of registered groups |
| 16 | modbus_status | R | `0` disabled, `1` slave ready, `2` master ready, `3` error |
| 17 | ble_scheduler_status | R | `0` idle, `1` busy, `2` backoff, `3` error |

`config_flags` bits:

| Bit | Meaning |
| ---: | --- |
| 4 | Modbus enabled |
| 5 | Modbus master mode enabled; currently forced off for slave operation |

## Light Blocks

Each registered light has a 100-register block.

| Light index | Base offset |
| ---: | ---: |
| 0 | 1000 |
| 1 | 1100 |
| 2 | 1200 |
| 3 | 1300 |

Light block offsets:

| Offset from base | Name | Access | Meaning |
| ---: | --- | --- | --- |
| 0 | present | R | `1` if a registered light exists at this index |
| 1 | enabled | R | `1` if enabled |
| 3 | last_result_code | R | Result of last accepted command sequence |
| 4 | last_command_seq | R | Last processed sequence |
| 5 | command_seq | W | Increment/change this to dispatch a command |
| 6 | command_code | W | Command enum below |
| 9 | timeout_seconds | W | Scene/command timeout hint |
| 10 | preset_id | W | Preset enum below |
| 11 | replace_flag | W | `1` replace current state, `0` merge where supported |
| 12 | ramp_from | W | Ramp start intensity, 0..1000 |
| 13 | ramp_to | W | Ramp end intensity, 0..1000 |
| 14 | ramp_duration_ms_low | W | Low word of duration |
| 15 | ramp_duration_ms_high | W | High word of duration |
| 16 | ramp_steps | W | Ramp step count |
| 20..28 | channel values | W | Brightness, cool white, blue, deep red, violet, UV, green, royal blue, moonlight |
| 40 | current_power | R | `0` off, `1` on, `2` unknown |
| 41..49 | current channel values | R | Last known channel state, same order as 20..28 |
| 50 | last_seen_rssi_abs | R | Absolute RSSI value |
| 52 | command_queue_depth | R | Pending command count for this light |

## Commands

Write command parameters first, then write a new `command_seq`. The controller dispatches when `command_seq != last_command_seq`.

Command codes:

| Code | Name | Required fields |
| ---: | --- | --- |
| 0 | No-op | `command_seq` only; acknowledges without dispatch |
| 1 | Off | `command_code`, `command_seq` |
| 2 | On | `command_code`, `command_seq` |
| 3 | Apply channels | Channel registers 20..28, `command_code`, `command_seq` |
| 4 | Preset | `preset_id`, `command_code`, `command_seq` |
| 5 | Ramp | Ramp registers 12..16, `command_code`, `command_seq` |
| 6 | Identify | `command_code`, `command_seq` |

Preset IDs:

| ID | Preset |
| ---: | --- |
| 0 | none |
| 1 | off |
| 2 | on |
| 3 | moonlight |
| 4 | blue moonlight |
| 5 | test 25 |

Result codes:

| Code | Meaning |
| ---: | --- |
| 0 | idle |
| 1 | accepted |
| 2 | running |
| 3 | success |
| 4 | partial failure |
| 10 | invalid command |
| 11 | invalid target |
| 12 | invalid channel |
| 13 | invalid intensity |
| 14 | busy |
| 15 | queue full |
| 20 | BLE connect failed |
| 21 | BLE confirmation timeout |
| 22 | unsupported light |
| 30 | internal error |

## Examples

### Turn Light 0 On

Write:

| Register | Value |
| ---: | ---: |
| 1006 | 2 |
| 1005 | 1 |

Then read:

| Register | Expected |
| ---: | --- |
| 1004 | `1` once processed |
| 1003 | `1` for accepted, or an error code |

### Turn Light 0 Off

Write:

| Register | Value |
| ---: | ---: |
| 1006 | 1 |
| 1005 | 2 |

Use a new sequence value each time. Reusing the same `command_seq` will not redispatch.

### Apply a Channel Mix to Light 0

This example sets brightness to 1000, blue to 600, royal blue to 700, moonlight to 50, and all other channels to 0.

Write registers 1020..1028:

| Register | Channel | Value |
| ---: | --- | ---: |
| 1020 | brightness | 1000 |
| 1021 | coolwhite | 0 |
| 1022 | blue | 600 |
| 1023 | deepred | 0 |
| 1024 | violet | 0 |
| 1025 | uv | 0 |
| 1026 | green | 0 |
| 1027 | royalblue | 700 |
| 1028 | moonlight | 50 |

Then write:

| Register | Value |
| ---: | ---: |
| 1006 | 3 |
| 1005 | next sequence |

### Apply Moonlight Preset to Light 0

Write:

| Register | Value |
| ---: | ---: |
| 1010 | 3 |
| 1006 | 4 |
| 1005 | next sequence |

### Ramp Light 0 Brightness

This ramps from 0 to 1000 over 20 seconds in 20 steps.

Write:

| Register | Value |
| ---: | ---: |
| 1012 | 0 |
| 1013 | 1000 |
| 1014 | 20000 |
| 1015 | 0 |
| 1016 | 20 |
| 1006 | 5 |
| 1005 | next sequence |

## Notes for Masters

- Prefer function 0x10 to write a coherent command block in one transaction.
- If using separate 0x06 writes, write `command_seq` last.
- Poll `last_command_seq` and `last_result_code` to confirm command acceptance.
- Light index order is the controller registry order shown in the web UI.
- The RS485 feature is a slave-only path; `master_mode_enabled` is forced off by the web config endpoint.
