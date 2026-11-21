#!/usr/bin/env python3
"""PAWS OFF! -- art + music generator.

Everything in the game (sprites, garden map, title screen, music) is authored
here as ASCII art / procedural drawing and converted to C arrays in
source/gfx.c + source/gfx.h.  Preview PNGs land in tools/preview/.

Run:  python3 tools/mkgfx.py      (or `make art`)
"""
import math, os, struct, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PREV = os.path.join(HERE, "preview")
os.makedirs(PREV, exist_ok=True)

# ---------------------------------------------------------------- utils --

def rgb(h):
    h = h.lstrip('#')
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))

def clamp8(v):
    return max(0, min(255, int(round(v))))

def to15(c):
    r, g, b = c
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)

def mix(a, b, t):
    return tuple(clamp8(a[i] + (b[i] - a[i]) * t) for i in range(3))

def write_png(fn, rows, scale=1):
    h = len(rows); w = len(rows[0])
    raw = bytearray()
    for y in range(h * scale):
        raw.append(0)
        r = rows[y // scale]
        for x in range(w):
            raw.extend(bytes(r[x]) * scale)
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    with open(fn, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w * scale, h * scale, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))

def hsh(x, y, s=0):
    n = (x * 374761393 + y * 668265263 + s * 2147483647) & 0xffffffff
    n = ((n ^ (n >> 13)) * 1274126177) & 0xffffffff
    return (n ^ (n >> 16)) & 0xffff

def bayer(x, y):
    m = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]
    return (m[y & 3][x & 3] + 0.5) / 16.0

# -------------------------------------------------------------- tinting --

def night(c, k=1.0):
    r, g, b = c
    n = (r * 0.34 + 6, g * 0.46 + 12, b * 0.66 + 34)
    return mix(c, tuple(clamp8(v) for v in n), k)

def dawn(c):
    r, g, b = c
    return (clamp8(r * 1.02 + 14), clamp8(g * 0.95 + 8), clamp8(b * 0.86 + 10))

def lit(c):
    r, g, b = c
    return (clamp8(r * 1.05 + 40), clamp8(g * 1.0 + 30), clamp8(b * 0.62 + 6))

def tint(c, mode):
    """-> (night colour, dawn colour)"""
    if mode == 't':   return night(c), dawn(c)           # ordinary scenery
    if mode == 'e':   return c, mix(c, (200, 196, 190), 0.55)  # glowing lights
    if mode == 'l':   return lit(night(c, 0.35)), dawn(c)      # lamp-lit ground
    if mode == 'o':   return night(c, 0.28), c           # sprites: stay readable
    return c, c                                          # 'c' constant

# ------------------------------------------------------------- palettes --
# Each palette: list of (name, hex, mode).  Index 0 is always transparent.

BGPALS = [
    # 0: nature
    [('g1', '1f5a36', 't'), ('g2', '2f7a42', 't'), ('g3', '45964e', 't'), ('g4', '6cb85c', 't'),
     ('h1', '0a2a22', 't'), ('h2', '2a7848', 't'), ('h3', '4aa862', 't'),
     ('t1', '4a3020', 't'), ('t2', '7a5434', 't'),
     ('f1', 'ff7aa8', 't'), ('f2', 'ffe070', 't'), ('f3', 'f4f4ff', 't'),
     ('s1', '555870', 't'), ('s2', '7c809a', 't'), ('s3', 'a6aac0', 't')],
    # 1: manor, lamps, trash cans
    [('w1', '4a3c50', 't'), ('w2', '6a5a70', 't'), ('w3', '8c7c90', 't'),
     ('r1', '50242c', 't'), ('r2', '7a3438', 't'), ('r3', 'a04c48', 't'),
     ('d1', '4a2c1c', 't'), ('d2', '7c5030', 't'),
     ('y1', 'fff6b8', 'e'), ('y2', 'ffd060', 'e'), ('y3', 'f09830', 'e'),
     ('k', '2c2030', 't'),
     ('m1', '4c5468', 't'), ('m2', '7a8498', 't'), ('m3', 'aab4c8', 't')],
    # 2: water (fountain + pond). c1..c3 are palette-cycled for shimmer.
    [('g1', '1f5a36', 't'), ('g2', '2f7a42', 't'), ('g3', '45964e', 't'),
     ('s1', '555870', 't'), ('s2', '7c809a', 't'), ('s3', 'a6aac0', 't'),
     ('b1', '1c4a8c', 't'), ('b2', '2c6cb8', 't'), ('b3', '4a94d8', 't'),
     ('c1', '2c6cb8', 't'), ('c2', '4a94d8', 't'), ('c3', 'b4e0f8', 't'),
     ('wh', 'f0f8ff', 't'), ('f1', 'ff7aa8', 't'), ('g4', '6cb85c', 't')],
    # 3: lamp-lit ground. L* entries glow warm at night and vanish at dawn.
    [('g1', '1f5a36', 't'), ('g2', '2f7a42', 't'), ('g3', '45964e', 't'), ('g4', '6cb85c', 't'),
     ('Lg1', '1f5a36', 'l'), ('Lg2', '2f7a42', 'l'), ('Lg3', '45964e', 'l'), ('Lg4', '6cb85c', 'l'),
     ('s1', '555870', 't'), ('s2', '7c809a', 't'), ('s3', 'a6aac0', 't'),
     ('Ls1', '555870', 'l'), ('Ls2', '7c809a', 'l'), ('Ls3', 'a6aac0', 'l'),
     ('Lf', 'ffe070', 'l')],
    # 4..8: text colours.  1 = ink, 2 = shadow, 3 = HUD bar, 4.. = HUD icons
] + [
    [('ink', ink, 'c'), ('sh', '0c0c1c', 'c'), ('bar', '14142c', 'c'),
     ('au', 'ffd23e', 'c'), ('ad', 'c8901a', 'c'), ('al', 'fff3a0', 'c'),
     ('si', 'c8d0e0', 'c'), ('sd', '7c86a0', 'c'), ('cy', '4fd8e8', 'c'),
     ('rd', 'e03050', 'c'), ('mo', 'f4f0c8', 'c'), ('or', 'ff9030', 'c'),
     ('pk', 'ff9ec4', 'c'), ('gr', '4fd070', 'c'), ('pu', 'a050e0', 'c')]
    for ink in ('ffffff', 'ffd23e', 'ff6a8a', '6fe8f0', '9a9ab8')
]
BG_FALLBACK = {'h1': 'g1', 'h2': 'g1', 'h3': 'g2', 'f1': 'g4', 'f2': 'g4', 'f3': 'g4',
               't1': 'g1', 't2': 's1', 'g4': 'g3', 'Lf': 'Lg4',
               'Lh1': 'Lg1', 'Lh2': 'Lg1', 'Lh3': 'Lg2', 'Lt1': 'Lg1', 'Lt2': 'Ls1',
               'Lf1': 'Lf', 'Lf2': 'Lf', 'Lf3': 'Lf'}

OBJPALS = [
    # 0: the Duchess
    [('o', '1a1426'), ('s', 'f2c9a0'), ('S', 'c9966e'), ('h', 'e8e4f0'), ('H', 'a9a3c2'),
     ('g', '8a3fa8'), ('G', '5c2878'), ('p', 'b869d6'), ('y', 'ffd23e'), ('Y', 'c8901a'),
     ('w', 'ffffff'), ('r', 'e03050'), ('c', '4fd8e8'), ('e', '30243c'), ('k', 'ff9ec4')],
    # 1: raccoon
    [('o', '15121c'), ('f', '8d8a99'), ('F', '5e5b6e'), ('l', 'b9b6c4'), ('m', '25222e'),
     ('w', 'f4f2f8'), ('n', '2a2030'), ('t', '2c2a36'), ('e', 'fff8c0'), ('y', 'ffd23e'),
     ('p', '4a4757'), ('r', 'e03050'), ('c', '4fd8e8'), ('x', '000000'), ('z', '000000')],
    # 2: kit (warm brown)
    [('o', '1c1410'), ('f', 'b08a62'), ('F', '7c5c40'), ('l', 'd8b88c'), ('m', '32241c'),
     ('w', 'fff6e8'), ('n', 'f08aa0'), ('t', '4c3626'), ('e', 'ffe65a'), ('y', 'ffd23e'),
     ('p', '5a4230'), ('r', 'e03050'), ('c', '4fd8e8'), ('x', '000000'), ('z', '000000')],
    # 3: chonk (blue-black, red eyes)
    [('o', '0c0c18'), ('f', '565a7c'), ('F', '383a58'), ('l', '7c82a8'), ('m', '14142a'),
     ('w', 'd8dcf4'), ('n', 'c86a90'), ('t', '22243c'), ('e', 'ff5a4a'), ('y', 'ffd23e'),
     ('p', '2c2e48'), ('r', 'e03050'), ('c', '4fd8e8'), ('x', '000000'), ('z', '000000')],
    # 4: items + fx
    [('o', '181020'), ('y', 'ffd23e'), ('Y', 'c8901a'), ('L', 'fff3a0'), ('w', 'ffffff'),
     ('s', 'c8d0e0'), ('S', '7c86a0'), ('c', '4fd8e8'), ('b', '2f6fd0'), ('r', 'e03050'),
     ('k', 'ff9ec4'), ('g', '4fd070'), ('p', 'a050e0'), ('d', '06060c'), ('a', 'ff9030')],
]

def build_pal(pal, modes_default=None):
    n = [(0, 0, 0)]; d = [(0, 0, 0)]
    for e in pal:
        name, hx = e[0], e[1]
        mode = e[2] if len(e) > 2 else modes_default
        a, b = tint(rgb(hx), mode)
        n.append(a); d.append(b)
    while len(n) < 16:
        n.append((0, 0, 0)); d.append((0, 0, 0))
    return n, d

