param(
    [string]$Version = "1.0.0",
    [string]$Author = "airpocket-soundman",
    [string]$Repository = "https://github.com/airpocket-soundman/Tab5_SSH_Client",
    [string]$Device = "Tab5"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$platformio = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\platformio.exe"
if (!(Test-Path $platformio)) {
    throw "PlatformIO executable not found: $platformio"
}

$env:PYTHONUTF8 = "1"
Push-Location $root
try {
    & $platformio run
    & $platformio run -t buildfs

    $build = Join-Path $root ".pio\build\tab5"
    $packageName = "Tab5_SSH_Client-$Version"
    $distRoot = Join-Path $root "dist\m5burner"
    $packageRoot = Join-Path $distRoot $packageName
    $firmwareDir = Join-Path $packageRoot "firmware"
    $zipPath = Join-Path $distRoot "$packageName.zip"

    if (Test-Path $packageRoot) {
        Remove-Item -LiteralPath $packageRoot -Recurse -Force
    }
    if (Test-Path $zipPath) {
        Remove-Item -LiteralPath $zipPath -Force
    }

    New-Item -ItemType Directory -Force -Path $firmwareDir | Out-Null

    Copy-Item -LiteralPath (Join-Path $build "bootloader.bin") -Destination (Join-Path $firmwareDir "bootloader_0x2000.bin")
    Copy-Item -LiteralPath (Join-Path $build "partitions.bin") -Destination (Join-Path $firmwareDir "partitions_0x8000.bin")
    Copy-Item -LiteralPath (Join-Path $build "firmware.bin") -Destination (Join-Path $firmwareDir "firmware_0x10000.bin")
    Copy-Item -LiteralPath (Join-Path $build "littlefs.bin") -Destination (Join-Path $firmwareDir "littlefs_0x410000.bin")
    Copy-Item -LiteralPath (Join-Path $root "README.md") -Destination (Join-Path $packageRoot "README.md")

    $metadata = [ordered]@{
        name = "Tab5 SSH Client"
        description = "SSH terminal firmware for M5Stack Tab5 with Tab5 Keyboard. Supports Wi-Fi and SSH profiles, direct ssh user@host[:port] connections, a scrollable terminal buffer, US/JP key mapping, USB keyboard input, and serial diagnostics."
        keywords = "M5Stack, Tab5, SSH, terminal, ESP32-P4, Arduino, PlatformIO"
        author = $Author
        repository = $Repository
        firmware_category = [ordered]@{
            path = "firmware"
            device = @($Device)
            default_baud = 1500000
        }
        version = $Version
        framework = "Arduino"
    }

    $metadataJson = $metadata | ConvertTo-Json -Depth 8
    $metadataJson | Set-Content -Path (Join-Path $packageRoot "m5burner.json") -Encoding UTF8
    $metadataJson | Set-Content -Path (Join-Path $packageRoot "manifest.json") -Encoding UTF8

    Compress-Archive -Path (Join-Path $packageRoot "*") -DestinationPath $zipPath -Force

    Write-Host "M5Burner package created:"
    Write-Host "  $packageRoot"
    Write-Host "  $zipPath"
} finally {
    Pop-Location
}
