# Mandelbrot graphical demo using Tab5 MicroPython gfx API.
# Usage: python /mandel.py [wait_ms] [frames] [final_cell_px] [hold_ms]

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

def imax(a, b):
    return a if a > b else b

def imin(a, b):
    return a if a < b else b

def palette(i, limit):
    if i >= limit:
        return 0
    t = i * 255 // limit
    if t < 64:
        return rgb(0, t * 2, 80 + t * 2)
    if t < 128:
        u = t - 64
        return rgb(0, 128 + u * 2, 255)
    if t < 192:
        u = t - 128
        return rgb(u * 4, 255, 255 - u * 3)
    u = t - 192
    return rgb(255, 255 - u * 3, imax(0, 64 - u))

def mandel(cx, cy, limit):
    x = 0.0
    y = 0.0
    i = 0
    while x * x + y * y <= 4.0 and i < limit:
        x, y = x * x - y * y + cx, 2.0 * x * y + cy
        i += 1
    return i

wait = argi(1, 0)
frames = argi(2, 1)
w, h = gfx.size()
final_cell = imax(1, argi(3, 10))
hold_ms = argi(4, 60000)
limit = 48

def build_passes(final_cell):
    passes = []
    cell = 80
    while cell > final_cell:
        passes.append(cell)
        next_cell = cell // 2
        if next_cell < final_cell:
            next_cell = final_cell
        cell = next_cell
    passes.append(final_cell)
    return passes

def draw_pass(cell, frame, pass_index, pass_count):
    cols = imax(4, (w + cell - 1) // cell)
    rows = imax(4, (h + cell - 1) // cell)
    zoom = 1.0 + frame * 0.08
    span_x = 3.1 / zoom
    span_y = span_x * h / w
    left = -0.72 - span_x / 2.0
    top = 0.02 - span_y / 2.0
    gfx.text(8, 8, "mandel pass " + str(pass_index) + "/" + str(pass_count) + " cell " + str(cell), 0xffffff)
    for py in range(rows):
        cy = top + span_y * (py * cell + cell // 2) / h
        for px in range(cols):
            cx = left + span_x * (px * cell + cell // 2) / w
            c = palette(mandel(cx, cy, limit), limit)
            gfx.rect(px * cell, py * cell, cell, cell, c, 1)
        gfx.present()
    gfx.present()
    pause(wait)

passes = build_passes(final_cell)

for f in range(frames):
    gfx.clear(0)
    gfx.text(8, 8, "mandel progressive render", 0xffffff)
    gfx.present()
    for i in range(len(passes)):
        draw_pass(passes[i], f, i + 1, len(passes))

elapsed = 0
while hold_ms < 0 or elapsed < hold_ms:
    gfx.present()
    pause(50)
    elapsed += 50
