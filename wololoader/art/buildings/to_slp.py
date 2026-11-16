# Indexed render (to_palette.py output) -> SLP for the game.
# Usage: python3 -I to_slp.py RENDER_DIR PREFIX OUT.slp
# Player colour pixels store their shade 0..7 (dark..light; the game adds the player's ramp),
# like the original building sprites (checked on 281.slp: values 0..7). Ground shadow pixels use
# the SLP shadow command. The image is cropped to the used area and the hotspot moved with it.
import sys, os
from PIL import Image
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
import slpwrite

rdir, prefix, out = sys.argv[1], sys.argv[2], sys.argv[3]
info = open(os.path.join(rdir, prefix + "_info.txt")).read().split()
hx, hy = int(info[2]), int(info[3])
idx = Image.open(os.path.join(rdir, prefix + "_index.png"))
masks = Image.open(os.path.join(rdir, prefix + "_masks.png")).convert("RGB")
W, H = idx.size
rows = []
for y in range(H):
    row = []
    for x in range(W):
        r, g, b = masks.getpixel((x, y))
        if b == 255:
            i = idx.getpixel((x, y))
            row.append(("p", i - 16) if r > 0 or 16 <= i <= 23 else ("c", i))
        elif g == 255:
            row.append(("s", 0))
        else:
            row.append(None)
    rows.append(row)
# crop
ys = [y for y in range(H) if any(rows[y])]
xs = [x for x in range(W) if any(rows[y][x] for y in ys)]
x0, x1, y0, y1 = min(xs), max(xs) + 1, min(ys), max(ys) + 1
rows = [r[x0:x1] for r in rows[y0:y1]]
open(out, "wb").write(slpwrite.encode([(rows, hx - x0, hy - y0)]))
print("wrote", out, "size", x1 - x0, y1 - y0, "hotspot", hx - x0, hy - y0)
