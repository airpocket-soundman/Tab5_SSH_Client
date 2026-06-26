# Tab5 Python Mode

The firmware embeds a MicroPython runtime for local REPL use, SD-card scripts,
and graphics demos without replacing the SSH terminal firmware.

## Commands

```text
python
python <sd.py> [args...]
python -c <statement>
python --reset
```

Serial API commands:

```text
python -c <statement>
python <sd.py> [args...]
python --reset
```

## Script Arguments

Scripts receive arguments in `argv`. The runner also tries to set `sys.argv`
when the embedded MicroPython build supports it.

```python
print(argv)
```

Example:

```text
python /life.py 20 120
python /mandel.py 0 1 8 -1
```

## Graphics API

Python scripts automatically receive a global `gfx` object. It draws through the
firmware's M5GFX sprite, not through terminal characters. Coordinates are
relative to the area below the top header bar.

```python
w, h = gfx.size()
gfx.clear(0x000000)
gfx.pixel(10, 10, 0xffffff)
gfx.line(20, 20, w - 20, h - 20, 0x30ff70)
gfx.rect(40, 40, 120, 80, 0x2070ff, 1)
gfx.circle(w // 2, h // 2, 40, 0xffcc20, 0)
gfx.text(8, 8, "Tab5 gfx", 0xffffff)
gfx.mono(20, 40, 8, 8, 10, 0x30ff70, 0x000000, "8040201008040201")
gfx.present()
```

Available methods:

| Method | Description |
| --- | --- |
| `gfx.size()` | Return `(width, height)` for the graphics area. |
| `gfx.clear(color)` | Fill the graphics area. |
| `gfx.pixel(x, y, color)` | Draw one pixel. |
| `gfx.line(x0, y0, x1, y1, color)` | Draw a line. |
| `gfx.rect(x, y, w, h, color, fill=1)` | Draw or fill a rectangle. |
| `gfx.fill(x, y, w, h, color)` | Fill a rectangle. |
| `gfx.circle(x, y, r, color, fill=1)` | Draw or fill a circle. |
| `gfx.text(x, y, text, color=0xffffff)` | Draw text on the graphics surface. |
| `gfx.mono(x, y, cols, rows, cell, fg, bg, bits)` | Draw a packed 1-bit cell bitmap in one command. |
| `gfx.present()` | Push the sprite to the display and poll for script interrupt. |

Colors are integer `0xRRGGBB` values.
`gfx.mono()` packs cells into hexadecimal bits, left to right, top to bottom.

## Stopping Graphics Scripts

Graphics scripts are interrupted at `gfx.present()` so animations can remain
responsive without adding input code to every demo. Press one of these keys:

```text
Ctrl-C
q
```

`Ctrl-C` is the standard interrupt key. `q` is also accepted by the bundled
graphics demos as a quick stop key. The runner reports the stop as
`interrupted`.

## Drawing Model

The graphics API is intentionally sprite-based:

1. Python sends drawing commands to the firmware.
2. The firmware draws into the M5GFX sprite.
3. `gfx.present()` pushes the sprite to the display.

This means scripts choose when the user sees updates. For construction demos
such as `mandel.py` and `hat.py`, call `gfx.present()` after each row or pass.
For animation demos such as `plasma.py`, draw a complete frame first and call
`gfx.present()` once per frame.

## Demo Scripts

The repository contains SD-card demo scripts under `demos/`.

```text
python /mandel.py 0 1 8 -1
python /plasma.py 0 160 16
python /hat.py 5 1 -1
python /life.py 0 500 10
python /starfield.py 0 120 7
python /maze.py 10 25 17
```

These demos use `gfx`, so they are graphical M5GFX drawing demos rather than
ASCII-art terminal output.

See [DEMOS.md](DEMOS.md) for arguments and behavior.
