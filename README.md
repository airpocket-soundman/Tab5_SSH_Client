# Tab5 SSH Client

English | [日本語](README.ja.md)

M5Stack Tab5 firmware that turns the Tab5 into a portable SSH terminal. It is
built with PlatformIO and targets the Tab5 with the Tab5 Keyboard, microSD, and
Wi-Fi.

## Features

- PlatformIO project for M5Stack Tab5 / ESP32-P4.
- On-device Wi-Fi and SSH profile management persisted to flash.
- Direct SSH command: `ssh user@host[:port] [password]`.
- Interactive SSH shell using `LibSSH-ESP32`.
- ANSI/VT-style terminal handling for shells, `vim`, `nano`, `sl`, and similar apps.
- Scrollback buffer and keyboard-driven command editing/history.
- Tab5 Keyboard, USB keyboard, and BLE keyboard configuration paths.
- US/JP keyboard layout mapping on the Tab5 side.
- SCP-style file transfer between SSH hosts and the Tab5 microSD card.
- Linux-like local CLI for SD files, Wi-Fi, SSH/SCP, diagnostics, and Python.
- Embedded MicroPython runner for REPL, `python -c`, and SD-card scripts.
- MicroPython graphics API backed by the firmware M5GFX sprite.
- SD-card demos: progressive Mandelbrot, sine plasma, wireframe hat, Life,
  starfield, and maze.

## Documentation

- [Command list](docs/COMMANDS.md)
- [コマンド一覧](docs/COMMANDS.ja.md)
- [Python and graphics](docs/PYTHON.md)
- [Demo scripts](docs/DEMOS.md)
- [デモスクリプト](docs/DEMOS.ja.md)
- [Third-party licenses](THIRD_PARTY_LICENSES.md)

## Hardware

- M5Stack Tab5
- Tab5 Keyboard
- microSD card
- USB cable for flashing and serial diagnostics
- Wi-Fi network reachable by the Tab5

## Build

Install PlatformIO, open this folder, then build the `tab5` environment.

```powershell
pio run -e tab5
```

On Windows consoles, use UTF-8 if PlatformIO output fails with encoding errors.

```powershell
$env:PYTHONUTF8='1'; pio run -e tab5
```

Upload firmware:

```powershell
pio run -e tab5 -t upload
```

For a complete device flash, including bootloader, partition table, firmware,
and LittleFS profiles, use:

```powershell
.\tools\flash_tab5.ps1 -Port COM4
```

If `data/profiles.local.json` exists, this command temporarily uses that ignored
local profile file for the LittleFS image, then restores the public
`data/profiles.json`. For a clean public image for M5Burner export:

```powershell
.\tools\flash_tab5.ps1 -Port COM4 -UseLocalProfiles:$false -EraseFirst
```

## Configuration

Profiles can be edited on the Tab5 UI and are persisted to flash.

- `WIFI`: saved Wi-Fi profiles, scan, add, edit, connect, Wi-Fi on/off.
- `SSH`: saved SSH profiles, add, edit, connect.
- `FONT`: terminal font and line spacing.
- `CONF`: device name, region, UTC offset, NTP, keymap, and related settings.

Do not commit real Wi-Fi passwords or SSH credentials.

## Usage

1. Upload the firmware.
2. Reboot the Tab5.
3. Configure Wi-Fi from the `WIFI` screen.
4. Configure SSH from the `SSH` screen.
5. Select a profile and press `CONNECT`.

You can also connect from the local CLI:

```text
ssh list
ssh connect 0
ssh demo@192.0.2.10:22
```

If a direct SSH command omits the password, the firmware tries to reuse
credentials from a saved profile with the same host/user or host/user/port.

## On-Device Controls

- `Esc`: switch focus between the terminal/content area and the top menu bar.
- `Tab`: move focus in menus, lists, and edit fields.
- Arrow keys: move focus in menus/settings, or send cursor movement to terminal apps.
- `Ctrl+Up` / `Ctrl+Down`: scroll the terminal buffer.

When an SSH session is active on the terminal screen, `Esc` is sent to the
remote application so `vim` can leave insert mode.

## Built-In CLI

The built-in CLI is Linux-like, not a full POSIX shell. There are no pipelines,
redirection, shell expansion, or background jobs.

```text
help
man <command>
status
wifi status
wifi off
wifi on
ssh list
ssh connect 0
ssh user@host[:port] [password]
ls /
ls -lah /
cat /life.txt
df
mkdir /scripts
rmdir /scripts
scp get /home/demo/test.py /test.py 0
scp put /test.py /home/demo/test.py 0
python /life.py
python /mandel.py 0 1 8 -1
python /plasma.py 0 160 16
```

Normal `ls` uses multi-column output; `ls -l` uses one file per line.

## MicroPython And Graphics

The local CLI can start the embedded REPL or run `.py` files from microSD:

```text
python
python -c print('hello')
python /life.py
```

Scripts receive `argv` and a global `gfx` object. Drawing commands render into
the firmware sprite, and `gfx.present()` pushes that sprite to the display.
Graphics scripts can be interrupted at `gfx.present()` with `Ctrl-C` or `q`.

See [docs/PYTHON.md](docs/PYTHON.md) for the API and
[docs/DEMOS.md](docs/DEMOS.md) for bundled demo scripts.

## Tailscale Hosts

This firmware does not run a Tailscale node on the ESP32-P4. To connect to a
tailnet host, put the Tab5 on a network that has a Tailscale gateway, subnet
router, tethered Tailscale device, or SSH relay, then configure the SSH profile
with the reachable address and port.

## Serial Diagnostics

The firmware exposes a serial API at `115200` baud for diagnostics:

```text
help
status
sd ls /
wifi status
ssh list
ssh connect [index]
ssh disconnect
term dump
python /life.py 0 5
```

When opening the serial port from host tools, avoid unnecessary DTR/RTS
transitions because they may reset the board.

## Repository Layout

```text
data/       LittleFS profile data
demos/      SD-card Python demo scripts and text help
docs/       Documentation
include/    Headers
lib/        Embedded MicroPython and local libraries
src/        Firmware source
tools/      Helper scripts
```

## Status

This is experimental firmware for Tab5 hardware bring-up and mobile SSH use.
Expect to tune Wi-Fi behavior, terminal escape handling, performance, fonts,
and keyboard mappings for your own setup.

## License

Project-owned source code is distributed under the [MIT License](LICENSE).
Third-party components remain under their own licenses; see
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md). In particular, SSH/SCP
support uses `LibSSH-ESP32` / `libssh`, which is LGPL-2.1-or-later.
