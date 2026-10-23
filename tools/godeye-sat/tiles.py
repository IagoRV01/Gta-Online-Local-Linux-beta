"""GodEye's satellite map, step 2: cut render.mjs's 1024 m blocks into the 2D map's tiles.

  python tools/godeye-sat/tiles.py [godeye/data/sat]

Level L is 2^L metres a pixel (0 = 1 m, the blocks' own scale, up to 5 = 32 m). A tile is 256 px; tile (tx, ty) of
level L covers x from tx*256*2^L and y from ty*256*2^L (y grows north). Tiles are WebP with alpha: the sea stays
transparent and all-sea tiles are not written. index.json lists every tile that exists, per level.
"""
import json, os, re, sys, time
from PIL import Image

SAT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..", "godeye", "data", "sat"))
BLOCKS = os.path.join(SAT, "blocks")
LEVELS = 6
T = 256

def save(img, level, tx, ty, have):
    if img.getextrema()[3][1] == 0:   # all transparent: open sea
        return
    d = os.path.join(SAT, str(level))
    os.makedirs(d, exist_ok=True)
    img.save(os.path.join(d, f"{tx}_{ty}.webp"), "WEBP", quality=82, method=4)
    have[level].add((tx, ty))

def main():
    t0 = time.time()
    have = [set() for _ in range(LEVELS)]
    cache = {}   # level -> {(tx, ty): Image} for the level being built from
    # level 0: each 1024 px block is 4 x 4 tiles; image row 0 is the block's north edge
    level0 = {}
    for f in sorted(os.listdir(BLOCKS)):
        m = re.match(r"b_(-?\d+)_(-?\d+)\.png$", f)
        if not m:
            continue
        bx, by = int(m[1]), int(m[2])
        img = Image.open(os.path.join(BLOCKS, f)).convert("RGBA")
        for i in range(4):
            for j in range(4):
                tile = img.crop((i * T, j * T, (i + 1) * T, (j + 1) * T))
                tx, ty = bx * 4 + i, by * 4 + (3 - j)
                level0[(tx, ty)] = tile
                save(tile, 0, tx, ty, have)
    cache = level0
    for level in range(1, LEVELS):
        parents = {}
        for (tx, ty) in cache:
            parents.setdefault((tx >> 1, ty >> 1), []).append((tx, ty))   # >> floors negatives too
        nxt = {}
        for (px, py), kids in parents.items():
            big = Image.new("RGBA", (T * 2, T * 2), (0, 0, 0, 0))
            for (tx, ty) in kids:
                a, b = tx - px * 2, ty - py * 2
                big.paste(cache[(tx, ty)], (a * T, (1 - b) * T))
            small = big.resize((T, T), Image.LANCZOS)
            nxt[(px, py)] = small
            save(small, level, px, py, have)
        cache = nxt
        print(f"level {level}: {len(have[level])} tiles", flush=True)
    index = {"version": 1, "built": time.strftime("%Y%m%d%H%M%S"), "tile": T, "levels": LEVELS,
             "tiles": [sorted(f"{x}_{y}" for x, y in lv) for lv in have]}
    json.dump(index, open(os.path.join(SAT, "index.json"), "w"))
    size = sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(SAT) for f in fs if f.endswith(".webp"))
    print(f"level 0: {len(have[0])} tiles; {sum(map(len, have))} tiles, {size / 1048576:.0f} MB, {time.time() - t0:.0f} s")

main()