BG_N, BG_D, BG_IDX = [], [], []
for p in BGPALS:
    n, d = build_pal(p)
    BG_N.append(n); BG_D.append(d)
    BG_IDX.append({e[0]: i + 1 for i, e in enumerate(p)})
OBJ_N, OBJ_D, OBJ_IDX = [], [], []
for i, p in enumerate(OBJPALS):
    n, d = build_pal(p, 'c' if i == 4 else 'o')
    OBJ_N.append(n); OBJ_D.append(d)
    idx = {e[0]: j + 1 for j, e in enumerate(p)}
    idx['.'] = 0
    OBJ_IDX.append(idx)

# ---------------------------------------------------------------- sprites --

def art(pal, text, w=None, h=None, oy=0, ox=0):
    """ASCII -> grid of palette indices, padded to w x h."""
    idx = OBJ_IDX[pal]
    rows = [r for r in text.strip('\n').split('\n')]
    width = len(rows[0])
    for i, r in enumerate(rows):
        assert len(r) == width, "row %d has width %d, want %d: %r" % (i, len(r), width, r)
    w = w or width; h = h or len(rows)
    g = [[0] * w for _ in range(h)]
    for y, r in enumerate(rows):
        for x, ch in enumerate(r):
            if 0 <= y + oy < h and 0 <= x + ox < w:
                g[y + oy][x + ox] = idx[ch]
    return g

def outline(g, col):
    h = len(g); w = len(g[0])
    out = [r[:] for r in g]
    for y in range(h):
        for x in range(w):
            if g[y][x] == 0:
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    xx, yy = x + dx, y + dy
                    if 0 <= xx < w and 0 <= yy < h and g[yy][xx] != 0:
                        out[y][x] = col
                        break
    return out

def blank(w, h):
    return [[0] * w for _ in range(h)]

def paste(dst, src, ox, oy):
    for y, r in enumerate(src):
        for x, v in enumerate(r):
            if v and 0 <= y + oy < len(dst) and 0 <= x + ox < len(dst[0]):
                dst[y + oy][x + ox] = v
    return dst

def hflip(g):
    return [r[::-1] for r in g]

def shift_rows(g, rows, dx):
    out = [r[:] for r in g]
    w = len(g[0])
    for y in rows:
        r = g[y]
        out[y] = [r[x - dx] if 0 <= x - dx < w else 0 for x in range(w)]
    return out

SPRITES = []   # (name, pal, grid)
def add(name, pal, g):
    assert len(g) % 8 == 0 and len(g[0]) % 8 == 0, name
    SPRITES.append((name, pal, g))

# ---- the Duchess (16x32, feet at row 29) ----
DU_FRONT = """
......oooo......
.....ohhhho.....
....ohhhhhho....
....oHhhhhHo....
...oyyyccyyyo...
...ohhhhhhhho...
..ohhssssssHho..
..ohsssssssSho..
..ohsesssessho..
..oHsssssssSHo..
...oSskrrksSo...
....oSssssSo....
....owwwwwwo....
...ogpwwcwwGo...
..osgpggggGGso..
..osgpggggGGso..
...ogpggggGGo...
...ogpgggggGo...
..ogpggggggGGo..
..ogpggggggGGo..
.ogppgggggggGGo.
.ogpggggggggGGo.
.ogpggGgggGggGo.
.oGggGGggGGGgGo.
..oGGGGGGGGGGo..
...oooooooooo...
"""
DU_BACK = """
......oooo......
.....ohhhho.....
....ohhhhhho....
....oHhhhhHo....
...oyyyyyyyyo...
...ohhhhhhhho...
..ohhhhhhhhhho..
..ohhHhhhhHhho..
..ohhhhhhhhhho..
..oHhhhhhhhhHo..
...oHhhhhhhHo...
....oHHHHHHo....
....osssssso....
...oggggggGGo...
..osggggggGGso..
..osggggggGGso..
...oggggggGGo...
...ogggggggGo...
..oggggggggGGo..
..oggggggggGGo..
.ogggggggggGGGo.
.ogggggggggGGGo.
.ogggGGgggGggGo.
.oGggGGggGGGgGo.
..oGGGGGGGGGGo..
...oooooooooo...
"""
DU_SIDE = """
......oooo......
....oohhhho.....
...ohhhhhhho....
...ohhhhhhHo....
...ohhyyyycco...
...ohhhhhhhho...
..ohhhhhsssso...
..ohhhhssseso...
..ohhhhsssssso..
..oHhhhsssskso..
...oHhhSssrso...
....oHHSssso....
.....owwwwwo....
....ogpgwwcGo...
....ogpsggGGo...
....ogpsggGGo...
....ogpgggGGo...
...ogpggggGGo...
...ogpgggggGGo..
..ogpggggggGGo..
..ogpgggggggGGo.
.ogppgggggggGGo.
.ogpggGgggGggGo.
.oGggGGggGGGgGo.
..oGGGGGGGGGGo..
...oooooooooo...
"""

def duchess_frames(text):
    base = art(0, text)                       # 16 x 26
    def frame(g, bob, sway):
        if bob:   # squash: drop one plain gown row, push the top down a pixel
            g = [[0] * 16] + g[:16] + g[17:]
        if sway:
            g = shift_rows(g, range(20, 26), sway)
            g = shift_rows(g, range(22, 26), sway)
        out = blank(16, 32)
        return paste(out, g, 0, 4)
    return [frame(base, 0, 0), frame(base, 1, -1), frame(base, 0, 0), frame(base, 1, 1)]

for nm, tx in (('DU_D', DU_FRONT), ('DU_U', DU_BACK), ('DU_S', DU_SIDE)):
    fr = duchess_frames(tx)
    for i in (0, 1, 3):
        add('%s%d' % (nm, (0, 1, 0, 2)[i]), 0, fr[i])

# ---- raccoons (16x16) ----
RC_FRONT_A = """
.oo......oo.....
owwo....owwo....
owFfoooofFwo....
offffFFffffo....
owwwfFFfwwwo.oo.
ommmmFFmmmmootto
omewmmmmwemooFFo
oFmmwwwwmmFootto
.oFwwnnwwFo.oFFo
..owwwwwwo.ootto
.offooooffooFFo.
offlfffflffotto.
oflllfflllfoFo..
offllffllffoo...
.oFFooooFFo.....
..oo....oo......
"""
RC_FRONT_B = """
.oo......oo.....
owwo....owwo....
owFfoooofFwo....
offffFFffffo....
owwwfFFfwwwo....
ommmmFFmmmmo.oo.
omewmmmmwemootto
oFmmwwwwmmFooFFo
.oFwwnnwwFo.otto
..owwwwwwo.ooFFo
.offooooffootto.
offlfffflffoFFo.
oflllfflllfotto.
offllffllffooo..
oFFo.oooooFFo...
.oo.......oo....
"""
RC_BACK_A = """
..oo.......oo...
.olFo.....oFlo..
.offfooooofffo..
.offfffffffffo..
.offfffFfffffo..
.offffFFFffffo..
.offfffFfffffo..
..offfffffffo...
..offfffffffo...
.offfffffffffo..
.offffotoffffo..
.offffoFoffffo..
.offffotoffffo..
..offfoFofffo...
..oFFootooFFo...
...oo..o..oo....
"""
RC_BACK_B = """
..oo.......oo...
.olFo.....oFlo..
.offfooooofffo..
.offfffffffffo..
.offfffFfffffo..
.offffFFFffffo..
.offfffFfffffo..
..offfffffffo...
..offfffffffo...
.offfffffffffo..
.offfotoofffffo.
.offfoFofffffo..
.offffotoffffo..
..offffoFoffo...
.oFFoootooFFo...
..oo...o...oo...
"""
RC_SIDE_A = """
................
..........o...o.
.oo......owo.owo
ottoo....offfffo
oFFto...offwwfwo
otFFo..offmmemmo
oFttoooffffmmwwo
.oFFtfffffffwwno
.ottFfffffffwwo.
..offffffffffo..
..offffffffllo..
..offlfffflllo..
..oFfooooooFfo..
..oFo.oFo.oFo...
..oo..oo..oo....
................
"""
RC_SIDE_B = """
................
..........o...o.
..oo.....owo.owo
.ottoo...offfffo
.oFFto..offwwfwo
.otFFo.offmmemmo
.oFttooffffmmwwo
.oFFtfffffffwwno
..otFfffffffwwo.
..offffffffffo..
..offffffffllo..
..offlfffflllo..
...oFfooooFfo...
..oFo.oFooFo....
..oo..oo.oo.....
................
"""
# happy, sitting, holding the shiny (eyes closed ^ ^)
RC_PLAY_A = """
.oo......oo.....
owwo....owwo....
owFfoooofFwo....
offffFFffffo....
owwwfFFfwwwo....
ommwmFFmwmmo....
omwmwmmwmwmo....
oFmmwwwwmmFo....
.oFwwnnwwFo..oo.
oo.owwwwo.oootto
olooffffoolooFFo
ollflllllflootto
.ooflllllfoooFo.
.offllllllfooo..
.oFFFooooFFFo...
..ooo....ooo....
"""
RC_PLAY_B = """
................
.oo......oo.....
owwo....owwo....
owFfoooofFwo....
offffFFffffo....
owwwfFFfwwwo....
ommwmFFmwmmo....
omwmwmmwmwmo....
oFmmwwwwmmFo.oo.
.oFwwnnwwFo.otto
oooowwwwooooFFo.
ollflllllfllotto
.ooflllllfoooFo.
.offllllllfooo..
.oFFFooooFFFo...
..ooo....ooo....
"""
for nm, tx in (('RC_D0', RC_FRONT_A), ('RC_D1', RC_FRONT_B), ('RC_U0', RC_BACK_A), ('RC_U1', RC_BACK_B),
               ('RC_S0', RC_SIDE_A), ('RC_S1', RC_SIDE_B), ('RC_P0', RC_PLAY_A), ('RC_P1', RC_PLAY_B)):
    add(nm, 1, art(1, tx, 16, 16))

