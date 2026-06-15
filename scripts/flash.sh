#!/usr/bin/env bash
set -euo pipefail

PORT="${1:-}"
BAUD="${2:-460800}"

if [[ -z "$PORT" ]]; then
    echo "Usage: $0 <serial-port> [baud]"
    echo "Example: $0 /dev/ttyUSB0"
    echo "Example: $0 /dev/ttyACM0 460800"
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIRMWARE_DIR="${FIRMWARE_DIR:-$SCRIPT_DIR}"

if [[ ! -f "$FIRMWARE_DIR/hydra_ble_controller.bin" ]]; then
    if [[ -f "$SCRIPT_DIR/../build/hydra_ble_controller.bin" ]]; then
        FIRMWARE_DIR="$SCRIPT_DIR/../build"
    else
        echo "Firmware binaries were not found."
        echo "Run from an extracted release zip, or build first with: idf.py build"
        exit 1
    fi
fi

if [[ -f "$FIRMWARE_DIR/partition-table.bin" ]]; then
    PARTITION_BIN="$FIRMWARE_DIR/partition-table.bin"
else
    PARTITION_BIN="$FIRMWARE_DIR/partition_table/partition-table.bin"
fi

BOOTLOADER_BIN="$FIRMWARE_DIR/bootloader.bin"
if [[ ! -f "$BOOTLOADER_BIN" ]]; then
    BOOTLOADER_BIN="$FIRMWARE_DIR/bootloader/bootloader.bin"
fi

OTA_BIN="$FIRMWARE_DIR/ota_data_initial.bin"
APP_BIN="$FIRMWARE_DIR/hydra_ble_controller.bin"

for file in "$BOOTLOADER_BIN" "$PARTITION_BIN" "$OTA_BIN" "$APP_BIN"; do
    if [[ ! -f "$file" ]]; then
        echo "Missing firmware file: $file"
        exit 1
    fi
done

if ! python3 -m esptool version >/dev/null 2>&1; then
    echo "esptool is not installed for python3."
    echo "Install it with: python3 -m pip install esptool"
    exit 1
fi

python3 -m esptool \
    --chip esp32s3 \
    -p "$PORT" \
    -b "$BAUD" \
    --before default_reset \
    --after hard_reset \
    write_flash \
    --flash_mode dio \
    --flash_size 16MB \
    --flash_freq 80m \
    0x0 "$BOOTLOADER_BIN" \
    0x8000 "$PARTITION_BIN" \
    0xf000 "$OTA_BIN" \
    0x20000 "$APP_BIN"
