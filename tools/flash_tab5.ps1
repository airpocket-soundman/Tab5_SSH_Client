param(
    [string]$Port = "COM4",
    [int]$Baud = 1500000,
    [bool]$UseLocalProfiles = $true,
    [switch]$EraseFirst
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$platformio = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\platformio.exe"
if (!(Test-Path $platformio)) {
    throw "PlatformIO executable not found: $platformio"
}

$profilesPath = Join-Path $root "data\profiles.json"
$localProfilesPath = Join-Path $root "data\profiles.local.json"
$backupProfilesPath = Join-Path $env:TEMP "tab5_profiles_backup_$(Get-Date -Format 'yyyyMMddHHmmssfff').json"
$usingLocalProfiles = $false

$env:PYTHONUTF8 = "1"

Push-Location $root
try {
    if ($UseLocalProfiles -and (Test-Path $localProfilesPath)) {
        Copy-Item -LiteralPath $profilesPath -Destination $backupProfilesPath -Force
        Copy-Item -LiteralPath $localProfilesPath -Destination $profilesPath -Force
        $usingLocalProfiles = $true
        Write-Host "Using ignored local profiles: data/profiles.local.json"
    } else {
        Write-Host "Using public profiles: data/profiles.json"
    }

    & $platformio run -e tab5
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & $platformio run -e tab5 -t buildfs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    $build = Join-Path $root ".pio\build\tab5"
    $bootloader = Join-Path $build "bootloader.bin"
    $partitions = Join-Path $build "partitions.bin"
    $firmware = Join-Path $build "firmware.bin"
    $littlefs = Join-Path $build "littlefs.bin"

    if ($EraseFirst) {
        & $platformio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32p4 --port $Port --baud $Baud erase_flash
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }

    & $platformio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32p4 --port $Port --baud $Baud write_flash `
        0x2000 $bootloader `
        0x8000 $partitions `
        0x10000 $firmware `
        0x410000 $littlefs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host "Flashed Tab5 on $Port"
} finally {
    if ($usingLocalProfiles -and (Test-Path $backupProfilesPath)) {
        Copy-Item -LiteralPath $backupProfilesPath -Destination $profilesPath -Force
        Remove-Item -LiteralPath $backupProfilesPath -Force
        Write-Host "Restored public data/profiles.json"
    }
    Pop-Location
}
