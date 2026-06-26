# Animated graphical maze generation and solving demo.
# Usage: python /maze.py [wait_ms] [cols] [rows]

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

def lcg(seed):
    return (seed * 73 + 41) % 9973

def imin(a, b):
    return a if a < b else b

def odd(n):
    if n < 9:
        n = 9
    return n if n % 2 else n - 1

def shuffled(seed):
    dirs = [(2,0),(-2,0),(0,2),(0,-2)]
    for i in range(3, 0, -1):
        seed = lcg(seed)
        j = seed % (i + 1)
        dirs[i], dirs[j] = dirs[j], dirs[i]
    return dirs, seed

def draw(grid, cell):
    colors = [0x000000, 0xffffff, 0x2070ff, 0xffcc20, 0xff3030]
    gfx.clear(0)
    for y in range(len(grid)):
        for x in range(len(grid[0])):
            gfx.rect(x * cell, y * cell, cell, cell, colors[grid[y][x]], 1)
    gfx.present()

def generate(grid, cell, wait):
    rows = len(grid)
    cols = len(grid[0])
    stack = [(1, 1)]
    grid[1][1] = 0
    seed = 911
    tick = 0
    while stack:
        x, y = stack[-1]
        old = grid[y][x]
        grid[y][x] = 4
        dirs, seed = shuffled(seed)
        carved = False
        for dx, dy in dirs:
            nx = x + dx
            ny = y + dy
            if 0 < nx < cols - 1 and 0 < ny < rows - 1 and grid[ny][nx] == 1:
                grid[y + dy // 2][x + dx // 2] = 0
                grid[ny][nx] = 0
                stack.append((nx, ny))
                carved = True
                break
        grid[y][x] = old if old != 4 else 0
        if not carved:
            stack.pop()
        tick += 1
        if tick % 2 == 0:
            draw(grid, cell)
            pause(wait)
    grid[1][0] = 0
    grid[rows - 2][cols - 1] = 0

def solve(grid, cell, wait):
    rows = len(grid)
    cols = len(grid[0])
    start = (0, 1)
    goal = (cols - 1, rows - 2)
    queue = [start]
    parent = {start: None}
    head = 0
    tick = 0
    while head < len(queue):
        x, y = queue[head]
        head += 1
        if (x, y) == goal:
            break
        for dx, dy in [(1,0),(-1,0),(0,1),(0,-1)]:
            nx = x + dx
            ny = y + dy
            if 0 <= nx < cols and 0 <= ny < rows and grid[ny][nx] == 0 and (nx, ny) not in parent:
                parent[(nx, ny)] = (x, y)
                queue.append((nx, ny))
                if (nx, ny) != goal:
                    grid[ny][nx] = 2
        tick += 1
        if tick % 4 == 0:
            grid[y][x] = 4
            draw(grid, cell)
            pause(wait)
            if grid[y][x] == 4:
                grid[y][x] = 2
    p = goal
    while p in parent and p is not None:
        x, y = p
        grid[y][x] = 3
        p = parent[p]
        draw(grid, cell)
        pause(wait)

wait = argi(1, 0)
w, h = gfx.size()
cell = 18
cols = odd(imin(argi(2, 31), w // cell))
rows = odd(imin(argi(3, 19), h // cell))
grid = [[1 for _ in range(cols)] for _ in range(rows)]
generate(grid, cell, wait)
solve(grid, cell, wait)
