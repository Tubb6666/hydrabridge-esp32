# HydraBridge ESP32 Quickstart

This guide is for flashing the supported Waveshare ESP32-S3 controller and getting it onto WiFi without compiling the firmware.

The prebuilt release firmware is developed and tested for the [Waveshare ESP32-S3-RS485-CAN](https://www.waveshare.com/wiki/ESP32-S3-RS485-CAN#Onboard_Resources). That board has the expected ESP32-S3, 2.4 GHz WiFi/BLE, 16MB flash, USB-C flashing/debug, wide-range power input, and onboard isolated RS485 interface.

Other ESP32-S3 boards can run HydraBridge ESP32, but they may need source-level or settings changes before use: flash size and partition layout, RS485 transceiver wiring, RS485 GPIO assignments, and board-specific USB/serial flashing behavior. For non-Waveshare boards, build from source unless you have confirmed the release binary matches your board.

## What you need

- Waveshare ESP32-S3-RS485-CAN controller
- Optional: another ESP32-S3 board, if you are prepared to adjust pins, flash layout, and RS485 hardware
- USB-C data cable
- A 2.4 GHz WiFi network
- A computer running Linux, macOS, or Windows
- Python 3.10 or newer

## Install esptool

HydraBridge release packages use Espressif's `esptool` flasher.

Linux or macOS:

```bash
python3 -m pip install --user esptool
```

Windows PowerShell:

```powershell
py -3 -m pip install esptool
```

## Download firmware

1. Open the GitHub Releases page for this repository.
2. Download the latest `hydrabridge-esp32-firmware-*.zip`.
3. Extract the zip file.

The extracted folder contains:

- `hydra_ble_controller.bin`
- `bootloader.bin`
- `partition-table.bin`
- `ota_data_initial.bin`
- `flash.sh`
- `flash.ps1`

## Flash from Linux

Plug in the controller, then find the serial port:

```bash
ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
```

Flash the firmware:

```bash
cd hydrabridge-esp32-firmware-*
./flash.sh /dev/ttyUSB0
```

Use `/dev/ttyACM0` if that is the port your board uses.

## Flash from macOS

Plug in the controller, then find the serial port:

```bash
ls /dev/cu.usb*
```

Flash the firmware:

```bash
cd hydrabridge-esp32-firmware-*
./flash.sh /dev/cu.usbserial-0001
```

Use the actual `/dev/cu.*` device shown by your Mac.

## Flash from Windows

Plug in the controller, then find the COM port in Device Manager.

In PowerShell:

```powershell
cd .\hydrabridge-esp32-firmware-*
.\flash.ps1 COM3
```

Use the COM port shown for your board.

If PowerShell blocks the script, run this once in the same PowerShell window:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
```

## First boot WiFi setup

On first boot HydraBridge starts its setup hotspot because no home WiFi has been saved yet.

1. From a phone or computer, connect to WiFi network `HydraBridge-Setup`.
2. Use password `hydrabridge`.
3. Open `http://192.168.1.10/`.
4. Go to `Settings`.
5. In `WiFi`, enter your home WiFi SSID and password.
6. Keep `Setup hotspot fallback` enabled.
7. Select `Save WiFi`.
8. Reconnect your phone or computer to your home WiFi.
9. Open `http://hydrabridge.local/`, or use the IP address shown by your router.

The setup hotspot will automatically come back if no WiFi is configured or the saved network cannot be reached after repeated retries.

## Basic setup after WiFi

1. Open the HydraBridge web UI.
2. Use `Discovery` to find Hydra® lights.
3. Register each light and give it a friendly name.
4. Create light groups if you want one command to control multiple lights.
5. Use `Profiles` to apply reusable channel mixes.
6. Enable MQTT or RS485 from `Settings` only if you need those integrations.

## Build from source

Developers can build directly with ESP-IDF 5.4 or newer:

```bash
. ~/esp/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

The release zip uses the same flash offsets as `idf.py build`:

- `0x0` bootloader
- `0x8000` partition table
- `0xf000` OTA data
- `0x20000` HydraBridge app
