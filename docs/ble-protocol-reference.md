# Hydra® 64HD BLE Protocol — Technical Reference

Clean reference for the myAI / Mobius® BLE protocol as used by the Aqua Illumination **Hydra® 64HD** light. This document tells you *what to send and what comes back*. For the experimental derivation see [`myai-ble-reverse-engineering.md`](myai-ble-reverse-engineering.md); for application-level design see [`esp32-hydra64hd-controller-plan.md`](esp32-hydra64hd-controller-plan.md).

Every fact in this doc is byte-exact verified against captured hardware traces (`docs/myai-ble-reverse-engineering.md` lines 906–1149) and exercised by the host-test suite in `host_tests/tests/`.

---

## 1. BLE service

The light advertises with name `MOBIUS` and exposes one proprietary GATT service:

```
service:    01FF0100-BA5E-F4EE-5CA1-EB1E5E4B1CE0
```

### Characteristics

| UUID                                       | Direction | Property      | Use |
|--------------------------------------------|-----------|---------------|-----|
| `01FF0101-BA5E-F4EE-5CA1-EB1E5E4B1CE0`     | RX        | Notify        | Response fragments (non-final) |
| `01FF0102-BA5E-F4EE-5CA1-EB1E5E4B1CE0`     | RX        | Notify        | Final response fragment — triggers parse |
| `01FF0103-BA5E-F4EE-5CA1-EB1E5E4B1CE0`     | TX        | Write w/o rsp | Command fragments (non-final) |
| `01FF0104-BA5E-F4EE-5CA1-EB1E5E4B1CE0`     | TX        | Write w/o rsp | Final command fragment — triggers fixture-side parse |

**Connection setup**: GAP connect, discover services, subscribe to RX Data + RX Final CCCDs, then proceed to attribute reads/writes. No BLE bonding required on this firmware — the myAI Android app uses unsecured GATT.

A single command that fits within the negotiated MTU is sent in one write to **TX Final**. Larger commands are split: all but the last chunk go to **TX Data**, the last chunk goes to **TX Final**. Responses arrive the same way in reverse.

---

## 2. FSCI frame format

Every command and response is wrapped in an FSCI (Freescale Serial Communications Interface) frame. The light uses a fixed 11-byte overhead per frame.

```
offset  size  meaning
0       1     start marker = 0x02
1       1     op_group
2       1     op_code
3       2     msg_id          (little-endian)
5       2     reserved        (= 0x0000 in v1)
7       2     payload_len     (little-endian)
9       N     payload
9+N     2     CRC16           (little-endian)
```

Total frame length: `11 + payload_len`.

### CRC

**CRC-16/CCITT-FALSE**: polynomial `0x1021`, init `0xFFFF`, no input/output reflection, xorout `0`. Computed over frame bytes `[1 .. 8+N]` inclusive — i.e. starting at `op_group` and ending at the last payload byte. Result stored little-endian at offsets `9+N` and `10+N`.

Golden vectors (captured TX frames from `docs/myai-ble-reverse-engineering.md`):

| Command          | CRC over bytes [1..8+N] |
|------------------|-------------------------|
| Lights On (all channels = 1000) | `0x80AC` |
| Lights Off (all = 0)            | `0xCA03` |
| Brightness 20 + Moonlight 20    | `0x0490` |
| Brightness 1000 + Moonlight 50  | `0x9946` |

Reference implementation in `components/fsci_codec/src/fsci_codec.c` (`fsci_crc16`); verified against 13 captured vectors in `host_tests/tests/test_crc16.c`.

### Op groups & codes

| Value | Constant                       | Meaning |
|-------|--------------------------------|---------|
| `0xDE` | `FSCI_OG_C2CI_REQUEST`        | Master → light request |
| `0xDF` | `FSCI_OG_C2CI_CONFIRM`        | Light → master response |
| `0x17` | `FSCI_OC_LEGACY_GET_C2_ATTR`  | GetC2Attr (read an attribute) |
| `0x18` | `FSCI_OC_LEGACY_SET_C2_ATTR`  | SetC2Attr (write an attribute) |

### Message IDs

