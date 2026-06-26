# M5Burner Publishing

This project can be shared through M5Burner as a user custom firmware.

Official references:

- M5Burner Publish Firmware: https://docs.m5stack.com/en/uiflow/m5burner/publish
- M5Burner Export Firmware: https://docs.m5stack.com/en/uiflow/m5burner/export
- M5Stack firmware repository format: https://github.com/m5stack/M5Stack-Firmware

## Build a Package

Run this from the repository root:

```powershell
.\tools\package_m5burner.ps1 -Version 0.1.0
```

The script builds the firmware and LittleFS image, then creates:

```text
dist/m5burner/Tab5_SSH_Client-<version>/
  README.md
  m5burner.json
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

1. Open M5Burner and sign in with a M5Stack community account.
2. Open `USER CUSTOM`.
3. Select `Publish`.
4. Fill in the fields:

```text
Name: Tab5 SSH Client
Version: 0.1.0
Device Type: Tab5
Github: https://github.com/airpocket-soundman/Tab5_SSH_Client
FirmWare: dist/m5burner/Tab5_SSH_Client-0.1.0.zip
Cover: a screenshot or cover image for the firmware
```

Suggested description:

```text
SSH terminal firmware for M5Stack Tab5 with Tab5 Keyboard. Supports Wi-Fi and
SSH profiles from LittleFS, direct ssh user@host[:port] connections, a scrollable
terminal buffer, US/JP key mapping, USB keyboard input, and serial diagnostics.

Before flashing, edit profiles.json or use the included sample profiles as
placeholders. Do not publish real Wi-Fi passwords or SSH credentials.
```

5. Click `Upload`.
6. After upload, use `Detail` to adjust metadata if needed.
7. Use `Publish` to change the visibility.
8. Use `Share` to get a share code for testing.

## Notes

- M5Burner also has a built-in firmware export flow. The official docs recommend
  that flow for the `FirmWare` field, but the generated zip follows the public
  `m5burner.json` package format used by M5Stack's firmware repository.
- If M5Burner rejects the zip, burn the firmware once with PlatformIO, then use
  M5Burner's `USER CUSTOM > Firmware Exporter` and upload the exported firmware
  file through `USER CUSTOM > Publish`.
- For a first-time listing in the public M5Burner catalog, M5Stack may review the
  firmware. The GitHub repository format can also be submitted to
  `m5stack/M5Stack-Firmware` by adding this repository to `firmware-repo.list`.
