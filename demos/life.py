# Conway's Game of Life graphical demo using Tab5 MicroPython gfx API.
# Usage: python /life.py [wait_ms] [frames] [cell_px]

try:
    args = argv
except NameError:
    import sys
    args = sys.argv

def argi(i, default):
    try:
        return int(args[i])
    except Exception:
        return default

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

def put(g, x, y):
    if 0 <= x < COLS and 0 <= y < ROWS:
        g[y * COLS + x] = 1

def imin(a, b):
    return a if a < b else b

def seed(g):
    ox = -7
    oy = 7
    cells = (
        (24,0),(22,1),(24,1),(12,2),(13,2),(20,2),(21,2),
        (11,3),(15,3),(20,3),(21,3),(10,4),(16,4),
        (10,5),(14,5),(16,5),(17,5),(22,5),(24,5),
        (10,6),(16,6),(24,6),(11,7),(15,7),(12,8),(13,8)
    )
    for x, y in cells:
        put(g, ox + x, oy + y)
    for x, y in ((2,2),(3,2),(4,2),(18,4),(18,5),(18,6),(5,19),(6,19),(7,19)):
        put(g, x, y)

def encode_bits(g):
    out = ""
    nibble = 0
    count = 0
    for v in g:
        nibble = (nibble << 1) | (1 if v else 0)
        count += 1
        if count == 4:
            out += HEX[nibble]
            nibble = 0
            count = 0
    if count:
        out += HEX[nibble << (4 - count)]
    return out

def draw(g):
    gfx.mono(ORIGIN_X, ORIGIN_Y, COLS, ROWS, CELL, 0x30ff70, 0x000000, encode_bits(g))
    gfx.present()

def step(g, scratch):
    changed = 0
    for y in range(ROWS):
        ym = ((y - 1) % ROWS) * COLS
        yr = y * COLS
        yp = ((y + 1) % ROWS) * COLS
        for x in range(COLS):
            xm = (x - 1) % COLS
            xp = (x + 1) % COLS
            n = (
                g[ym + xm] + g[ym + x] + g[ym + xp] +
                g[yr + xm]             + g[yr + xp] +
                g[yp + xm] + g[yp + x] + g[yp + xp]
            )
            alive = g[yr + x]
            next_alive = 1 if n == 3 or (alive and n == 2) else 0
            scratch[yr + x] = next_alive
            if next_alive != alive:
                changed += 1
    return changed

COLS = 25
ROWS = 25
HEX = "0123456789abcdef"
wait = argi(1, 0)
frames = argi(2, 200)
w, h = gfx.size()
CELL = argi(3, 12)
if CELL < 6:
    CELL = 6
max_cell = imin(w // COLS, h // ROWS)
if CELL > max_cell:
    CELL = max_cell
ORIGIN_X = (w - COLS * CELL) // 2
ORIGIN_Y = (h - ROWS * CELL) // 2

grid = [0] * (COLS * ROWS)
next_grid = [0] * (COLS * ROWS)
seed(grid)

gfx.clear(0)
gfx.rect(ORIGIN_X - 2, ORIGIN_Y - 2, COLS * CELL + 3, ROWS * CELL + 3, 0x305070, 0)
gfx.text(8, 8, "life 25x25  Ctrl-C/q: stop", 0xffffff)
draw(grid)

for frame in range(frames):
    changes = step(grid, next_grid)
    grid, next_grid = next_grid, grid
    draw(grid)
    if changes == 0:
        break
    pause(wait)

gfx.text(8, 28, "done", 0xffcc20)
gfx.present()