A 16-bit, little-endian, monotonically incrementing counter. The light echoes the request `msg_id` in its confirm so requests and responses can be correlated. Wrap at `0xFFFF` is acceptable; the validated low-range behavior is independent of the upper wrap policy.

---

## 3. GetC2Attr — reading an attribute

Used to read `SupportedColorChannels` after connecting. 4-byte payload:

```
offset  size  meaning
0       2     attribute_id   (little-endian)
2       1     start_index
3       1     count
```

### Capture: SupportedColorChannels, count = 9

```
TX:  02 DE 17 46 00 00 00 04 00 85 03 00 09 9A 24

RX:  02 DF 17 46 00 00 00 0F 00 00 85 03 00 09 01
     01 10 11 19 17 15 13 12 1E 54 41
```

The TX payload `85 03 00 09` is `attr=0x0385 (901, SupportedColorChannels), start=0, count=9`.

The RX payload structure for a successful GetC2Attr response is:

```
offset  size  meaning
0       1     status                (0 = success)
1       2     attribute_id          (little-endian, echo)
3       1     start_index           (echo)
4       1     count                 (echo)
5       1     element_length        (= 1 for SupportedColorChannels)
6       N     element bytes
```

For SupportedColorChannels on the Hydra® 64HD the element bytes are the channel visual IDs in fixture order:

```
01 10 11 19 17 15 13 12 1E
```

This is the canonical channel order. Every command that touches multiple channels must emit them in this exact order.

---

## 4. SetC2Attr — writing an attribute (LiveDemoScene)

The only write used for live control in v1. 5-byte wrapper + 51-byte LiveDemoScene value = 56-byte payload.

### Wrapper

```
offset  size  meaning
0       2     attribute_id     (LE; = 0x0197 for LiveDemoScene)
2       1     start_index      (= 0)
3       1     count            (= 1)
4       1     element_length   (= 0x33 = 51 bytes)
```

### LiveDemoScene value (51 bytes)

```
offset  size  meaning
0       4     primitive_type = VisualV1 + 3-byte zero padding (01 00 00 00)
4       2     scene_id       (LE; = 1)
6       2     timeout_sec    (LE; e.g. 0x003C = 60s)
8       16    scene_name     (NUL-padded; empty for live demo)
24      27    9 channel triples (3 bytes each)
```

### Channel triple format

```
offset  size  meaning
0       1     visual_id      (e.g. 0x01 = Brightness)
1       2     intensity      (LE; 0..1000)
```

### Channel order (must match `SupportedColorChannels` response)

| Index | visual_id | Canonical name | Notes |
|-------|-----------|----------------|-------|
| 0 | `0x01` | `brightness`  | Master intensity |
| 1 | `0x10` | `coolwhite`   | |
| 2 | `0x11` | `blue`        | |
| 3 | `0x19` | `deepred`     | |
| 4 | `0x17` | `violet`      | |
| 5 | `0x15` | `uv`          | |
| 6 | `0x13` | `green`       | |
| 7 | `0x12` | `royalblue`   | |
| 8 | `0x1E` | `moonlight`   | Dim white moonlight emitter on this fixture |

> **Important**: 8-channel payloads are accepted at the FSCI status level but do **not** produce visible LED output on the tested Hydra® 64HD. All 9 triples are required even when several are zero.

### Intensity rules

- Range `0..1000` for every channel. Out-of-range values: this codebase's `command_engine` returns `INVALID_INTENSITY`; the fixture's own validation behavior at the edges isn't fully characterized.
- `Brightness = 0` turns the light off regardless of other channels.
- "Blue moonlight" must be synthesized by mixing low `blue`/`royalblue`/`violet` (e.g. 40/80/20) — there is no separate blue-moonlight channel on this fixture.

### Capture: Lights On

All 9 channels at 1000, timeout 60s, msg_id 0x0050. 67 bytes total on the wire:

```
02 DE 18 50 00 00 00 38 00 97 01 00 01 33 01 00
00 00 01 00 3C 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 01 E8 03 10 E8 03 11 E8 03 19
E8 03 17 E8 03 15 E8 03 13 E8 03 12 E8 03 1E E8
03 AC 80
```

