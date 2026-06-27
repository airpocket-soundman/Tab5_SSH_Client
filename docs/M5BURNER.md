# M5Burner Publishing

This project can be shared through M5Burner as a user custom firmware.

Official references:

- M5Burner Publish Firmware: https://docs.m5stack.com/en/uiflow/m5burner/publish
- M5Burner Export Firmware: https://docs.m5stack.com/en/uiflow/m5burner/export
- M5Stack firmware repository format: https://github.com/m5stack/M5Stack-Firmware

## Build a Package

Run this from the repository root:

```powershell
.\tools\package_m5burner.ps1 -Version 1.0.0
```

The script builds the firmware and LittleFS image, then creates:

```text
dist/m5burner/Tab5_SSH_Client-<version>/
  README.md
  m5burner.json
  manifest.json
  firmware/
    bootloader_0x2000.bin
    partitions_0x8000.bin
    firmware_0x10000.bin
    littlefs_0x410000.bin

dist/m5burner/Tab5_SSH_Client-<version>.zip
```

The flash offsets come from the ESP32-P4 PlatformIO/Arduino build:

```text
0x2000   bootloader
0x8000   partition table
0x10000  application
0x410000 LittleFS data
```

## Publish from M5Burner

For the public M5Burner flow, prefer exporting a clean image from the device.
Flash the Tab5 with the public dummy profiles first:

```powershell
.\tools\flash_tab5.ps1 -Port COM4 -UseLocalProfiles:$false -EraseFirst
```

Then open M5Burner and use the firmware export flow. The exported `.bin` is the
file to upload in the `FirmWare` field.

## Release History

- `1.0.0`: Released to M5Burner as the first public Tab5 SSH Client firmware.
  The release uses a clean LittleFS image with dummy Wi-Fi/SSH profiles only.
  Cover image: `assets/m5burner-cover.png`.

1. Open M5Burner and sign in with a M5Stack community account.
2. Open `USER CUSTOM`.
3. Select `Publish`.
4. Fill in the fields:

```text
Name: Tab5 SSH Client
Version: 1.0.0
Device Type: Tab5
Github: https://github.com/airpocket-soundman/Tab5_SSH_Client
FirmWare: dist/m5burner/Tab5_SSH_Client-1.0.0.zip
Cover: assets/m5burner-cover.png
```

Suggested description:

```text
Portable SSH terminal firmware for M5Stack Tab5 with Tab5 Keyboard. Includes
Wi-Fi and SSH profile management, Tab5sh Linux-like shell, adjustable terminal
fonts, microSD filesystem commands, SCP file transfer, embedded MicroPython,
and an M5GFX-backed graphics API for Python demos.
```

5. Click `Upload`.
6. After upload, use `Detail` to adjust metadata if needed.
7. Use `Publish` to change the visibility.
8. Use `Share` to get a share code for testing.

## Notes

- M5Burner also has a built-in firmware export flow. The official docs recommend
  that flow for the `FirmWare` field. The generated zip includes both
  `m5burner.json`, used by M5Stack's firmware repository format, and
  `manifest.json`, accepted by newer M5Burner upload flows.
- If M5Burner rejects the zip, burn the firmware once with PlatformIO, then use
  M5Burner's `USER CUSTOM > Firmware Exporter` and upload the exported firmware
  file through `USER CUSTOM > Publish`.
- For local development, `.\tools\flash_tab5.ps1 -Port COM4` can write the
  ignored `data/profiles.local.json` into LittleFS without committing secrets.
- For a first-time listing in the public M5Burner catalog, M5Stack may review the
  firmware. The GitHub repository format can also be submitted to
  `m5stack/M5Stack-Firmware` by adding this repository to `firmware-repo.list`.