# ---- items (palette 4) ----
def item(text, w=8, h=8, ol=True, ox=0, oy=0):
    g = art(4, text, w, h, oy, ox)
    return outline(g, OBJ_IDX[4]['o']) if ol else g

add('COIN0', 4, item("""
........
..yyyy..
.yLLyyY.
.yLyyyY.
.yyyyyY.
.yyyyYY.
..YYYY..
........
"""))
add('COIN1', 4, item("""
........
...yy...
..yLyY..
..yLyY..
..yyyY..
..yyYY..
...YY...
........
"""))
add('COIN2', 4, item("""
........
...yy...
...LY...
...yY...
...yY...
...yY...
...YY...
........
"""))
add('STACK', 4, item("""
................
................
.......w........
......www.......
.......w........
.....yyyyyy.....
....yLLyyyyY....
....YyyyyyyY....
..yyyYYYYYYyyy..
.yLLyyyyyyLLyyY.
.YyyyyyYYyyyyyY.
.yYYYYYYyYYYYYy.
.YyyyyyYYyyyyyY.
..YYYYYYYYYYYY..
................
................
""", 16, 16))

def disco(phase):
    g = blank(16, 16)
    I = OBJ_IDX[4]
    cols = [I['s'], I['w'], I['S'], I['c'], I['k'], I['y']]
    for y in range(16):
        for x in range(16):
            dx, dy = x - 7.5, y - 8.5
            if dx * dx + dy * dy <= 5.6 * 5.6:
                fx, fy = (x + phase) // 2, y // 2
                k = hsh(fx, fy, 3 + phase) % 10
                c = I['s'] if (fx + fy) % 2 == 0 else I['S']
                if dx < -1 and dy < -1 and (fx + fy) % 2 == 0: c = I['w']
                if k == 0: c = I['c']
                if k == 1: c = I['k']
                if k == 2: c = I['y']
                g[y][x] = c
    g[1][7] = g[1][8] = I['S']; g[2][7] = g[2][8] = I['S']       # hanger stub
    return outline(g, I['o'])
add('DISCO0', 4, disco(0))
add('DISCO1', 4, disco(1))

JEWELS = [
("J_TIARA", """
................
................
................
.......y........
..y...yLy...y...
.yLy..ycy..yLy..
.yyy.yyyyy.yyy..
.yYyyyyyyyyyYy..
.yyryyycyyyryy..
.yYyyyyyyyyyYy..
..YYYYYYYYYYY...
................
................
................
................
................
"""),
("J_NECK", """
................
................
..w.........w...
..ws.......sw...
..sw.......ws...
...ws.....sw....
...sww...wws....
....swwwwws.....
.....syyys......
......yry.......
.....yrrry......
......yry.......
.......y........
................
................
................
"""),
("J_RING", """
................
................
......wcw.......
.....wccbc......
.....cccbb......
......cbb.......
.....yyYyy......
....yy...yy.....
....y.....Y.....
....y.....Y.....
....yy...YY.....
.....yyYYY......
......YYY.......
................
................
................
"""),
("J_BROOCH", """
................
................
................
......yyyy......
....yyLppLyy....
...yLppwpppLy...
...yppwpppppy...
..yppppppppppy..
..yppppppppPpy..
...yppppppppy...
...yLppppppLy...
....yyLppLyy....
......yyyy......
................
................
................
""".replace('P', 'p')),
("J_EARR", """
................
................
...yy.....yy....
..y..y...y..y...
..y..y...y..y...
...yy.....yy....
...Y......Y.....
..ggg....ggg....
.gwggg..gwggg...
.gwggg..gwggg...
.ggggg..ggggg...
..ggg....ggg....
...g......g.....
................
................
................
"""),
]
for nm, tx in JEWELS:
    add(nm, 4, item(tx, 16, 16))

add('SPARK0', 4, item("""
........
........
........
...w....
..wLw...
...w....
........
........
""", ol=False))
add('SPARK1', 4, item("""
........
...w....
...w....
.wwLww..
...w....
...w....
........
........
""", ol=False))
add('SPARK2', 4, item("""
...w....
...w....
..wLw...
wwLLLww.
..wLw...
...w....
...w....
........
""", ol=False))
add('HEART', 4, item("""
........
.kk.kk..
kwkkkkk.
kkkkkkk.
.kkkkk..
..kkk...
...k....
........
""", ox=0))
add('NOTE', 4, item("""
........
...cccc.
...c..c.
...c..c.
.ccc.cc.
.ccc.cc.
........
........
"""))
add('ALERT', 4, item("""
..rrr...
..rwr...
..rwr...
..rwr...
..rrr...
..rwr...
..rrr...
........
""", ol=False))
add('DIZZY', 4, item("""
........
.yy..yy.
y..yy..y
...y....
..y..yy.
.y..y..y
.yyy.yy.
........
""", ol=False))
add('DUST0', 4, item("""
........
........
..ss.s..
.sSss...
..sss...
........
........
........
""", ol=False))
add('DUST1', 4, item("""
.s...s..
........
s.s.....
......s.
..s.....
.....s..
.s......
........
""", ol=False))
add('SHADOW', 4, item("""
................
....dddddddd....
..dddddddddddd..
.dddddddddddddd.
.dddddddddddddd.
..dddddddddddd..
....dddddddd....
................
""", 16, 8, ol=False))
add('SHADOWS', 4, item("""
........
........
..dddd..
.dddddd.
.dddddd.
..dddd..
........
........
""", ol=False))
add('ARROW_R', 4, item("""
........
...r....
...rr...
rrrrrr..
rrrrrr..
...rr...
...r....
........
"""))
add('ARROW_U', 4, item("""
........
..rr....
.rrrr...
rrrrrr..
..rr....
..rr....
..rr....
........
"""))
add('MOON', 4, item("""
........
..LLL...
.LL.....
.LL.....
.LL.....
.LLL.L..
..LLL...
........
"""))

DIG = {'0': "111101101101111", '1': "010110010010111", '2': "111001111100111",
       '3': "111001111001111", '4': "101101111001001", '5': "111100111001111",
       '+': "000010111010000"}
