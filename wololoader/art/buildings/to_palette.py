# Converts the Blender renders into an AoE2-style indexed sprite:
#   colour pixels -> nearest colour of the game palette 50500 (no dithering),
#   magenta pixels -> player colour (palette 16..23 ramp for player 1 = blue, as the game draws it),
#   ground shadow (alpha of the shadow catcher outside the building) -> shadow mask.
# Usage: python3 -I to_palette.py RENDER_DIR PALETTE.bina OUT_PREFIX
# Writes OUT_PREFIX_preview.png (on grass, 2x), OUT_PREFIX_index.png (palette image) and
# OUT_PREFIX_masks.png (R = player colour level 0..7 * 32, G = shadow, B = building).
import sys
from PIL import Image, ImageEnhance

rdir, palpath, out = sys.argv[1], sys.argv[2], sys.argv[3]
lines = open(palpath).read().split("\n")
pal = [tuple(int(v) for v in l.split()[:3]) for l in lines[3:259]]

from PIL import ImageFilter
info = open(rdir + "/abbey_info.txt").read().split()
BW, BH = int(info[0]), int(info[1])
def load(name):   # renders are SS times bigger: shrink with a good filter, then sharpen a little
    im = Image.open(rdir + "/" + name).convert("RGBA")
    if im.size != (BW, BH):
        im = im.resize((BW, BH), Image.LANCZOS)
    return im
full = load("abbey_full.png")
col_big = Image.open(rdir + "/abbey_color.png").convert("RGBA")
# "Photoshop clean-up" step of the original pipeline (render -> 256 colours -> 2D artist sharpened
# details and cleaned edges): dark contour lines where the big render's brightness or alpha jumps,
# drawn at render size so they shrink to soft 1 px lines.
from PIL import ImageChops
nrm = Image.open(rdir + "/abbey_normal.png").convert("RGB")
ne = [ch.filter(ImageFilter.FIND_EDGES) for ch in nrm.split()]
edges = ImageChops.lighter(ImageChops.lighter(ne[0], ne[1]), ne[2])
edges = ImageChops.lighter(edges, col_big.split()[3].filter(ImageFilter.FIND_EDGES))
edges = edges.point(lambda v: 255 if v > 60 else 0)
dark = Image.new("RGBA", col_big.size, (12, 9, 6, 255))
mask = ImageChops.multiply(edges, col_big.split()[3]).point(lambda v: v * 6 // 10)
col_big = Image.composite(dark, col_big, mask)
col = col_big.resize((BW, BH), Image.LANCZOS) if col_big.size != (BW, BH) else col_big
col = Image.merge("RGBA", (*col.convert("RGB").filter(ImageFilter.UnsharpMask(1.2, 60, 2)).split(), col.split()[3]))
rgb = ImageEnhance.Contrast(ImageEnhance.Color(col.convert("RGB")).enhance(1.05)).enhance(1.12)
W, H = col.size

# usable palette entries for sprite colours: skip 0 (transparent), player colour ramps 16..23 etc.
usable = [i for i in range(1, 256) if not (16 <= i <= 23)]
def lab(c):  # sRGB -> CIE Lab (D65): distances there match what the eye sees
    def lin(v):
        v /= 255.0
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = (lin(v) for v in c)
    X = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047
    Y = 0.2126 * r + 0.7152 * g + 0.0722 * b
    Z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883
    f = lambda t: t ** (1 / 3) if t > 0.008856 else 7.787 * t + 16 / 116
    return (116 * f(Y) - 16, 500 * (f(X) - f(Y)), 200 * (f(Y) - f(Z)))
pal_lab = {i: lab(pal[i]) for i in range(256)}
cache = {}
def nearest(c):
    if c in cache: return cache[c]
    L = lab(c)
    best, bd = 0, 1e30
    for i in usable:
        p = pal_lab[i]
        d = (p[0] - L[0]) ** 2 + (p[1] - L[1]) ** 2 + (p[2] - L[2]) ** 2
        if d < bd: bd, best = d, i
    cache[c] = best
    return best

# dark rim: building pixels next to transparency get darker, like the originals' edges
alpha = col.split()[3]
def edge(x, y):
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        xx, yy = x + dx, y + dy
        if 0 <= xx < W and 0 <= yy < H and alpha.getpixel((xx, yy)) < 128: return True
    return False
for y in range(H):
    for x in range(W):
        if alpha.getpixel((x, y)) >= 128 and edge(x, y):
            r, g, b = rgb.getpixel((x, y)); rgb.putpixel((x, y), (r * 6 // 10, g * 6 // 10, b * 6 // 10))
idx = Image.new("P", (W, H), 0); idx.putpalette([v for p in pal for v in p])
masks = Image.new("RGB", (W, H), (0, 0, 0))
prev = Image.new("RGBA", (W, H), (92, 110, 48, 255))
for y in range(H):
    for x in range(W):
        a = col.getpixel((x, y))[3]
        if a >= 128:
            r, g, b = rgb.getpixel((x, y))
            raw = col.getpixel((x, y))
            if raw[0] > 2 * raw[1] + 20 and raw[2] > 2 * raw[1] + 20:   # magenta (lit or shaded): player colour
                level = min(5, max(1, (raw[0] + raw[2]) // 2 * 8 // 256))
                masks.putpixel((x, y), (level * 32, 0, 255))
                pc = pal[16 + level]
                prev.putpixel((x, y), pc + (255,))
                idx.putpixel((x, y), 16 + level)
            else:
                i = nearest((r >> 2 << 2, g >> 2 << 2, b >> 2 << 2))
                idx.putpixel((x, y), i)
                masks.putpixel((x, y), (0, 0, 255))
                prev.putpixel((x, y), pal[i] + (255,))
        else:
            sa = full.getpixel((x, y))[3]
            if sa > 60:                                                  # ground shadow
                masks.putpixel((x, y), (0, 255, 0))
                pr = prev.getpixel((x, y))
                prev.putpixel((x, y), (pr[0] * 55 // 100, pr[1] * 55 // 100, pr[2] * 55 // 100, 255))
idx.save(out + "_index.png"); masks.save(out + "_masks.png")
prev.resize((W * 2, H * 2), Image.NEAREST).save(out + "_preview.png")
print("ok", W, H)
