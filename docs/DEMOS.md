# Demo Scripts

[日本語](DEMOS.ja.md)

The `demos/` directory contains MicroPython scripts intended to be copied to the
Tab5 microSD card. Each demo also has a `.txt` help file that can be read on the
Tab5 with `cat`.

```text
cat /mandel.txt
cat /plasma.txt
cat /life.txt
```

Graphics demos use the firmware `gfx` object and draw through the M5GFX sprite.
Press `Ctrl-C` or `q` to interrupt a running graphics script.

## Copying Demos To SD

Using an SSH profile on the Tab5:

```text
scp get /home/demo/mandel.py /mandel.py 0
scp get /home/demo/mandel.txt /mandel.txt 0
```

From a direct SCP endpoint:

```text
scp get demo@192.0.2.10:/home/demo/plasma.py /plasma.py
```

## Mandelbrot

```text
python /mandel.py [wait_ms] [frames] [final_cell_px] [hold_ms]
```

Progressive Mandelbrot renderer. It starts with coarse blocks and redraws the
same view with smaller blocks until `final_cell_px` is reached.

- `wait_ms`: delay after each refinement pass. Default: `0`.
- `frames`: number of zoom frames. Default: `1`.
- `final_cell_px`: final block size. Default: `10`, minimum: `1`.
- `hold_ms`: final image hold time. Default: `60000`; use `-1` to hold until interrupt.

Examples:

```text
python /mandel.py
python /mandel.py 0 1 8 -1
python /mandel.py 0 1 1 -1
```

`final_cell_px=1` is intentionally possible, but it is very slow because it
calculates the fractal for every pixel in MicroPython.

## Plasma

```text
python /plasma.py [wait_ms] [frames] [cell_px]
```

Classic sine plasma animation. Several sine waves, including a radial distance
wave, are combined into a moving color field. The frame is drawn into the
sprite first and presented once per frame.

- `wait_ms`: delay after each frame. Default: `0`.
- `frames`: number of animation frames. Default: `160`.
- `cell_px`: block size. Default: `12`, minimum: `6`.

Examples:

```text
python /plasma.py
python /plasma.py 0 160 24
python /plasma.py 0 160 16
```

Smaller `cell_px` values look smoother but cost many more draw commands.

## Hat

```text
python /hat.py [wait_ms] [frames] [hold_ms]
```

Retro ten-gallon-hat / sombrero wireframe. The surface is presented after each
wire row so the drawing process is visible.

- `wait_ms`: delay after each wire row. Default: `0`.
- `frames`: number of frames. Default: `1`.
- `hold_ms`: final image hold time. Default: `60000`; use `-1` to hold until interrupt.

Examples:

```text
python /hat.py
python /hat.py 5 1 -1
```

## Life

```text
python /life.py [wait_ms] [frames] [cell_px]
```

Conway's Game of Life on a fixed 25x25 board. It uses `gfx.mono()` to send the
board as a packed 1-bit bitmap so the firmware can draw it efficiently.

- `wait_ms`: delay after each frame. Default: `0`.
- `frames`: maximum generations. Default: `200`.
- `cell_px`: cell size. Default: `12`, minimum: `6`.

Examples:

```text
python /life.py
python /life.py 0 500 10
```

## Starfield

```text
python /starfield.py [wait_ms] [frames] [speed]
```

Center-out graphical starfield.

- `wait_ms`: delay after each frame. Default: `0`.
- `frames`: number of frames. Default: `30`.
- `speed`: star movement speed. Default: `5`.

Example:

```text
python /starfield.py 0 120 7
```

## Maze

```text
python /maze.py [wait_ms] [cols] [rows]
```

Animated maze generation and solving demo.

- `wait_ms`: delay after animation steps. Default: `0`.
- `cols`: requested maze width. Default: `31`, rounded down to odd.
- `rows`: requested maze height. Default: `19`, rounded down to odd.

Example:

```text
python /maze.py 10 25 17
```

## termgfx.py

`termgfx.py` is a small reference/helper file for writing gfx scripts. Current
SD imports are limited, so bundled demos remain standalone.
