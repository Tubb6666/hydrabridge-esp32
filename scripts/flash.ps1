param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Port,

    [Parameter(Mandatory = $false, Position = 1)]
    [int]$Baud = 460800
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$FirmwareDir = if ($env:FIRMWARE_DIR) { $env:FIRMWARE_DIR } else { $ScriptDir }

if (-not (Test-Path (Join-Path $FirmwareDir "hydra_ble_controller.bin"))) {
    $RepoBuild = Join-Path (Split-Path -Parent $ScriptDir) "build"
    if (Test-Path (Join-Path $RepoBuild "hydra_ble_controller.bin")) {
        $FirmwareDir = $RepoBuild
    } else {
        Write-Error "Firmware binaries were not found. Run from an extracted release zip, or build first with: idf.py build"
    }
}

$BootloaderBin = Join-Path $FirmwareDir "bootloader.bin"
if (-not (Test-Path $BootloaderBin)) {
    $BootloaderBin = Join-Path $FirmwareDir "bootloader\bootloader.bin"
}

$PartitionBin = Join-Path $FirmwareDir "partition-table.bin"
if (-not (Test-Path $PartitionBin)) {
    $PartitionBin = Join-Path $FirmwareDir "partition_table\partition-table.bin"
}

$OtaBin = Join-Path $FirmwareDir "ota_data_initial.bin"
$AppBin = Join-Path $FirmwareDir "hydra_ble_controller.bin"

foreach ($File in @($BootloaderBin, $PartitionBin, $OtaBin, $AppBin)) {
    if (-not (Test-Path $File)) {
        Write-Error "Missing firmware file: $File"
    }
}

$Python = "py"
try {
    & $Python -3 -m esptool version *> $null
} catch {
    $Python = "python"
    try {
        & $Python -m esptool version *> $null
    } catch {
        Write-Error "esptool is not installed. Install it with: py -3 -m pip install esptool"
    }
}

if ($Python -eq "py") {
    & $Python -3 -m esptool `
        --chip esp32s3 `
        -p $Port `
        -b $Baud `
        --before default_reset `
        --after hard_reset `
        write_flash `
        --flash_mode dio `
        --flash_size 16MB `
        --flash_freq 80m `
        0x0 $BootloaderBin `
        0x8000 $PartitionBin `
        0xf000 $OtaBin `
        0x20000 $AppBin
} else {
    & $Python -m esptool `
        --chip esp32s3 `
        -p $Port `
        -b $Baud `
        --before default_reset `
        --after hard_reset `
        write_flash `
        --flash_mode dio `
        --flash_size 16MB `
        --flash_freq 80m `
        0x0 $BootloaderBin `
        0x8000 $PartitionBin `
        0xf000 $OtaBin `
        0x20000 $AppBin
}
