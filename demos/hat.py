# Ten-gallon-hat / sombrero wireframe graphical demo using Tab5 gfx API.
# Usage: python /hat.py [wait_ms] [frames] [hold_ms]

try:
    args = argv
except NameError:
    import sys
    args = sys.argv

try:
    import math
except ImportError:
    math = None

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

def surface(x, y, phase):
    r = (x * x + y * y) ** 0.5
    if r < 0.001:
        return 2.2
    return math.sin(r * 2.7 + phase) * 2.2 / r

def project(x, y, z, w, h):
    px = int(w * 0.50 + (x - y) * w * 0.045)
    py = int(h * 0.60 + (x + y) * h * 0.022 - z * h * 0.075)
    return px, py

wait = argi(1, 0)
frames = argi(2, 1)
hold_ms = argi(3, 60000)
w, h = gfx.size()
x_count = 76
y_count = 54

for f in range(frames):
    phase = f * 0.28
    gfx.clear(0)
    gfx.text(8, 8, "hat wireframe rendering...", 0xffffff)
    gfx.present()
    for j in range(y_count):
        y = -6.0 + j * (12.0 / (y_count - 1))
        last = None
        shade = 130 + j * 100 // y_count
        color = (shade << 16) | (shade << 8) | shade
        for i in range(x_count):
            x = -6.4 + i * (12.8 / (x_count - 1))
            z = surface(x, y, phase)
            p = project(x, y, z, w, h)
            if last is not None:
                gfx.line(last[0], last[1], p[0], p[1], color)
            last = p
        gfx.present()
        pause(wait)
    gfx.present()

elapsed = 0
while hold_ms < 0 or elapsed < hold_ms:
    gfx.present()
    pause(50)
    elapsed += 50
