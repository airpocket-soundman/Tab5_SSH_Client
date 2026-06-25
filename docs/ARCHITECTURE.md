# Tab5 SSH Client Architecture

This project targets M5Stack Tab5 with the Tab5 Keyboard in VSCode + PlatformIO.

## Hardware notes

- Tab5 main controller: ESP32-P4 with 16 MB flash and 32 MB PSRAM.
- Wireless module: ESP32-C6 connected as the wireless coprocessor.
- Tab5 Keyboard: STM32F030C8T6-based keyboard module on Tab5 ExtPort1.
- Tab5 Keyboard I2C details from the official M5Unit-KEYBOARD code:
  - I2C address: `0x6D`
  - SDA: GPIO0
  - SCL: GPIO1
  - INT: GPIO50
  - modes: Normal, HID, Character

## Application flow

1. `SettingsStore` loads `/profiles.json` from LittleFS.
2. `WifiProfiles` tries configured Wi-Fi profiles in order.
3. `SshClient` connects to the selected SSH profile.
4. `Tab5KeyboardInput` reads the Tab5 keyboard and maps input through `KeyboardMapper`.
5. `TerminalBuffer` stores received text and exposes a vertically scrollable viewport.

## Controls

- `Esc`: connect or disconnect the current SSH profile.
- `Ctrl+Up`: scroll terminal buffer upward.
- `Ctrl+Down`: scroll terminal buffer downward.
- Serial monitor input is also accepted as a fallback during hardware bring-up.

## Tailscale

Running a full Tailscale node directly on ESP32-P4 is not implemented here. Tailscale requires userspace networking, WireGuard, DERP/control-plane behavior, persistent node keys, and enough OS integration that is not currently available as a normal Arduino/PlatformIO ESP32 library.

Supported practical path:

- Run a Tailscale subnet router or gateway on a Raspberry Pi, Linux server, or router on the same Wi-Fi network as Tab5.
- Add Tailscale IPs or MagicDNS names to `data/profiles.json`.
- Ensure the Wi-Fi network routes the tailnet prefix or can resolve the MagicDNS name.

## Keyboard layout strategy

The keyboard has its own STM32 firmware and official firmware-flashing examples exist in the M5Stack documentation and `M5Unit-KEYBOARD` repository. For this SSH client, layout customization is done on the Tab5 side first through `KeyboardMapper`, because it is reversible and does not risk making the keyboard hard to recover.

Firmware replacement should be treated as a later hardware task after confirming:

- Bootloader entry method for the Tab5 Keyboard module.
- Official firmware backup or recovery image.
- Exact desired matrix-to-character layout.

