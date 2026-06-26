# Tab5 Python Mode

This firmware includes a small MicroPython-compatible command subset inside the
Tab5 SSH client. It is not the full upstream MicroPython VM yet; it is a stable
in-app runner intended for REPL, SD scripts, and simple GPIO work without
replacing the SSH terminal firmware.

## Commands

```text
python
python <sd.py>
python -c <statement>
python --reset
```

Serial API commands:

```text
python -c <statement>
python <sd.py>
python --reset
sd write <path> <text>
sd append <path> <text>
sd cat <path>
```

## Supported Python Subset

```python
print("hello", 123)
from machine import Pin
from time import sleep_ms

led = Pin(2, Pin.OUT)
led.on()
sleep_ms(100)
led.off()

for i in range(3):
    print("blink", i)
    led.value(1)
    sleep_ms(100)
    led.value(0)
    sleep_ms(100)
```

Additional helpers:

```python
pin(2, 1)
digitalWrite(2, 0)
digitalRead(2)
blink(2, 3, 100)
```

The command surface is intentionally compatible with a future full MicroPython
backend, so `python` and `python <sd.py>` can stay stable if the runner is later
replaced by an embedded VM or a dedicated MicroPython firmware mode.
