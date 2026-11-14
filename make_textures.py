# Generates textures.png: a 4x4 atlas of 16x16 pixel-art tiles for Barnocraft.
# Tile order (index = row*4 + col):
#  0 grass top   1 grass side  2 dirt      3 stone
#  4 log side    5 log top     6 glass     7 leaves
#  8 bedrock     9 coal ore   10 iron ore
import random
from PIL import Image

T = 16
atlas = Image.new("RGBA", (T * 4, T * 4), (0, 0, 0, 0))
rnd = random.Random(1337)


def clamp(v):
    return max(0, min(255, int(v)))


def noisy(base, spread, n=T * T):
    return [tuple(clamp(c + rnd.randint(-spread, spread)) for c in base) for _ in range(n)]


def tile(idx, px):
    ox, oy = (idx % 4) * T, (idx // 4) * T
    for y in range(T):
        for x in range(T):
            p = px[y * T + x]
            atlas.putpixel((ox + x, oy + y), p if len(p) == 4 else p + (255,))


def shade(c, k):
    return tuple(clamp(v * k) for v in c[:3])


# --- dirt ---
def dirt_px():
    px = []
    for i in range(T * T):
        r = rnd.random()
        base = (134, 96, 67)
        if r < 0.12:
            base = (108, 76, 52)
        elif r < 0.2:
            base = (152, 112, 80)
        elif r < 0.23:
            base = (122, 122, 122)
        px.append(shade(base, 0.92 + rnd.random() * 0.16))
    return px


dirt = dirt_px()

# --- grass top ---
grass_top = []
for i in range(T * T):
    k = 0.8 + rnd.random() * 0.35
    grass_top.append(shade((98, 160, 58), k))

# --- grass side: dirt with a ragged green fringe ---
grass_side = list(dirt)
for x in range(T):
    depth = 3 + rnd.randint(0, 2)
    for y in range(depth):
        grass_side[y * T + x] = shade((98, 160, 58), 0.8 + rnd.random() * 0.3)

# --- stone ---
stone = []
for i in range(T * T):
    k = 0.85 + rnd.random() * 0.2
    stone.append(shade((126, 126, 126), k))
for _ in range(7):  # darker cracks
    x, y = rnd.randrange(T), rnd.randrange(T)
    for _ in range(rnd.randint(2, 5)):
        stone[y * T + x] = shade((126, 126, 126), 0.62)
        x = (x + rnd.choice((-1, 0, 1))) % T
        y = (y + rnd.choice((0, 1))) % T


def ore(base, spots, colors):
    px = list(base)
    for _ in range(spots):
        cx, cy = rnd.randrange(2, T - 2), rnd.randrange(2, T - 2)
        for dx, dy in ((0, 0), (1, 0), (0, 1), (1, 1), (-1, 0), (0, -1)):
            if rnd.random() < 0.8:
                px[(cy + dy) * T + cx + dx] = rnd.choice(colors)
    return px


coal = ore(stone, 6, [(30, 30, 30), (45, 45, 45), (20, 20, 22)])
iron = ore(stone, 6, [(216, 175, 147), (196, 150, 120), (230, 196, 168)])

# --- log side: vertical bark ---
log_side = []
cols = [0.75 + rnd.random() * 0.35 for _ in range(T)]
for y in range(T):
    for x in range(T):
        k = cols[x] * (0.9 + rnd.random() * 0.15)
        if x % 4 == 0:
            k *= 0.75
        log_side.append(shade((112, 84, 50), k))

# --- log top: rings ---
log_top = []
for y in range(T):
    for x in range(T):
        d = max(abs(x - 7.5), abs(y - 7.5))
        if d > 6.5:
            c = shade((112, 84, 50), 0.85 + rnd.random() * 0.2)
        else:
            ring = int(d) % 2
            c = shade((176, 144, 92), (0.88 if ring else 1.0) + rnd.random() * 0.06)
        log_top.append(c)

# --- leaves ---
leaves = []
for i in range(T * T):
    r = rnd.random()
    if r < 0.18:
        leaves.append((34, 78, 26))
    else:
        leaves.append(shade((58, 128, 42), 0.75 + rnd.random() * 0.45))

# --- glass: light frame, mostly transparent centre with a streak ---
glass = []
for y in range(T):
    for x in range(T):
        if x in (0, T - 1) or y in (0, T - 1):
            glass.append((222, 240, 255, 230))
        elif (x - y) in (3, 4) and 2 < x < 10:
            glass.append((240, 250, 255, 170))
        elif (x - y) == -6 and 8 < x < 14:
            glass.append((240, 250, 255, 150))
        else:
            glass.append((180, 220, 250, 45))

# --- bedrock ---
bedrock = []
for i in range(T * T):
    r = rnd.random()
    base = (52, 52, 56) if r < 0.5 else (88, 88, 92) if r < 0.75 else (30, 30, 32)
    bedrock.append(shade(base, 0.9 + rnd.random() * 0.2))

for i, px in enumerate([grass_top, grass_side, dirt, stone, log_side, log_top,
                        glass, leaves, bedrock, coal, iron]):
    tile(i, px)

atlas.save("textures.png", optimize=True)
atlas.resize((T * 4 * 6, T * 4 * 6), Image.NEAREST).save("textures_preview.png")
print("ok")
