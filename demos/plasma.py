# Retro sine plasma demo using Tab5 MicroPython gfx API.
# Usage: python /plasma.py [wait_ms] [frames] [cell_px]

try:
    args = argv
except NameError:
    import sys
    args = sys.argv

import math

def argi(i, d):
    try:
        return int(args[i])
    except Exception:
        return d

def pause(ms):
    if ms <= 0:
        return
    try:
        import time
        if hasattr(time, "sleep_ms"):
            time.sleep_ms(ms)
        else:
            time.sleep(ms / 1000.0)
    except Exception:
        pass

def imax(a, b):
    return a if a > b else b

def rgb(r, g, b):
    return (int(r) << 16) | (int(g) << 8) | int(b)

def color(v):
    r = int((math.sin(v) + 1.0) * 127.5)
    g = int((math.sin(v + 2.094) + 1.0) * 127.5)
    b = int((math.sin(v + 4.188) + 1.0) * 127.5)
    return rgb(r, g, b)

def plasma_value(x, y, t):
    cx = x - 0.5
    cy = y - 0.5
    d = math.sqrt(cx * cx + cy * cy)
    return (
        math.sin(x * 18.0 + t) +
        math.sin(y * 14.0 + t * 1.3) +
        math.sin((x + y) * 12.0 + t * 0.7) +
        math.sin(d * 38.0 - t * 1.8)
    )

wait = argi(1, 0)
frames = argi(2, 160)
cell = imax(6, argi(3, 12))
w, h = gfx.size()
cols = (w + cell - 1) // cell
rows = (h + cell - 1) // cell

gfx.clear(0)
gfx.text(8, 8, "sine plasma  Ctrl-C/q: stop", 0xffffff)
gfx.present()

for frame in range(frames):
    t = frame * 0.16
    for py in range(rows):
        y = py / rows
        for px in range(cols):
            x = px / cols
            gfx.rect(px * cell, py * cell, cell, cell, color(plasma_value(x, y, t)), 1)
    gfx.present()
    pause(wait)