Confirm (14 bytes — short success response):

```
02 DF 18 50 00 00 00 03 00 00 FF FF 59 E6
```

The confirm payload is 3 bytes: `status=0x00 (success)` followed by `FF FF` (semantics unknown — appears to be an unused field returned consistently).

### Reference implementation

`components/hydra64hd_protocol/src/hydra64hd_protocol.c`: `hydra64_build_live_demo_scene_write(state, timeout_seconds, out, out_cap)` produces this exact byte sequence. Verified byte-for-byte against the four captured TX frames in `host_tests/tests/test_hydra64hd_protocol.c`.

---

## 5. End-to-end example — "turn the light on"

Composed by chaining `hydra64_build_live_demo_scene_write(...)` into `fsci_build(FSCI_OG_C2CI_REQUEST, FSCI_OC_LEGACY_SET_C2_ATTR, msg_id, payload, 56, ...)`.

1. Build the LiveDemoScene payload (56 bytes wrapper+value) with every channel at 1000 and timeout 60.
2. Wrap in an FSCI frame: prepend `[0x02, 0xDE, 0x18, msg_id_LE, 0x00, 0x00, 0x38, 0x00]`, append CRC16 little-endian over bytes `[1..64]`.
3. Write to TX Final (or split between TX Data and TX Final if it exceeds the MTU).
4. Wait for a notify on RX Final with matching `msg_id`. Successful payload starts with `0x00`.

For the Hydra® 64HD's typical MTU (~23 bytes default, often negotiated up), the 67-byte On frame splits into roughly 3 chunks: 2 to TX Data, 1 to TX Final.

---

## 6. Attribute IDs cheat-sheet

| Hex     | Decimal | Name                      | Used in v1? |
|---------|---------|---------------------------|-------------|
| `0x0197` | 407    | `LiveDemoScene`            | ✅ Write only |
| `0x0385` | 901    | `SupportedColorChannels`   | ✅ Read once after connect |
| `0x0398` | 920    | `Intensity` (legacy direct)| ❌ Returned NotPermitted in tests |
| `0x039A` | 922    | `Ramp` (fixture-side)      | ❌ Not validated; v1 uses host-driven ramp |

For v1, only the attributes above are required by the controller. Additional fixture attributes can be added later from fresh captures and hardware validation.

---

## 7. Validated commands

The following light states have been verified end-to-end on hardware:

| Command           | Channel values |
|-------------------|----------------|
| Lights On         | All channels = 1000 |
| Lights Off        | All channels = 0 |
| Low brightness    | Brightness=20, Moonlight=20, rest 0 (sub-visible in test room) |
| White moonlight   | Brightness=1000, Moonlight=50, rest 0 |
| Host-driven ramp  | Repeated LiveDemoScene writes stepping intensities, 11-step 0→1000→0 sequence verified |

Blue moonlight, color presets, and group commands are constructed from these primitives by the host application — the fixture has no notion of "groups" or "scenes" at the BLE layer beyond a single live scene.

---

## 8. Error responses

A failed SetC2Attr returns a confirm frame whose payload starts with a non-zero status byte:

| Status | Meaning observed |
|--------|------------------|
| `0x00` | Success |
| `0x03` | `InvalidElement` — seen when `count` > actual supported elements (e.g. SupportedColorChannels `count=32` returned this) |

Other status values from the FSCI status enum may appear; not all are documented from observation yet. Treat any non-zero status as a command failure and surface the raw byte through `event_log` for later analysis.

---

## 9. Implementation pointers

| Concern | Module |
|---------|--------|
| CRC + frame build/parse | `components/fsci_codec/` |
| LiveDemoScene + SupportedColorChannels payloads | `components/hydra64hd_protocol/` |
| Channel order + name lookup | `components/channel_model/` |
| Preset expansions | `components/preset_engine/` |
| RX reassembly buffer | `fsci_reassembly_*` in `fsci_codec.h` |

All of the above are pure C and compile on a Linux host for unit testing. See [`host_tests/`](../host_tests/) for the test fixtures and golden vectors.
