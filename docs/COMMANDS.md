# Command List

[日本語](COMMANDS.ja.md)

The Tab5 built-in CLI provides a practical Linux-like command set for terminal
operation, SD card files, SSH/SCP, Wi-Fi control, MicroPython, and diagnostics.
It is not a full POSIX shell: pipelines, redirection, shell expansion, job
control, and arbitrary external commands are not implemented.

Use `help` on the Tab5 for a short list and `man <command>` for examples of
built-in commands. Demo scripts have `.txt` help files on SD, such as
`cat /mandel.txt`.

## General

| Command | Description | Example |
| --- | --- | --- |
| `help` | Show a short command list. | `help` |
| `man <command>` | Show help for a built-in command. | `man scp` |
| `clear` | Clear the Tab5 CLI screen buffer. | `clear` |
| `status` | Show device, Wi-Fi, SSH, keyboard, BLE, and SD state. | `status` |
| `history` | Show local command history. | `history` |
| `echo <text>` | Print text. | `echo hello` |
| `date` | Show local time after NTP sync. | `date` |
| `uptime` | Show time since firmware boot. | `uptime` |
| `time sync` | Request NTP time synchronization. | `time sync` |
| `uname [-a]` | Show firmware/platform information. | `uname -a` |
| `whoami` | Show the configured device name. | `whoami` |
| `hostname` | Show the configured device name. | `hostname` |

## Wi-Fi And Network

| Command | Description | Example |
| --- | --- | --- |
| `wifi status` | Show Wi-Fi runtime state, IP, and SSID. | `wifi status` |
| `wifi list` | List saved Wi-Fi profiles. | `wifi list` |
| `wifi off` | Turn off Wi-Fi and stop reconnect attempts. | `wifi off` |
| `wifi on` | Turn Wi-Fi back on and reconnect. | `wifi on` |
| `ip addr` | Show the Tab5 network address. | `ip addr` |
| `ip a` | Alias of `ip addr`. | `ip a` |
| `ifconfig` | Network status display. | `ifconfig` |

## SSH

| Command | Description | Example |
| --- | --- | --- |
| `ssh list` | List saved SSH profiles. | `ssh list` |
| `ssh connect <index>` | Connect to a saved SSH profile. | `ssh connect 0` |
| `ssh disconnect` | Disconnect the active SSH session. | `ssh disconnect` |
| `ssh user@host[:port] [password]` | Connect without saving a profile. | `ssh airpocket@192.168.50.7` |

If the direct SSH command omits the password, the firmware tries to reuse
credentials from a saved profile with the same host/user or host/user/port.

## SCP File Transfer

`scp get` copies from the SSH server to Tab5 microSD. `scp put` copies from the
Tab5 microSD to the SSH server.

| Command | Description | Example |
| --- | --- | --- |
| `scp get <remote> <sd-local> [profile]` | Download from profile target. | `scp get /home/airpocket/test.py /test.py 0` |
| `scp put <sd-local> <remote> [profile]` | Upload to profile target. | `scp put /test.py /home/airpocket/test.py 0` |
| `scp get user@host:/remote <sd-local> [password]` | Download using direct endpoint. | `scp get airpocket@192.168.50.7:/home/airpocket/test.py /test.py` |
| `scp put <sd-local> user@host:/remote [password]` | Upload using direct endpoint. | `scp put /test.py airpocket@192.168.50.7:/home/airpocket/test.py` |

Useful checks after transfer:

```text
ls -l /
cat /test.py
python /test.py
```

## SD Filesystem

These commands operate on the Tab5 microSD card. Paths are normalized relative
to the current SD working directory.

| Command | Description | Example |
| --- | --- | --- |
| `sd status` | Show SD mount state and capacity. | `sd status` |
| `df` | Show SD size, used, available, and mount point. | `df` |
| `sd df` | Same as `df`. | `sd df` |
| `pwd` | Show current SD directory. | `pwd` |
| `cd <path>` | Change current SD directory. | `cd /scripts` |
| `ls [-lah] [path]` | List files. | `ls /` |
| `dir [-lah] [path]` | Alias-like listing command. | `dir /` |
| `cat <path>` | Print a file. | `cat /life.txt` |
| `sd write <path> <text>` | Write one line, replacing the file. | `sd write /hello.py print(123)` |
| `sd append <path> <text>` | Append one line. | `sd append /notes.txt more text` |
| `mkdir <path>` | Create a directory. | `mkdir /scripts` |
| `sd mkdir <path>` | Create a directory. | `sd mkdir /logs` |
| `rmdir <path>` | Remove an empty directory. | `rmdir /scripts` |
| `sd rmdir <path>` | Remove an empty directory. | `sd rmdir /logs` |
| `sd rm <path>` | Remove a file. | `sd rm /old.py` |
| `chmod <mode> <path>` | Set virtual permission bits. | `chmod 644 /test.py` |
| `sd chmod <mode> <path>` | Set virtual permission bits. | `sd chmod 755 /scripts` |

`ls` options:

- `-l`: long format, one file per line.
- `-a`: show dotfiles such as `.tab5perms`.
- `-h`: human-readable sizes with `-l`.

Normal `ls` prints names in multiple columns. `ls -l` prints mode, owner, size,
time, and name.

Permissions are virtual metadata stored on the SD card. FAT/exFAT does not
natively persist Unix permission bits.

## MicroPython

| Command | Description | Example |
| --- | --- | --- |
| `python` | Start the embedded MicroPython REPL. | `python` |
| `python <sd.py> [args...]` | Run a Python script from SD. | `python /life.py` |
| `python -c <statement>` | Run one statement. | `python -c print('hello')` |
| `python --reset` | Reset the embedded Python VM. | `python --reset` |

Script arguments are exposed as `argv`; the firmware also tries to set
`sys.argv` when supported.

Scripts receive a global `gfx` object for M5GFX sprite drawing. See
[PYTHON.md](PYTHON.md) for the graphics API and [DEMOS.md](DEMOS.md) for demo
scripts.

On the Tab5, demo help files can be read with `cat`:

```text
cat /mandel.txt
cat /plasma.txt
cat /life.txt
```

## Bluetooth Keyboard

| Command | Description | Example |
| --- | --- | --- |
| `ble status` | Show BLE keyboard state. | `ble status` |
| `ble enable` | Enable BLE keyboard support. | `ble enable` |
| `ble disable` | Disable BLE keyboard support. | `ble disable` |
| `ble scan` | Scan for BLE keyboard candidates. | `ble scan` |
| `ble pair <index>` | Pair with a scanned keyboard. | `ble pair 0` |
| `ble forget` | Forget stored BLE keyboard pairing state. | `ble forget` |

## Notes

- Use `Tab` for SD path completion in the local Tab5 CLI where supported.
- Use `man <command>` for built-in CLI examples.
- Use `.txt` files and `cat` for demo-specific help.
- Serial API commands overlap with the CLI but are intended for diagnostics.
