# Tab5 SSH Client

English | [日本語](README.ja.md)

M5Stack Tab5 SSH terminal firmware built with PlatformIO. The project targets a
Tab5 with the Tab5 Keyboard and provides Wi-Fi profile loading, saved SSH
profiles, keyboard input, and a scrollable terminal view.

## Features

- PlatformIO project for M5Stack Tab5 / ESP32-P4.
- Wi-Fi and SSH profile loading from LittleFS JSON.
- Multiple saved SSH profiles with host, port, user, password, and terminal type.
- Direct CLI connection syntax: `ssh user@host[:port] [password]`.
- Interactive SSH shell using `LibSSH-ESP32`.
- Scrollable terminal buffer with basic ANSI escape handling.
- Tab5 Keyboard input through `M5Unit-KEYBOARD`.
- US/JP keyboard layout mapping on the Tab5 side.
- USB keyboard input path for bring-up and testing.
- Serial monitor command API for diagnostics.

## Hardware

- M5Stack Tab5
- Tab5 Keyboard
- USB cable for flashing and serial diagnostics
- Wi-Fi network reachable by the Tab5

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the application flow and
hardware notes.

## Build

Install PlatformIO, open this folder, then build the `tab5` environment.

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

Upload the LittleFS profile data:

```powershell
pio run -t uploadfs
```

## Configuration

Edit `data/profiles.json` before running `uploadfs`.

- `wifi`: Wi-Fi profiles are tried in order.
- `ssh`: saved SSH targets shown in the Tab5 SSH profile list.
- `keyboard.layout`: `us` or `jp`.
- `system.region` and `system.utcOffsetMinutes`: used for local time display.

Example SSH profile:

```json
{
  "name": "linux-box",
  "host": "192.0.2.10",
  "port": 22,
  "user": "demo",
  "password": "change-me",
  "terminal": "xterm-256color"
}
```

Do not commit real Wi-Fi passwords or SSH credentials.

## Usage

1. Edit `data/profiles.json` with at least one Wi-Fi profile and one SSH profile.
2. Upload the firmware with `pio run -t upload`.
3. Upload the profile file with `pio run -t uploadfs`.
4. Reboot the Tab5.
5. Wait until the status line shows a connected Wi-Fi network and an IP address.
6. Open the `SSH` screen, select a profile, and press `CONNECT`.

You can also connect from the terminal CLI:

```text
ssh list
ssh connect 0
```

For a one-off connection without saving a profile:

```text
ssh demo@192.0.2.10:22
```

If a password is not provided in the direct command, the firmware tries to reuse
credentials from a saved profile with the same host/user or host/user/port.

## On-Device Controls

The buttons in the top menu bar switch between the main screens:

- `TERM`: terminal and built-in CLI.
- `WIFI`: saved Wi-Fi profiles, scan, add, edit, and connect.
- `SSH`: saved SSH profiles, add, edit, and connect.
- `FONT`: terminal font and line spacing.
- `CONF`: device, region, time offset, NTP, and keymap settings.
- `CONN` / `DISC`: connect or disconnect from the current terminal screen.

Keyboard shortcuts:

- `Esc`: switch focus between the terminal/content area and the top menu bar.
- `Tab`: move focus within the top menu bar, list screens, and edit/settings fields.
- `Ctrl+Up`: scroll the terminal buffer upward.
- `Ctrl+Down`: scroll the terminal buffer downward.

When an SSH session is active on the terminal screen, `Esc` is sent to the
remote shell/application and does not activate the top menu bar.

Useful built-in CLI commands:

```text
help
status
wifi status
wifi list
ip addr
ssh list
ssh connect <index>
ssh disconnect
time sync
clear
```

## Connecting to Tailscale hosts

This firmware does not run a Tailscale node on the ESP32-P4. To connect to a
tailnet host, put the Tab5 on a network that has a Tailscale gateway, subnet
router, or another SSH relay. Then configure the SSH profile with the reachable
gateway address and port.

## Serial Diagnostics

The firmware exposes a small serial API at `115200` baud:

```text
help
status
wifi status
ssh list
ssh connect [index]
ssh disconnect
term dump
```

`tools/serial_bridge.ps1` can log serial output and send commands from a text
file during bring-up.

## M5Burner

To prepare a M5Burner upload package:

```powershell
.\tools\package_m5burner.ps1 -Version 0.1.0
```

See [docs/M5BURNER.md](docs/M5BURNER.md) for the publishing workflow and
metadata fields.

## Repository Layout

```text
data/       LittleFS profile data
docs/       Architecture notes
include/    Headers
src/        Firmware source
tools/      Helper scripts
```

## Status

This is an experimental firmware project for Tab5 hardware bring-up and mobile
SSH use. Expect to tune Wi-Fi behavior, font sizing, terminal escape handling,
and keyboard mapping for your own setup.
