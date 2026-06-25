# Tab5 SSH Client

M5Stack Tab5 + Tab5 Keyboard SSH client project for VSCode + PlatformIO.

## Build

Install PlatformIO in VSCode, open this folder, then build the `tab5` environment.

```powershell
pio run
```

On Japanese Windows consoles, run PlatformIO with UTF-8 enabled if package
output fails with `UnicodeEncodeError`.

```powershell
$env:PYTHONUTF8='1'; pio run
```

Upload firmware:

```powershell
pio run -t upload
```

Upload profiles to LittleFS:

```powershell
pio run -t uploadfs
```

## Configure profiles

Edit `data/profiles.json` before `uploadfs`.

- `wifi`: multiple Wi-Fi profiles are tried in order.
- `ssh`: multiple SSH targets can be stored.
- Tailscale servers can be listed by Tailscale IP or MagicDNS name when the local network has a Tailscale subnet router or gateway.

## Current status

Implemented:

- PlatformIO project for Tab5/ESP32-P4 using `pioarduino/platform-espressif32`.
- Wi-Fi and SSH profile loading from LittleFS JSON.
- Scrollable terminal text buffer.
- Tab5 Keyboard input path using M5Unit-KEYBOARD Character/HID events.
- Tab5-side keyboard layout translation hook.
- Interactive SSH shell wrapper using `LibSSH-ESP32`.

Known hardware verification needed:

- Confirm Arduino Wi-Fi initialization on the installed Tab5 firmware/core.
- Confirm `LibSSH-ESP32` builds and runs on ESP32-P4 with the selected pioarduino release.
- Tune terminal rendering for font size and ANSI escape handling.
