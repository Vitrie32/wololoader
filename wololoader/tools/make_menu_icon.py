#!/usr/bin/env python3
"""Make the main-menu "Mods" button images with the Wololoader logo instead of
the Steam Workshop gear.

Reads the player's own button images (up / down / hover-up / hover-down,
159x129) from the game, covers the gear with the button's own wood (a gear-free
vertical strip at the left of the inner panel, stretched sideways: the grain is
horizontal, so it still looks like the same plank) and draws the logo on top.
Writes them into <game>/wololoader/resources/..., which files.c serves instead of
the game's files (asset override of the loader itself). Nothing from the game is
stored in the repo; this runs at `make install`.

usage: make_menu_icon.py <game dir> <mark png: white W + waves on transparent, ~62 px high>
"""
import os
import sys

import random

from PIL import Image, ImageChops, ImageFilter

REL = os.path.join("resources", "_common", "textures", "ui", "main-menu", "default", "mod-manager")
INNER = (41, 47, 117, 111)   # inner wood panel of the button (x0, y0, x1, y1), measured on up.png
STRIP = (42, 49)             # gear-free columns inside the panel


def rusty_mark(mask_png, gear):
    """Draw the mark (white shape on transparent) as rusty metal like the
    game's own gear icon: colours sampled from the gear, light from the top
    left, dark rim, drop shadow on the wood."""
    a = Image.open(mask_png).convert("RGBA").getchannel("A")
    w, h = a.size
    # Rust palette from the gear pixels of the original button.
    # Rust = clearly redder than the yellowish wood (r much bigger than g).
    px = [p for p in gear.get_flattened_data() if p[3] > 200 and p[0] > p[1] * 1.4 and p[0] > 80]
    px.sort(key=lambda p: sum(p[:3]))
    dark, mid, light = px[len(px) // 10], px[len(px) // 2], px[len(px) * 9 // 10]
    rnd = random.Random(7)
    body = Image.new("RGBA", (w, h))
    for y in range(h):
        t = y / max(1, h - 1)
        for x in range(w):
            c = [int(light[i] * (1 - t) + dark[i] * t) for i in range(3)]
            n = rnd.randint(-18, 18)                     # pitted rust
            c = [max(0, min(255, v + n)) for v in c]
            body.putpixel((x, y), (c[0], c[1], c[2], 255))
    # Bevel: lighter top-left edge, darker bottom-right edge.
    hi = ImageChops.subtract(a, a.transform(a.size, Image.AFFINE, (1, 0, -1, 0, 1, -1)))
    lo = ImageChops.subtract(a, a.transform(a.size, Image.AFFINE, (1, 0, 1, 0, 1, 1)))
    body = Image.composite(Image.new("RGBA", a.size, light[:3] + (255,)), body, hi)
    rim = ImageChops.subtract(a, a.filter(ImageFilter.MinFilter(3)))
    body = Image.composite(Image.new("RGBA", a.size, (40, 22, 10, 255)), body, rim)
    body = Image.composite(Image.new("RGBA", a.size, (60, 32, 14, 255)), body, lo)
    body.putalpha(a)
    # Drop shadow (offset 2,2, soft), like the gear sitting on the plank.
    pad = 4
    out = Image.new("RGBA", (w + pad * 2, h + pad * 2))
    sh = Image.new("RGBA", out.size, (20, 12, 4, 0))
    sh.putalpha(Image.new("L", out.size, 0))
    sa = Image.new("L", out.size, 0)
    sa.paste(a, (pad + 2, pad + 2))
    sh.putalpha(sa.filter(ImageFilter.GaussianBlur(1.5)).point(lambda v: v * 170 // 255))
    out.alpha_composite(sh)
    out.alpha_composite(body, (pad, pad))
    return out


def main():
    game, mark_path = sys.argv[1], sys.argv[2]
    out_dir = os.path.join(game, "wololoader", REL)
    os.makedirs(out_dir, exist_ok=True)
    x0, y0, x1, y1 = INNER
    gear = Image.open(os.path.join(game, REL, "up.png")).convert("RGBA").crop(INNER)
    mark = rusty_mark(mark_path, gear)
    for name in ("up", "down", "hover-up", "hover-down"):
        im = Image.open(os.path.join(game, REL, name + ".png")).convert("RGBA")
        wood = im.crop((STRIP[0], y0, STRIP[1], y1)).resize((x1 - x0, y1 - y0), Image.BILINEAR)
        im.paste(wood, (x0, y0))
        cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
        im.alpha_composite(mark, (cx - mark.width // 2, cy - mark.height // 2))
        im.save(os.path.join(out_dir, name + ".png"))
    print("menu icon: wrote", out_dir)


if __name__ == "__main__":
    main()
