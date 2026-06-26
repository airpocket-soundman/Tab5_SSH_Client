# Graphical center-out starfield demo using Tab5 MicroPython gfx API.
# Usage: python /starfield.py [wait_ms] [frames] [speed]

try:
    args = argv
except NameError:
    import sys
    args = sys.argv

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

def rgb(r, g, b):
    return (int(r) << 16) | (int(g) << 8) | int(b)

def lcg(seed):
    return (seed * 73 + 41) % 9973

def make_stars(count):
    out = []
    seed = 137
    for _ in range(count):
        seed = lcg(seed)
        sx = ((seed % 2001) - 1000) / 1000.0
        seed = lcg(seed)
        sy = ((seed % 2001) - 1000) / 1000.0
        if -0.05 < sx < 0.05:
            sx += 0.16
        if -0.05 < sy < 0.05:
            sy -= 0.16
        seed = lcg(seed)
        z = 12 + (seed % 130)
        out.append([sx, sy, z])
    return out

def project(s, w, h):
    scale = 70.0 / s[2]
    return int(w / 2 + s[0] * w * scale), int(h / 2 + s[1] * h * scale)

def star_color(z):
    if z < 18:
        return rgb(255, 255, 255)
    if z < 34:
        return rgb(170, 220, 255)
    if z < 60:
        return rgb(70, 130, 230)
    return rgb(20, 40, 90)

wait = argi(1, 0)
frames = argi(2, 30)
speed = argi(3, 5)
w, h = gfx.size()
stars = make_stars(160)

for _ in range(frames):
    gfx.clear(0)
    for s in stars:
        ox, oy = project(s, w, h)
        s[2] -= speed
        if s[2] <= 8:
            s[2] += 130
        x, y = project(s, w, h)
        c = star_color(s[2])
        gfx.line(ox, oy, x, y, c)
        size = 1 if s[2] > 30 else 2
        gfx.rect(x - size, y - size, size * 2 + 1, size * 2 + 1, c, 1)
    gfx.present()
    pause(wait)
