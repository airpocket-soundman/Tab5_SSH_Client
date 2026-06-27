# Third-Party Licenses

Tab5 SSH Client is distributed under the MIT License for the project-owned
source code. It also uses third-party libraries, frameworks, fonts, and tools.
Those components remain under their own licenses.

This document is a practical summary for firmware distribution. The license
files in each upstream project are authoritative.

## Direct PlatformIO Dependencies

| Component | Purpose | License | Notes |
| --- | --- | --- | --- |
| M5Unified | M5Stack device abstraction | MIT | PlatformIO dependency: `m5stack/M5Unified` |
| M5GFX | Display and graphics | MIT, with bundled font/component notices | PlatformIO dependency: `m5stack/M5GFX` |
| M5Unit-KEYBOARD | Tab5/CardKB keyboard support | MIT | PlatformIO dependency: `m5stack/M5Unit-KEYBOARD` |
| ArduinoJson | JSON profile/config parsing | MIT | PlatformIO dependency: `bblanchon/ArduinoJson` |
| LibSSH-ESP32 | SSH and SCP client support | LGPL-2.1-or-later, based on libssh | PlatformIO dependency: `ewpa/LibSSH-ESP32` |

## Embedded MicroPython

The firmware includes a local embedded MicroPython runtime under
`lib/micropython_embed`. MicroPython source files are distributed under the
MIT License. Copyright notices in the original source files must be preserved.

## LibSSH-ESP32 / libssh Notice

LibSSH-ESP32 is an ESP32/Arduino port based on libssh. The bundled libssh
source is licensed under the GNU Lesser General Public License, version 2.1 or
later.

The Tab5 SSH Client firmware links against LibSSH-ESP32 to provide SSH and SCP
features. The project-owned application code is MIT-licensed, but the LGPL
terms still apply to the LibSSH-ESP32/libssh portion when distributing firmware
binaries.

For binary firmware distribution, keep at least the following available:

- This source repository and build instructions.
- The LibSSH-ESP32/libssh license text and copyright notices.
- A way for recipients to obtain the corresponding source for the LGPL-covered
  components used to build the firmware.

## M5GFX Font And Component Notices

M5GFX includes and references additional font/component licenses, including
LovyanGFX, Adafruit GFX fonts, TFT_eSPI fonts, DejaVu fonts, and IPA fonts.
When redistributing firmware or source bundles, preserve the upstream M5GFX
license and font notices.

## ESP32 / Arduino Platform

The firmware is built with the Arduino framework for ESP32-P4 through
PlatformIO. The ESP32 Arduino core and ESP-IDF components have their own
licenses, commonly permissive licenses such as Apache-2.0, BSD, and MIT.
Those upstream notices should be preserved when redistributing complete source
or toolchain-derived bundles.

## Project License Scope

The MIT License in this repository applies to the project-owned files unless a
file explicitly says otherwise. It does not relicense third-party libraries,
frameworks, fonts, or generated files that carry their own license notices.