def popup(s):
    g = blank(32, 8)
    x = (32 - len(s) * 4) // 2
    for ch in s:
        bits = DIG[ch]
        for i, b in enumerate(bits):
            if b == '1':
                g[1 + i // 3][x + i % 3] = OBJ_IDX[4]['L']
        x += 4
    return outline(g, OBJ_IDX[4]['o'])
for s in ('+20', '+50', '+100', '+150'):
    add('POP' + s[1:], 4, popup(s))

# ------------------------------------------------- sprite tile encoding --

def tiles_of(g):
    out = []
    for ty in range(0, len(g), 8):
        for tx in range(0, len(g[0]), 8):
            words = []
            for y in range(8):
                v = 0
                for x in range(8):
                    v |= (g[ty + y][tx + x] & 15) << (4 * x)
                words.append(v)
            out.append(tuple(words))
    return out

OBJ_TILES = []
OBJ_DEFS = []
for name, pal, g in SPRITES:
    OBJ_DEFS.append((name, len(OBJ_TILES)))
    OBJ_TILES += tiles_of(g)

def sprite_sheet_preview():
    cols = 12; cell = 34
    rows = (len(SPRITES) + cols - 1) // cols
    W, H = cols * cell, rows * cell
    img = [[(70, 110, 80)] * W for _ in range(H)]
    for i, (name, pal, g) in enumerate(SPRITES):
        ox, oy = (i % cols) * cell + 1, (i // cols) * cell + 1
        for y, r in enumerate(g):
            for x, v in enumerate(r):
                if v:
                    img[oy + y][ox + x] = OBJ_D[pal][v]
    write_png(os.path.join(PREV, "sprites.png"), img, 4)
sprite_sheet_preview()

# ==================================================================== MAP ==

MAP = [
    "RRRRRRRRRRRRRRRRRRRRRRRRRRRRRR",
    "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMM",
    "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMM",
    "#o..........,.==.,..........o#",
    "#...T.....,...==...,.....T...#",
    "#.............==.............X",
    "#..#####...L..==..L...#####..#",
    "#.....,.......==.......,.....#",
    "#..........========..........#",
    "#...,......========......,...#",
    "X============================X",
    "#..........========..........#",
    "#..,.......========.......,..#",
    "#........#....==....#........#",
    "#..PPP...#.L..==..L.#...T....#",
    "#.PPPPP..#....==....#........#",
    "X..PPPP.......==.........,...#",
    "#...........,.==.,...###.....#",
    "#....T........==.........T...#",
    "#######X######GG#######X######",
]
MW, MH = 30, 20
assert len(MAP) == MH
for r in MAP:
    assert len(r) == MW, r
PW, PH = MW * 16, MH * 16

def cell(cx, cy):
    if cx < 0 or cx >= MW or cy >= MH: return '#'
    if cy < 0: return 'R'
    return MAP[cy][cx]

class Layer:
    def __init__(self):
        self.px = [[None] * PW for _ in range(PH)]
        self.pal = [[0] * (PW // 8) for _ in range(PH // 8)]
    def put(self, x, y, name):
        if 0 <= x < PW and 0 <= y < PH:
            self.px[y][x] = name
    def get(self, x, y):
        if 0 <= x < PW and 0 <= y < PH:
            return self.px[y][x]
        return None
    def setpal(self, x0, y0, x1, y1, pal):     # pixel rect, inclusive-exclusive
        for ty in range(max(0, y0 // 8), min(PH // 8, (y1 + 7) // 8)):
            for tx in range(max(0, x0 // 8), min(PW // 8, (x1 + 7) // 8)):
                self.pal[ty][tx] = pal

G = Layer()      # ground
O = Layer()      # overhead (drawn above sprites)
SOLID = [[0] * (PW // 8) for _ in range(PH // 8)]

def solid_rect(x0, y0, x1, y1, v=1):
    for ty in range(y0 // 8, (y1 + 7) // 8):
        for tx in range(x0 // 8, (x1 + 7) // 8):
            if 0 <= tx < PW // 8 and 0 <= ty < PH // 8:
                SOLID[ty][tx] |= v

# ---- terrain textures (16px periodic so tiles dedupe well) ----
def grass_px(x, y, var):
    u, v = x & 15, y & 15
    k = hsh(u, v, var)
    if k % 23 == 0: return 'g1'
    # little blades: a light pixel with a lighter tip
    if hsh(u, (v + 1) & 15, var) % 29 == 0: return 'g4'
    if k % 29 == 0: return 'g3'
    if k % 13 == 0: return 'g3'
    return 'g2'

def path_px(x, y):
    u, v = x & 15, y & 15
    row = v // 4
    off = 4 if row % 2 else 0
    if v % 4 == 3: return 's1'
    if (u + off) % 8 == 7: return 's1'
    if v % 4 == 0 and hsh(u, v, 5) % 3 == 0: return 's3'
    if hsh((u + off) // 8, row, 9) % 4 == 0: return 's3' if v % 4 == 0 else 's2'
    return 's2'

def pad_px(x, y):
    u, v = x & 15, y & 15
    if u in (0, 15) or v in (0, 15): return 'w1'
    if v == 7 or (u == 7 and v < 7) or (u == 3 and v > 7) or (u == 11 and v > 7): return 'w1'
    return 'w3' if hsh(u, v, 2) % 9 == 0 else 'w2'

def hedge_cell(cx, cy):
    x0, y0 = cx * 16, cy * 16
    up = cell(cx, cy - 1) in '#'
    dn = cell(cx, cy + 1) == '#' or cy + 1 >= MH
    lf = cell(cx - 1, cy) == '#' or cx - 1 < 0
    rt = cell(cx + 1, cy) == '#' or cx + 1 >= MW
    if cy - 1 < 3: up = False
    for v in range(16):
        for u in range(16):
            k = hsh(u, v, 11)
            c = 'h2'
            if k % 5 == 0: c = 'h3'
            elif k % 7 == 0: c = 'h1'
            if (u % 8 in (2, 3)) and (v % 8 in (1, 2)): c = 'h3'    # leaf clumps
            if (u % 8 == 6) and (v % 8 == 5): c = 'g3'
            face = (not dn) and v >= 11
            if face:
                c = 'h1' if k % 4 else 'h2'
                if v == 11: c = 'h3' if k % 3 else 'h2'
                if v == 15: c = 'h1'
            if not up and v == 0: c = 'h1' if k % 4 else None
            if not up and v == 1 and c != None and k % 3 == 0: c = 'h3'
            if not lf and u == 0: c = 'h1' if k % 5 else None
            if not rt and u == 15: c = 'h1' if k % 5 else None
            if c is not None:
                G.put(x0 + u, y0 + v, c)

def flowers(cx, cy):
    x0, y0 = cx * 16, cy * 16
    n = 0
    for i in range(5):
        fx = 2 + hsh(cx, cy, 20 + i) % 12
        fy = 2 + hsh(cx, cy, 30 + i) % 12
        col = ('f1', 'f2', 'f3')[hsh(cx, cy, 40 + i) % 3]
        G.put(x0 + fx, y0 + fy, col)
        G.put(x0 + fx - 1, y0 + fy, col); G.put(x0 + fx + 1, y0 + fy, col)
        G.put(x0 + fx, y0 + fy - 1, col); G.put(x0 + fx, y0 + fy + 1, 'g1')
        G.put(x0 + fx, y0 + fy, 'f2' if col != 'f2' else 'f3')

# ---- pass 1: base terrain ----
for cy in range(MH):
    for cx in range(MW):
        ch = MAP[cy][cx]
        x0, y0 = cx * 16, cy * 16
        var = hsh(cx, cy, 1) % 4
        if ch in 'RM':
            continue
        if ch in '=G':
            for v in range(16):
                for u in range(16):
                    G.put(x0 + u, y0 + v, path_px(x0 + u, y0 + v))
        elif ch == 'o':
            G.setpal(x0, y0, x0 + 16, y0 + 16, 1)
            for v in range(16):
                for u in range(16):
                    G.put(x0 + u, y0 + v, pad_px(u, v))
        elif ch == 'X':
            for v in range(16):
                for u in range(16):
                    k = hsh(u, v, 17)
                    G.put(x0 + u, y0 + v, 't2' if k % 4 else ('t1' if k % 3 else 'g2'))
        else:
            for v in range(16):
                for u in range(16):
                    G.put(x0 + u, y0 + v, grass_px(u, v, var))
        if ch == ',':
            flowers(cx, cy)

# grassy fringe where path meets lawn
for cy in range(3, MH):
    for cx in range(MW):
        if MAP[cy][cx] != '=': continue
        x0, y0 = cx * 16, cy * 16
        for v in range(16):
            for u in range(16):
                k = hsh(u, v, 23)
                for (dx, dy, d) in ((-1, 0, u), (1, 0, 15 - u), (0, -1, v), (0, 1, 15 - v)):
                    if cell(cx + dx, cy + dy) in '.,TP' and (d == 0 and k % 3 or d == 1 and k % 4 == 0):
                        G.put(x0 + u, y0 + v, 'g2' if k % 2 else 'g3')

for cy in range(3, MH):
    for cx in range(MW):
        if MAP[cy][cx] == '#':
            hedge_cell(cx, cy)
            solid_rect(cx * 16, cy * 16, cx * 16 + 16, cy * 16 + 16)
        if MAP[cy][cx] == 'X':
            solid_rect(cx * 16, cy * 16, cx * 16 + 16, cy * 16 + 16, 2)

# ---- manor ----
def manor():
    G.setpal(0, 0, PW, 48, 1)
    for y in range(48):
        for x in range(PW):
            if y < 20:
                row = y // 5
                off = 4 if row % 2 else 0
                c = 'r2'
                if y % 5 == 4: c = 'r1'
                elif (x + off) % 8 == 0: c = 'r1'
                elif y % 5 == 0: c = 'r3'
                if y == 0: c = 'r1'
            elif y < 22:
                c = 'k'
            else:
                v = y - 22
                row = v // 6
                off = 8 if row % 2 else 0
                c = 'w2'
                if v % 6 == 5 or (x + off) % 16 == 15: c = 'w1'
                elif v % 6 == 0 and hsh(x // 4, row, 3) % 3 == 0: c = 'w3'
                if y == 22: c = 'w1'
                if y >= 46: c = 'w1'
            G.put(x, y, c)
    solid_rect(0, 0, PW, 48)
    # windows
    for i, cx in enumerate((2, 5, 8, 11, 18, 21, 24, 27)):
        x0 = cx * 16 + 1; y0 = 26; w = 14; h = 16
        dark = i in (1, 6)
        for y in range(y0 - 1, y0 + h + 1):
            for x in range(x0 - 1, x0 + w + 1):
                edge = x in (x0 - 1, x0 + w) or y in (y0 - 1, y0 + h)
                if edge:
                    c = 'k'
                elif x == x0 + w // 2 - 1 or x == x0 + w // 2 or y == y0 + 7:
                    c = 'd1' if not dark else 'k'
                elif dark:
                    c = 'w1' if (x + y) % 5 else 'm1'
                else:
                    d = abs(x - (x0 + w / 2 - .5)) / (w / 2) + abs(y - (y0 + h / 2 - .5)) / (h / 2)
                    c = 'y1' if d < 0.75 else ('y2' if d < 1.25 else 'y3')
                    # curtains
                    if x - x0 < 2 or x0 + w - 1 - x < 2: c = 'r3' if (y % 3) else 'r2'
                G.put(x, y, c)
        for x in range(x0 - 2, x0 + w + 2):
            G.put(x, y0 + h + 1, 'w3'); G.put(x, y0 + h + 2, 'w1')
        if i == 2:   # a cat watching from the window
            for (dx, dy) in ((3, 12), (4, 11), (4, 12), (5, 12), (6, 11), (6, 12), (3, 13), (4, 13), (5, 13), (6, 13),
                             (3, 14), (4, 14), (5, 14), (6, 14), (7, 14), (3, 15), (4, 15), (5, 15), (6, 15), (7, 15), (3, 10), (6, 10)):
                G.put(x0 + dx, y0 + dy, 'k')
    # door
    dx0, dx1 = 14 * 16 + 1, 16 * 16 - 1
    for y in range(23, 48):
        for x in range(dx0 - 2, dx1 + 2):
            inside = dx0 <= x < dx1
            if not inside or y < 25:
                c = 'w3' if (x + y) % 7 else 'w2'
                if y == 23 or x in (dx0 - 2, dx1 + 1): c = 'k'
            else:
                u = x - dx0
                c = 'd2'
                if u in (0, 14, 15, 29): c = 'd1'
                if y in (25, 35, 36, 47): c = 'd1'
                if 3 <= u <= 11 or 18 <= u <= 26:
                    if y in (28, 33, 39, 44): c = 'd1'
                    if u in (3, 11, 18, 26) and (28 <= y <= 33 or 39 <= y <= 44): c = 'd1'
                if u in (12, 17) and y in (37, 38): c = 'y2'
            G.put(x, y, c)
    # lantern above the door (overhead layer not needed: it is on the wall)
    for (x, y, c) in ((239, 16, 'k'), (240, 16, 'k'), (238, 17, 'k'), (239, 17, 'y1'), (240, 17, 'y1'), (241, 17, 'k'),
                      (238, 18, 'k'), (239, 18, 'y2'), (240, 18, 'y1'), (241, 18, 'k'), (238, 19, 'k'), (239, 19, 'y2'),
                      (240, 19, 'y2'), (241, 19, 'k'), (239, 20, 'k'), (240, 20, 'k')):
        G.put(x, y, c)
manor()

# ---- lamp-lit ground ----
def relight(cx, cy, rx, ry, strength=1.0):
    for y in range(int(cy - ry) - 1, int(cy + ry) + 2):
        for x in range(int(cx - rx) - 1, int(cx + rx) + 2):
            if not (0 <= x < PW and 48 <= y < PH): continue
            if G.pal[y // 8][x // 8] not in (0, 3): continue
            d = math.hypot((x - cx) / rx, (y - cy) / ry)
            if d > 1: continue
            if G.pal[y // 8][x // 8] == 0:
                G.pal[y // 8][x // 8] = 3
            t = (1 - d) * 2.2 * strength
            if t > bayer(x, y):
                n = G.px[y][x]
                if n and not n.startswith('L'):
                    G.px[y][x] = 'L' + n

# ---- stamps ----
def trashcan(cx, cy):
    x0, y0 = cx * 16, cy * 16
    for v in range(1, 15):
        for u in range(3, 13):
            c = 'm2'
            if u in (3, 12): c = 'k'
            elif u in (5, 8, 10): c = 'm1'
            elif u == 4: c = 'm3'
            if v in (1, 14): c = 'k'
            if v in (2, 3): c = 'm3' if u < 9 else 'm2'
            if v == 4: c = 'k'
            G.put(x0 + u, y0 + v, c)
    for u in (2, 13):
        for v in (2, 3):
            G.put(x0 + u, y0 + v, 'k')
    G.put(x0 + 7, y0, 'k'); G.put(x0 + 8, y0, 'k')
    solid_rect(x0, y0, x0 + 16, y0 + 16)

def lamp(cx, cy):
    x0, y0 = cx * 16, cy * 16
    for v in range(0, 14):
        G.put(x0 + 7, y0 + v, 's3'); G.put(x0 + 8, y0 + v, 's1')
    for u in range(5, 11):
        G.put(x0 + u, y0 + 12, 's3'); G.put(x0 + u, y0 + 13, 's2')
    for u in range(4, 12):
        G.put(x0 + u, y0 + 14, 's1')
    # head on the overhead layer, in the cell above
    O.setpal(x0, y0 - 16, x0 + 16, y0, 1)
    head = [
        "......kk......",
        ".....kmmk.....",
        "...kkkkkkkk...",
        "...ky1111yk...",
        "...k111111k...",
        "...k112211k...",
        "...ky1221yk...",
        "....ky22yk....",
        "....kkkkkk....",
        "......mM......",
        "......mM......",
        "......mM......",
    ]
    names = {'k': 'k', 'm': 'm2', 'M': 'm1', '1': 'y1', '2': 'y2', 'y': 'y3'}
    for v, r in enumerate(head):
        for u, ch in enumerate(r):
            if ch != '.':
                O.put(x0 + 1 + u, y0 - 12 + v, names[ch])
    relight(x0 + 8, y0 + 14, 34, 24)
    for v in range(8, 15):
        for u in range(4, 12):
            n = G.get(x0 + u, y0 + v)
            if n and n.startswith('Ls'): G.put(x0 + u, y0 + v, n[1:])
    solid_rect(x0, y0 + 8, x0 + 16, y0 + 16)

def tree(cx, cy):
    x0, y0 = cx * 16, cy * 16
    # shade on the lawn
    for y in range(y0 - 6, y0 + 18):
        for x in range(x0 - 14, x0 + 30):
            d = math.hypot((x - (x0 + 8)) / 20.0, (y - (y0 + 7)) / 10.0)
            if d < 1 and (d < 0.7 or bayer(x, y) > (d - 0.7) / 0.3):
                n = G.get(x, y)
                if n in ('g2', 'g3', 'g4'):
                    G.put(x, y, {'g2': 'g1', 'g3': 'g2', 'g4': 'g2'}[n])
    # trunk
    for v in range(-6, 13):
        for u in range(5, 11):
            c = 't2' if u in (6, 7) else 't1'
            if u == 5 or u == 10: c = 'h1' if v < 11 else 't1'
            G.put(x0 + u, y0 + v, c)
    for (u, v) in ((4, 12), (11, 12), (3, 13), (4, 13), (11, 13), (12, 13), (5, 13), (10, 13)):
        G.put(x0 + u, y0 + v, 't1')
    solid_rect(x0 + 2, y0 + 2, x0 + 14, y0 + 16)
    # canopy
    ccx, ccy = x0 + 7.5, y0 - 13.5
    rx, ry = 21.0, 17.0
    for y in range(int(ccy - ry) - 2, int(ccy + ry) + 3):
        for x in range(int(ccx - rx) - 2, int(ccx + rx) + 3):
            ang = math.atan2(y - ccy, x - ccx)
            bump = 1 + 0.07 * math.sin(ang * 7 + cx) + 0.05 * math.sin(ang * 13 + cy)
            d = math.hypot((x - ccx) / (rx * bump), (y - ccy) / (ry * bump))
            if d > 1: continue
            k = hsh(x, y, 31)
            lightdir = ((x - ccx) / rx * -0.5 + (y - ccy) / ry * -0.8)   # lit from top-left
            t = lightdir + (k % 100) / 260.0 - 0.15
            c = 'h2'
            if t > 0.42: c = 'g3' if k % 3 == 0 else 'h3'
            elif t > 0.12: c = 'h3' if k % 2 else 'h2'
            elif t < -0.35: c = 'h1'
            elif t < -0.1: c = 'h1' if k % 2 else 'h2'
            if d > 0.93: c = 'h1'
            O.put(x, y, c)

def fountain():
    x0, y0 = 13 * 16, 9 * 16
    G.setpal(x0, y0, x0 + 64, y0 + 48, 2)
    cx, cy = x0 + 31.5, y0 + 23.5
    for y in range(y0, y0 + 48):
        for x in range(x0, x0 + 64):
            d_out = math.hypot((x - cx) / 30.0, (y - cy) / 21.5)
            d_in = math.hypot((x - cx) / 25.0, (y - (cy - 2.5)) / 15.5)
            if d_out > 1: continue
            if d_in > 1:
                c = 's2'
                if d_out > 0.93: c = 's1'
                elif y < cy - 2 and d_in < 1.13: c = 's3'
                elif y > cy + 6: c = 's2' if (x // 3 + y) % 4 else 's1'
                if y > cy and d_in < 1.07: c = 's3'
            else:
                k = hsh(x, y, 41)
                c = 'b2'
                if d_in > 0.82 and y < cy: c = 'b1'
                w = (x + 2 * y) % 12
                if (y % 4 == 1) and w < 3: c = ('c1', 'c2', 'c3')[((x + 2 * y) // 12 + y // 4) % 3]
                dd = math.hypot((x - cx) / 9.0, (y - (cy - 2)) / 5.5)
                if 0.8 < dd < 1.15 and k % 2: c = ('c1', 'c2', 'c3')[(x // 3) % 3]
            G.put(x, y, c)
    # pedestal + bowl
    for y in range(int(cy) - 14, int(cy) + 2):
        for x in range(int(cx) - 6, int(cx) + 8):
            dx = x - cx
            v = y - (cy - 14)
            if v < 5:
                if abs(dx) < 6.5 - (0 if v > 0 else 2):
                    c = 's3' if v < 2 else ('s2' if v < 4 else 's1')
                    if v == 1 and abs(dx) < 4: c = 'c3' if int(dx) % 2 else 'b3'
                    G.put(x, y, c)
            elif abs(dx) < 2.2:
                G.put(x, y, 's3' if dx < 0 else 's2')
            if v >= 13 and abs(dx) < 4.5:
                G.put(x, y, 's2' if v < 15 else 's1')
    for (dx, dy) in ((0, -17), (0, -16), (-1, -15), (1, -15), (-3, -13), (3, -13), (-4, -11), (4, -11)):
        G.put(int(cx) + dx, int(cy) + dy, 'wh')
    for ty in range(y0 // 8, (y0 + 48) // 8):
        for tx in range(x0 // 8, (x0 + 64) // 8):
            px, py = tx * 8 + 4, ty * 8 + 4
            if math.hypot((px - cx) / 29.0, (py - cy) / 20.0) < 1.0:
                SOLID[ty][tx] |= 1

def pond():
    def isw(cx, cy): return cell(cx, cy) == 'P'
    def water_at(x, y):
        cx, cy = x // 16, y // 16
        if not isw(cx, cy): return False
        u, v = x % 16, y % 16
        for sx, sy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
            if not isw(cx + sx, cy) and not isw(cx, cy + sy):
                ccx = 9.5 if sx < 0 else 5.5
                ccy = 9.5 if sy < 0 else 5.5
                if (u - ccx) * sx > 0 and (v - ccy) * sy > 0 and math.hypot(u - ccx, v - ccy) > 9.5:
                    return False
        return True
    for cy in range(MH):
        for cx in range(MW):
            if not isw(cx, cy): continue
            G.setpal(cx * 16, cy * 16, cx * 16 + 16, cy * 16 + 16, 2)
    for y in range(PH):
        for x in range(PW):
            if not water_at(x, y): continue
            near = min(1 if not water_at(x + dx, y + dy) else 9 for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (0, -2), (0, -3)))
            far = any(not water_at(x + dx, y + dy) for dx, dy in ((2, 0), (-2, 0), (0, 2), (0, -4), (1, 1), (-1, 1), (1, -1), (-1, -1)))
            if not water_at(x, y - 1) or not water_at(x - 1, y) or not water_at(x + 1, y) or not water_at(x, y + 1):
                c = 's1'
            elif near == 1 or far:
                c = 'b1'
            else:
                c = 'b2'
                w = (x + 3 * y) % 14
                if y % 5 == 2 and w < 4: c = ('c1', 'c2', 'c3')[((x + 3 * y) // 14 + y // 5) % 3]
            G.put(x, y, c)
    for (lx, ly) in ((70, 232), (96, 250), (52, 252), (84, 268)):       # lily pads
        for dy in range(-2, 3):
            for dx in range(-3, 4):
                if dx * dx + dy * dy * 2 <= 10 and not (dx > 0 and dy == 0):
                    G.put(lx + dx, ly + dy, 'g3' if dy < 1 else 'g2')
        G.put(lx - 1, ly - 1, 'f1'); G.put(lx - 2, ly - 1, 'wh')
    for ty in range(PH // 8):
        for tx in range(PW // 8):
            n = sum(1 for yy in range(8) for xx in range(8) if water_at(tx * 8 + xx, ty * 8 + yy))
            if n >= 40: SOLID[ty][tx] |= 1

def gate(cx, cy):
    x0, y0 = cx * 16, cy * 16
    for v in range(16):
        for u in range(16):
            if u % 4 == 1 or v in (2, 12):
                G.put(x0 + u, y0 + v, 's1' if (u % 4 == 1 and v not in (2, 12)) else 't1')
            if u % 4 == 1 and v == 0:
                G.put(x0 + u, y0 + v, 's3')
    solid_rect(x0, y0, x0 + 16, y0 + 16)

pond()
fountain()
for cy in range(MH):
    for cx in range(MW):
        ch = MAP[cy][cx]
        if ch == 'o': trashcan(cx, cy)
        if ch == 'G': gate(cx, cy)
# light spilling out of the windows and the door
for i, cx in enumerate((2, 5, 8, 11, 18, 21, 24, 27)):
    if i not in (1, 6):
        relight(cx * 16 + 8, 50, 15, 20, 0.8)
relight(240, 50, 26, 30, 1.0)
for cy in range(MH):
    for cx in range(MW):
        if MAP[cy][cx] == 'L': lamp(cx, cy)
for cy in range(MH):
    for cx in range(MW):
        if MAP[cy][cx] == 'T': tree(cx, cy)

# ---- encode BG tiles ----
BG_TILES = [tuple([0] * 8)]
BG_TILE_IDX = {BG_TILES[0]: 0}
warn = 0
def encode_layer(L, blank_ok):
    global warn
    out = []
    for ty in range(PH // 8):
        for tx in range(PW // 8):
            pal = L.pal[ty][tx]
            idx = BG_IDX[pal]
            words = []
            for y in range(8):
                v = 0
                for x in range(8):
                    n = L.px[ty * 8 + y][tx * 8 + x]
                    if n is None:
                        i = 0
                    else:
                        while n not in idx:
                            if n not in BG_FALLBACK:
                                raise SystemExit("no colour %s in bg pal %d at %d,%d" % (n, pal, tx, ty))
                            n = BG_FALLBACK[n]; warn += 1
                        i = idx[n]
                    v |= i << (4 * x)
                words.append(v)
            t = tuple(words)
            if t not in BG_TILE_IDX:
                BG_TILE_IDX[t] = len(BG_TILES)
                BG_TILES.append(t)
            out.append(BG_TILE_IDX[t] | (pal << 12))
    return out
MAP_G = encode_layer(G, False)
MAP_O = encode_layer(O, True)
print("bg tiles: %d  (fallback pixels: %d)" % (len(BG_TILES), warn))
assert len(BG_TILES) <= 1024

def map_preview(pals, fn):
    img = [[(0, 0, 0)] * PW for _ in range(PH)]
    for L, M in ((G, MAP_G), (O, MAP_O)):
        for ty in range(PH // 8):
            for tx in range(PW // 8):
                se = M[ty * (PW // 8) + tx]
                t = BG_TILES[se & 1023]; pal = pals[se >> 12]
                for y in range(8):
                    for x in range(8):
                        v = (t[y] >> (4 * x)) & 15
                        if v: img[ty * 8 + y][tx * 8 + x] = pal[v]
    write_png(os.path.join(PREV, fn), img, 2)
map_preview(BG_N, "map_night.png")
map_preview(BG_D, "map_dawn.png")

EXITS = []
for cy in range(MH):
    for cx in range(MW):
        if MAP[cy][cx] == 'X':
            dx = -1 if cx == 0 else (1 if cx == MW - 1 else 0)
            dy = 1 if cy == MH - 1 else 0
            EXITS.append((cx, cy, dx, dy))

# ================================================================== TITLE ==

TW, TH = 240, 160
timg = [[(0, 0, 0)] * TW for _ in range(TH)]

def tput(x, y, c):
    if 0 <= x < TW and 0 <= y < TH:
        timg[y][x] = c

SKY = [rgb(h) for h in ('070a20', '0b1030', '101840', '172250', '1f2c60', '2a3870', '374680', '46548c')]
for y in range(TH):
    f = (y / 128.0) * (len(SKY) - 1)
    for x in range(TW):
        i = int(f); t = f - i
        i2 = min(i + 1, len(SKY) - 1); i = min(i, len(SKY) - 1)
        timg[y][x] = SKY[i2] if t > bayer(x, y) else SKY[i]
# special animated colours (unique RGB so they get their own palette slots)
C_TWINK = (254, 254, 232)
C_EYE = (255, 230, 91)
C_BEYE = [(255, 231, 90), (255, 232, 90)]
for i in range(70):
    x = hsh(i, 1, 50) % TW; y = hsh(i, 2, 51) % 100
    k = hsh(i, 3, 52) % 6
    c = (200, 210, 255) if k else C_TWINK
    tput(x, y, c)
    if k == 0:
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            tput(x + dx, y + dy, (120, 130, 200))
# moon
MX, MY, MR = 196, 62, 25
for y in range(MY - 40, MY + 41):
    for x in range(MX - 40, MX + 41):
        d = math.hypot(x - MX, y - MY)
        if d <= MR:
            c = (250, 246, 210)
            if math.hypot(x - MX + 8, y - MY + 9) > 27: c = (226, 220, 180)
            for (cx, cy, r) in ((-8, -6, 5), (7, 4, 6), (-3, 11, 3), (10, -10, 3), (-13, 6, 2)):
                dd = math.hypot(x - MX - cx, y - MY - cy)
                if dd < r: c = (214, 206, 168) if dd < r - 1 or (x + y) % 2 else (236, 230, 190)
            if d > MR - 1.2: c = (200, 196, 170)
            tput(x, y, c)
        elif d <= MR + 12 and 0 <= x < TW and 0 <= y < TH:
            t = 1 - (d - MR) / 12.0
            if t * 0.8 > bayer(x, y):
                tput(x, y, mix(timg[y][x], (150, 160, 220), 0.45))

# hedge silhouette along the bottom
def hedge_top(x):
    return 122 + int(3 * math.sin(x * 0.09) + 2 * math.sin(x * 0.23 + 1) + 1.5 * math.sin(x * 0.61))

# raccoon peeking over the hedge
def ell(x, y, cx, cy, rx, ry):
    return ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2

def raccoon(cx, cy, s=1.0, eyes=C_EYE):
    FUR = (141, 138, 153); FURD = (94, 91, 110); FURL = (185, 182, 196)
    MASK = (37, 34, 46); WH = (244, 242, 248); OL = (16, 12, 24)
    shape = {}
    def fill(test, col, x0=-60, x1=60, y0=-60, y1=60, only=None):
        for yy in range(int(y0 * s), int(y1 * s) + 1):
            for xx in range(int(x0 * s), int(x1 * s) + 1):
                if test(xx / s, yy / s):
                    if only is None or shape.get((xx, yy)) in only:
                        shape[(xx, yy)] = col
    # ears
    for sx in (-1, 1):
        fill(lambda x, y: ell(x, y, sx * 27, -24, 11, 13) < 1, FURD)
        fill(lambda x, y: ell(x, y, sx * 27, -22, 6, 8) < 1, (229, 138, 160))
    # head
    fill(lambda x, y: ell(x, y, 0, 0, 38, 28) < 1, FUR)
    for sx in (-1, 1):   # cheek fluff
        fill(lambda x, y: ell(x, y, sx * 34, 10, 14, 11) < 1, FURL)
        fill(lambda x, y: ell(x, y, sx * 40, 16, 8, 5) < 1, FURL)
    fill(lambda x, y: ell(x, y, 0, -18, 5, 12) < 1, FURD)                      # forehead stripe
    for sx in (-1, 1):
        fill(lambda x, y: ell(x, y, sx * 17, -13, 13, 6) < 1, WH, only=(FUR, FURD))   # brows
        fill(lambda x, y: ell(x - sx * 16, y + 1 - (x * sx - 16) * 0.22, 0, 0, 17, 9.5) < 1, MASK)  # mask
    fill(lambda x, y: ell(x, y, 0, -1, 6, 6) < 1, MASK)
    fill(lambda x, y: ell(x, y, 0, 13, 15, 11) < 1, WH)                         # muzzle
    fill(lambda x, y: ell(x, y, 0, 6, 6, 4) < 1, OL)                            # nose
    fill(lambda x, y: ell(x, y, -2, 5, 2, 1) < 1, (120, 110, 130))
    fill(lambda x, y: abs(x) < 0.8 and 9 < y < 15, OL)
    fill(lambda x, y: 0.75 < ell(x, y, 0, 13, 7, 5) < 1.25 and y > 14, OL)      # smile
    for sx in (-1, 1):   # eyes
        fill(lambda x, y: ell(x, y, sx * 15, -2, 6.5, 6.5) < 1, eyes)
        fill(lambda x, y: ell(x, y, sx * 15 + 1, -1, 3, 4.5) < 1, OL)
        fill(lambda x, y: ell(x, y, sx * 15 - 1, -4, 1.6, 1.6) < 1, WH)
    # shading: darker under-chin
    for (xx, yy), c in list(shape.items()):
        if c == FUR and yy / s > 14 and (xx + yy) % 2 == 0: shape[(xx, yy)] = FURD
    for (xx, yy), c in shape.items():
        tput(cx + xx, cy + yy, c)
    for (xx, yy) in list(shape.keys()):
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            if (xx + dx, yy + dy) not in shape:
                tput(cx + xx + dx, cy + yy + dy, OL)

raccoon(78, 103, 1.0)
raccoon(182, 117, 0.5)
raccoon(222, 121, 0.38)

# paws gripping the hedge + hedge itself
HED = [rgb(h) for h in ('071a18', '0b2620', '12342a', '1a4634')]
for x in range(TW):
    top = hedge_top(x)
    for y in range(top, TH):
        k = hsh(x, y, 61)
        d = y - top
        c = HED[1]
        if d < 2: c = HED[3] if k % 3 else HED[2]
        elif d < 7: c = HED[2] if k % 3 else HED[3 if k % 5 == 0 else 1]
        elif k % 7 == 0: c = HED[2]
        elif k % 5 == 0: c = HED[0]
        if d > 22: c = HED[0] if k % 3 else HED[1]
        tput(x, y, c)
for px in (48, 108):
    for y in range(-5, 5):
        for x in range(-8, 9):
            if ell(x, y, 0, 0, 8, 5) < 1:
                yy = hedge_top(px) + y - 1
                c = (94, 91, 110) if ell(x, y, 0, 0, 8, 5) < 0.7 else (16, 12, 24)
                if y > 1 and x % 4 == 0: c = (16, 12, 24)
                tput(px + x, yy, c)
# glowing eyes in the bushes (two groups that blink independently)
for i, (ex, ey) in enumerate(((14, 138), (206, 151), (30, 152), (232, 141), (190, 139), (58, 141))):
    for sx in (0, 5):
        for (dx, dy) in ((0, 0), (1, 0), (0, 1), (1, 1)):
            tput(ex + sx + dx, ey + dy, C_BEYE[i % 2])

# logo
GLYPH = {
    'P': ["XXXX.", "X...X", "X...X", "XXXX.", "X....", "X....", "X...."],
    'A': [".XXX.", "X...X", "X...X", "XXXXX", "X...X", "X...X", "X...X"],
    'W': ["X...X", "X...X", "X...X", "X.X.X", "X.X.X", "XX.XX", "X...X"],
    'S': [".XXXX", "X....", "X....", ".XXX.", "....X", "....X", "XXXX."],
    'O': [".XXX.", "X...X", "X...X", "X...X", "X...X", "X...X", ".XXX."],
    'F': ["XXXXX", "X....", "X....", "XXXX.", "X....", "X....", "X...."],
    '!': ["X", "X", "X", "X", "X", ".", "X"],
}
def logo(text, x0, y0, sc=4, rad=2.3):
    solid = set()
    x = x0
    for i, ch in enumerate(text):
        if ch == ' ':
            x += 12; continue
        g = GLYPH[ch]
        yo = y0 + (0, 3, 1, -1, 0, 2, -1, 1, 0)[i % 9]
        for gy, row in enumerate(g):
            for gx, c in enumerate(row):
                if c == 'X':
                    for py in range(sc):
                        for px in range(sc):
                            solid.add((x + gx * sc + px, yo + gy * sc + py))
        x += len(g[0]) * sc + 7
    fat = set()
    r = int(math.ceil(rad))
    for (px, py) in solid:
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                if dx * dx + dy * dy <= rad * rad:
                    fat.add((px + dx, py + dy))
    ol = set()
    for (px, py) in fat:
        for dy in range(-2, 3):
            for dx in range(-2, 3):
                if dx * dx + dy * dy <= 5 and (px + dx, py + dy) not in fat:
                    ol.add((px + dx, py + dy))
    for (px, py) in ol | fat:
        tput(px + 2, py + 3, (6, 6, 20))
    for (px, py) in ol:
        tput(px, py, (44, 16, 52))
    ys = [p[1] for p in fat]; ymin, ymax = min(ys), max(ys)
    grad = [rgb(h) for h in ('fffbd0', 'fff3a0', 'ffe060', 'ffd23e', 'ffb830', 'f89a28', 'e87a20')]
    for (px, py) in fat:
        f = (py - ymin) / float(ymax - ymin) * (len(grad) - 1)
        i = int(f); t = f - i
        c = grad[min(i + 1, len(grad) - 1)] if t > bayer(px, py) else grad[i]
        if (px - 1, py - 1) not in fat or (px, py - 1) not in fat: c = (255, 255, 240)
        elif (px + 1, py + 1) not in fat or (px, py + 1) not in fat: c = (200, 100, 30)
        tput(px, py, c)
    return x
logo("PAWS OFF!", 13, 12)
# sparkles on the logo
for (sx, sy, r) in ((30, 12, 3), (118, 44, 2), (214, 14, 3), (160, 9, 2)):
    for d in range(-r, r + 1):
        tput(sx + d, sy, (255, 255, 255)); tput(sx, sy + d, (255, 255, 255))
    tput(sx, sy, C_TWINK)

# quantise (exact colours; we kept the count small)
TPAL = [(0, 0, 0), C_TWINK, C_EYE, (255, 255, 255), (12, 12, 28), (255, 210, 62), (255, 106, 138), C_BEYE[0], C_BEYE[1]]
TIDX = {c: i for i, c in enumerate(TPAL)}
TBMP = []
for y in range(TH):
    for x in range(TW):
        c = timg[y][x]
        if c not in TIDX:
            TIDX[c] = len(TPAL); TPAL.append(c)
        TBMP.append(TIDX[c])
print("title colours:", len(TPAL))
assert len(TPAL) <= 256
write_png(os.path.join(PREV, "title.png"), timg, 2)

# ============================================================== HUD TILES ==
HUD_LEG = {'.': 'bar', 'y': 'au', 'Y': 'ad', 'L': 'al', 's': 'si', 'S': 'sd', 'c': 'cy', 'r': 'rd',
           'm': 'mo', 'o': 'or', 'k': 'sh', 'w': 'ink', 'p': 'pk'}
def hud_tile(text):
    rows = text.strip('\n').split('\n')
    assert len(rows) == 8 and all(len(r) == 8 for r in rows), text
    words = []
    for r in rows:
        v = 0
        for x, ch in enumerate(r):
            v |= BG_IDX[4][HUD_LEG[ch]] << (4 * x)
        words.append(v)
    return words
HUD_TILES = []
for t in ("""
........
........
........
........
........
........
........
........
""", """
........
..yyyy..
.yLLyyY.
.yLyyyY.
.yyyyyY.
.yyyyYY.
..YYYY..
........
""", """
...SS...
..swsS..
.swscsS.
.sscsSS.
.scsSpS.
..sSSS..
...SS...
........
""", """
........
........
........
S.S.S.S.
........
........
........
........
""", """
...o....
.o.o.o..
..yyy...
ooyLyoo.
..yyy...
.o.o.o..
...o....
........
"""):
    HUD_TILES += hud_tile(t)

# ================================================================== MUSIC ==

NOTE_NAMES = {'c': 0, 'cs': 1, 'd': 2, 'ds': 3, 'e': 4, 'f': 5, 'fs': 6, 'g': 7, 'gs': 8, 'a': 9, 'as': 10, 'b': 11}
def note_num(tok):
    name, octv = tok[:-1], int(tok[-1])
    return (octv - 2) * 12 + NOTE_NAMES[name] + 1          # c2 == 1

def track(text, drums=False):
    out = []
    total = 0
    for tok in text.split():
        if tok == '|': continue
        n, _, l = tok.partition(':')
        l = int(l) if l else 1
        if drums:
            v = {'r': 0, 'h': 1, 's': 2, 'k': 3, 'o': 4}[n]
        else:
            v = 0 if n == 'r' else note_num(n)
        out += [v, l]; total += l
    return out + [255, 255], total

SONGS = {}
def song(name, tempo, loop, mel, bass, drums):
    m, a = track(mel); b, bb = track(bass); d, c = track(drums, True)
    assert a == bb == c, "%s: track lengths differ: %d %d %d" % (name, a, bb, c)
    SONGS[name] = (tempo, loop, m, b, d)

# --- "Sneaky Paws": the in-game loop (A minor, pizzicato) ---
song('game', 7, 1,
     mel="""
     a4:2 c5:2 e5:2 r:2 ds5 e5 r:2 c5:2 r:2 |
     a4:2 b4:2 c5:2 r:2 b4 c5 r:2 a4:2 r:2 |
     d5:2 f5:2 a5:2 r:2 gs5 a5 r:2 f5:2 r:2 |
     e5:2 d5:2 c5:2 b4:2 a4:4 r:4 |
     f4:2 a4:2 c5:2 r:2 f5:2 r:2 c5:2 r:2 |
     d5:2 r:2 f5:2 r:2 a5:2 g5:2 f5:2 d5:2 |
     e5:2 gs5:2 b5:2 r:2 gs5 b5 r:2 e5:2 r:2 |
     e5 f5 e5 d5 c5 b4 gs4:2 e4:4 r:4 |
     a5:2 r:2 e5:2 r:2 c5 d5 e5:2 a4:2 r:2 |
     g5:2 r:2 e5:2 r:2 b4 c5 d5:2 g4:2 r:2 |
     f5:2 r:2 c5:2 r:2 a4 b4 c5:2 f4:2 r:2 |
     e5 r e5 r gs5 r b5 r e6:4 r:4 |
     a4:2 c5:2 e5:2 r:2 ds5 e5 r:2 c5:2 r:2 |
     a4:2 b4:2 c5:2 r:2 b4 c5 r:2 a4:2 r:2 |
     d5 e5 f5 e5 d5:2 r:2 e5 f5 gs5 f5 e5:2 r:2 |
     a5:2 e5:2 c5:2 e5:2 a4:4 r:4
     """,
     bass="""
     a2:2 r:2 e3:2 r:2 a2:2 r:2 e3:2 r:2 | a2:2 r:2 e3:2 r:2 a2:2 r:2 c3:2 e3:2 |
     d3:2 r:2 a2:2 r:2 d3:2 r:2 a2:2 r:2 | a2:2 r:2 e3:2 r:2 a2:2 a2:2 g2:2 e2:2 |
     f2:2 r:2 c3:2 r:2 f2:2 r:2 c3:2 r:2 | d3:2 r:2 a2:2 r:2 d3:2 r:2 f3:2 d3:2 |
     e2:2 r:2 b2:2 r:2 e2:2 r:2 b2:2 r:2 | e2:2 r:2 gs2:2 r:2 b2:2 gs2:2 e2:2 r:2 |
     a2:2 r:2 e3:2 r:2 a2:2 r:2 e3:2 r:2 | g2:2 r:2 d3:2 r:2 g2:2 r:2 d3:2 r:2 |
     f2:2 r:2 c3:2 r:2 f2:2 r:2 c3:2 r:2 | e2:2 r:2 e2:2 r:2 e2:2 e3:2 e2:2 r:2 |
     a2:2 r:2 e3:2 r:2 a2:2 r:2 e3:2 r:2 | a2:2 r:2 e3:2 r:2 a2:2 r:2 c3:2 e3:2 |
     d3:2 r:2 a2:2 r:2 e2:2 r:2 b2:2 r:2 | a2:2 e3:2 a3:2 e3:2 a2:4 r:4
     """,
     drums=("k:2 h:2 s:2 h:2 k:2 h:2 s:2 h h | " * 3 + "k:2 h:2 s:2 h:2 k:2 k:2 s:2 s s | ") * 4)

# --- title: slow and moonlit ---
song('title', 10, 1,
     mel="""
     e5:8 c5:4 a4:4 | f5:8 c5:4 a4:4 | g5:6 f5:2 e5:4 d5:4 | e5:12 r:4 |
     a5:8 e5:4 c5:4 | f5:6 e5:2 d5:4 c5:4 | b4:4 d5:4 gs5:4 b5:4 | a5:12 r:4
     """,
     bass="""
     a2:2 e3:2 a3:2 e3:2 c4:2 e3:2 a3:2 e3:2 | f2:2 c3:2 f3:2 c3:2 a3:2 c3:2 f3:2 c3:2 |
     g2:2 d3:2 g3:2 d3:2 b3:2 d3:2 g3:2 d3:2 | e2:2 b2:2 e3:2 b2:2 gs3:2 b2:2 e3:2 b2:2 |
     a2:2 e3:2 a3:2 e3:2 c4:2 e3:2 a3:2 e3:2 | d3:2 a3:2 d4:2 a3:2 f3:2 a3:2 d4:2 a3:2 |
     e2:2 b2:2 e3:2 b2:2 gs3:2 b2:2 e3:2 b2:2 | a2:2 e3:2 a3:2 e3:2 a2:4 r:4
     """,
     drums="r:16 | " * 4 + "h:4 r:4 h:4 r:4 | " * 4)

song('dawn', 6, 0,
     mel="e5:2 gs5:2 b5:2 e6:4 r:2 b5:2 e6:8 r:10",
     bass="e3:2 r:2 b3:2 e3:4 r:2 b3:2 e3:8 r:10",
     drums="r:4 s s h:2 k:4 r:2 s s k:8 r:8")
song('win', 6, 0,
     mel="c5:2 e5:2 g5:2 c6:2 r:2 g5:2 c6:2 e6:2 d6:2 b5:2 g5:2 d6:2 c6:8 r:8",
     bass="c3:4 g3:4 c3:4 e3:4 g2:4 b2:4 c3:8 r:8",
     drums="k:2 h:2 s:2 h:2 k:2 h:2 s:2 h:2 k:2 h:2 s:2 s s k:8 r:8")
song('lose', 12, 0,
     mel="a4:2 g4:2 f4:2 e4:2 d4:2 c4:2 b3:2 gs3:2 a3:8 r:8",
     bass="a2:4 f2:4 d2:4 e2:4 a2:8 r:8",
     drums="r:16 k:8 r:8")
song('victory', 7, 0,
     mel="""c5 c5 c5:2 e5:2 g5:2 c6:4 r:2 a5:2 | f5 f5 f5:2 a5:2 c6:2 f6:4 r:2 e6:2 |
            d6:2 b5:2 g5:2 b5:2 d6:2 g6:2 f6:2 d6:2 | c6:2 e6:2 g6:2 e6:2 c6:8 | r:16""",
     bass="""c3:2 r:2 g3:2 r:2 c3:2 r:2 e3:2 r:2 | f2:2 r:2 c3:2 r:2 f2:2 r:2 a2:2 r:2 |
             g2:2 r:2 d3:2 r:2 g2:2 r:2 b2:2 r:2 | c3:2 g3:2 c4:2 g3:2 c3:8 | r:16""",
     drums="k:2 h:2 s:2 h:2 " * 6 + "k:2 s:2 k:2 s s k:8 | r:16")

RATES = [0]
for n in range(1, 85):
    f = 65.4064 * 2 ** ((n - 1) / 12.0)
    RATES.append(max(0, min(2047, 2048 - int(round(131072.0 / f)))))

# ================================================================= OUTPUT ==

def carr(name, typ, vals, per=12, fmt="0x%X"):
    s = "const %s %s[%d] = {\n" % (typ, name, len(vals))
    for i in range(0, len(vals), per):
        s += "\t" + ",".join(fmt % v for v in vals[i:i + per]) + ",\n"
    return s + "};\n\n"

def flat_pal(pals):
    out = []
    for p in pals:
        out += [to15(c) for c in p]
    return out

def flat_tiles(tiles):
    out = []
    for t in tiles: out += list(t)
    return out

h = "// Generated by tools/mkgfx.py -- do not edit.\n#ifndef GFX_H\n#define GFX_H\n#include <tonc_types.h>\n\n"
c = "// Generated by tools/mkgfx.py -- do not edit.\n#include \"gfx.h\"\n\n"

h += "#define MAP_W %d\n#define MAP_H %d\n#define MAP_TW %d\n#define MAP_TH %d\n" % (MW, MH, MW * 2, MH * 2)
h += "#define GFX_BG_NTILES %d\n#define GFX_OBJ_NTILES %d\n#define GFX_NBGPAL %d\n#define GFX_NOBJPAL %d\n" % (
    len(BG_TILES), len(OBJ_TILES), len(BGPALS), len(OBJPALS))
h += "#define NEXITS %d\n\n" % len(EXITS)
for name, idx in OBJ_DEFS:
    h += "#define OT_%s %d\n" % (name, idx)
h += "\n"

def decl(name, typ, vals, **kw):
    global h, c
    h += "extern const %s %s[%d];\n" % (typ, name, len(vals))
    c += carr(name, typ, vals, **kw)

decl("gfx_bg_tiles", "u32", flat_tiles(BG_TILES), per=8, fmt="0x%08X")
decl("gfx_obj_tiles", "u32", flat_tiles(OBJ_TILES), per=8, fmt="0x%08X")
decl("gfx_map_ground", "u16", MAP_G, per=16, fmt="0x%04X")
decl("gfx_map_over", "u16", MAP_O, per=16, fmt="0x%04X")
decl("gfx_solid", "u8", [v for r in SOLID for v in r], per=30, fmt="%d")
decl("gfx_bgpal_night", "u16", flat_pal(BG_N), per=16, fmt="0x%04X")
decl("gfx_bgpal_dawn", "u16", flat_pal(BG_D), per=16, fmt="0x%04X")
decl("gfx_objpal_night", "u16", flat_pal(OBJ_N), per=16, fmt="0x%04X")
decl("gfx_objpal_dawn", "u16", flat_pal(OBJ_D), per=16, fmt="0x%04X")
decl("gfx_exits", "s8", [v for e in EXITS for v in e], per=4, fmt="%d")
tp = [to15(x) for x in TPAL] + [0] * (256 - len(TPAL))
decl("gfx_title_pal", "u16", tp, per=16, fmt="0x%04X")
words = []
for i in range(0, len(TBMP), 4):
    words.append(TBMP[i] | (TBMP[i + 1] << 8) | (TBMP[i + 2] << 16) | (TBMP[i + 3] << 24))
decl("gfx_title_bmp", "u32", words, per=8, fmt="0x%08X")
decl("gfx_hud_tiles", "u32", HUD_TILES, per=8, fmt="0x%08X")
decl("snd_rates", "u16", RATES, per=12, fmt="%d")
h += "\ntypedef struct { u8 tempo, loop; const u8 *mel, *bass, *drums; } Song;\n"
names = list(SONGS.keys())
for i, n in enumerate(names):
    h += "#define SONG_%s %d\n" % (n.upper(), i)
    tempo, loop, m, b, d = SONGS[n]
    c += carr("song_%s_mel" % n, "static u8", m, per=24, fmt="%d")
    c += carr("song_%s_bass" % n, "static u8", b, per=24, fmt="%d")
    c += carr("song_%s_drums" % n, "static u8", d, per=24, fmt="%d")
h += "extern const Song songs[%d];\n" % len(names)
c += "const Song songs[%d] = {\n" % len(names)
for n in names:
    tempo, loop = SONGS[n][0], SONGS[n][1]
    c += "\t{ %d, %d, song_%s_mel, song_%s_bass, song_%s_drums },\n" % (tempo, loop, n, n, n)
c += "};\n"
h += "\n#endif\n"

with open(os.path.join(ROOT, "source", "gfx.h"), "w") as f: f.write(h)
with open(os.path.join(ROOT, "source", "gfx.c"), "w") as f: f.write(c)
print("obj tiles: %d, sprites: %d, exits: %d" % (len(OBJ_TILES), len(SPRITES), len(EXITS)))
