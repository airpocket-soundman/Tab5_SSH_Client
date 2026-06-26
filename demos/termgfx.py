# Tiny helper layer for the Tab5 MicroPython gfx API.
# Importing files from SD is limited in the current embedded VM, so demo scripts
# are still standalone. This file documents the intended drawing primitives.

BLACK = 0x000000
WHITE = 0xffffff
RED = 0xff3030
GREEN = 0x30ff70
BLUE = 0x2070ff
YELLOW = 0xffcc20

def rgb(r, g, b):
    return (int(r) << 16) | (int(g) << 8) | int(b)

def clear(color=BLACK):
    gfx.clear(color)

def present():
    gfx.present()

def size():
    return gfx.size()

def pixel(x, y, color=WHITE):
    gfx.pixel(x, y, color)

def line(x0, y0, x1, y1, color=WHITE):
    gfx.line(x0, y0, x1, y1, color)

def rect(x, y, w, h, color=WHITE, fill=True):
    gfx.rect(x, y, w, h, color, 1 if fill else 0)

def circle(x, y, r, color=WHITE, fill=True):
    gfx.circle(x, y, r, color, 1 if fill else 0)

def text(x, y, s, color=WHITE):
    gfx.text(x, y, s, color)
