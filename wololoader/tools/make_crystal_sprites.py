#!/usr/bin/env python3
"""Generate Crystal Mines SLP sprites:
  - 90001.slp: 7 original isometric crystal cluster variations (Crystal Deposit)
  - 90002.slp: Male Crystal Miner carrying crystal (recolored gold nugget from 2117.slp)
  - 90003.slp: Female Crystal Miner carrying crystal (recolored gold nugget from 2218.slp)

Usage:
  python3 -I tools/make_crystal_sprites.py "<game dir>" "<out graphics dir>" [--preview <png>]
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import slpwrite

# Palette indices verified against AoE2 HD 50500.bina:
# Crystal cyan/sapphire ramp (dark -> bright):
#   deep_rock:  144 (24,24,0), 137 (56,56,56), 135 (80,80,80), 133 (108,108,108)
#   deep_blue:  209 (0,115,207), 207 (0,123,189), 215 (0,138,186)
#   mid_cyan:   253 (0,139,210), 254 (0,160,243), 230 (105,166,197)
#   bright_cyn: 231 (125,197,217), 240 (152,192,240), 101 (38,223,170), 251 (0,255,255)
#   ice_white:  232 (156,197,217), 228 (146,176,187), 255 (255,255,255)

# Gold nugget indices in 2117.slp / 2218.slp -> crystal cyan ramp
GOLD_TO_CRYSTAL = {
    68:  251,  # (255,247,37) -> bright electric cyan highlight
    67:  240,  # (255,199,0)  -> glowing ice-blue crystal
    122: 231,  # (225,177,90) -> light cyan facet
    107: 254,  # (228,162,82) -> vivid azure crystal
    95:  254,  # (204,160,36) -> vivid azure crystal
    74:  253,  # (175,126,36) -> medium sapphire facet
    59:  207,  # (144,100,12) -> deep sapphire shadow facet
    156: 227,  # (116,115,75) -> cool slate-cyan shadow
}


def load_palette(game_dir):
    pal_path = os.path.join(game_dir, "resources", "_common", "drs", "interface", "50500.bina")
    with open(pal_path, "r", encoding="ascii", errors="ignore") as f:
        lines = [l.strip() for l in f if l.strip()]
    n = int(lines[2])
    return [tuple(map(int, lines[3 + i].split())) for i in range(n)]


def nearest_idx(pal, rgb, avoid=None):
    best_i, best_d = 0, 10 ** 9
    r0, g0, b0 = rgb
    for i, (r, g, b) in enumerate(pal):
        if avoid and i in avoid:
            continue
        d = (r - r0) ** 2 + (g - g0) ** 2 + (b - b0) ** 2
        if d < best_d:
            best_d, best_i = d, i
    return best_i


def draw_crystal_spire(buf, w, h, bx, by, length, width, angle_deg, c_dark, c_mid, c_lit, c_hi, c_edge):
    """Draw a sharp faceted hexagonal crystal spire rooted at (bx, by) pointing along angle_deg."""
    rad = math.radians(angle_deg)
    # Axis vector (dx, dy) from base to tip; y points down in image coords
    ax = math.sin(rad)
    ay = -math.cos(rad)
    # Perpendicular vector (px, py)
    px = math.cos(rad)
    py = math.sin(rad)

    min_x = max(0, int(min(bx, bx + ax * length) - width - 3))
    max_x = min(w - 1, int(max(bx, bx + ax * length) + width + 3))
    min_y = max(0, int(min(by, by + ay * length) - width - 3))
    max_y = min(h - 1, int(max(by, by + ay * length) + width + 3))

    tip_start = 0.68  # top 32% tapers to a sharp point
    for y in range(min_y, max_y + 1):
        for x in range(min_x, max_x + 1):
            rx = x - bx
            ry = y - by
            t = (rx * ax + ry * ay) / length  # 0 at base, 1 at tip
            s = rx * px + ry * py             # signed distance from central ridge
            if t < -0.05 or t > 1.0:
                continue
            if t < 0.0:
                half_w = width * (1.0 + t * 4.0)
            elif t <= tip_start:
                half_w = width * (1.0 - 0.12 * (t / tip_start))
            else:
                half_w = width * 0.88 * max(0.0, (1.0 - t) / (1.0 - tip_start))
            if half_w <= 0.35 or abs(s) > half_w:
                continue

            # Crisp dark outline on outer 1px edge
            if abs(s) > half_w - 0.95 or t > 0.94:
                col = c_edge
            else:
                # 3 visible vertical facets: left (lit), center-left (brightest), right (shaded)
                u = s / half_w  # -1 (left) .. +1 (right)
                if t > tip_start and u < 0.15:
                    col = c_hi if (t > 0.80 or abs(u + 0.25) < 0.28) else c_lit
                elif u < -0.25:
                    col = c_lit if t > 0.25 else c_mid
                elif u < 0.20:
                    # Bright specular central ridge
                    if abs(u + 0.05) < 0.18 and t > 0.22:
                        col = c_hi
                    else:
                        col = c_lit if t > 0.45 else c_mid
                else:
                    col = c_dark if u > 0.55 else c_mid
            buf[y][x] = ("c", col)


def make_crystal_deposit_frames(pal):
    """Generate 7 distinct isometric crystal cluster frames (matching Gold Mine's 7 angles)."""
    # Avoid player-color ramps (16..23, 32..39, 48..55, 64..71, 80..87, 96..103, 112..119, 128..135)
    # Actually only 16..23 are player 1 ramp when used with ("p", ...); ("c", idx) is fixed palette!
    c_edge = nearest_idx(pal, (12, 34, 62))
    c_dark = 207   # (0, 123, 189)
    c_mid  = 254   # (0, 160, 243)
    c_lit  = 231   # (125, 197, 217)
    c_hi   = 251   # (0, 255, 255)
    c_tip  = 255   # (255, 255, 255)
    c_rock_d = nearest_idx(pal, (44, 52, 64))
    c_rock_m = nearest_idx(pal, (74, 84, 98))
    c_rock_l = nearest_idx(pal, (108, 118, 130))

    # 7 cluster layouts: list of (dx, dy, length, width, angle_deg)
    layouts = [
        [(-12, 1, 15, 4.2, -32), (12, 2, 14, 4.0, 34), (-5, 0, 22, 5.2, -14), (6, 1, 20, 4.8, 18), (0, 2, 27, 5.8, 2), (-1, 5, 12, 3.6, -24), (8, 5, 11, 3.4, 28)],
        [(-13, 2, 14, 3.8, -38), (11, 0, 18, 4.6, 26), (-4, 1, 26, 5.6, -8), (4, 2, 21, 5.0, 14), (10, 4, 12, 3.5, 42), (-8, 4, 13, 3.6, -22), (0, 5, 14, 4.0, 4)],
        [(-10, 0, 19, 4.6, -26), (13, 2, 15, 4.0, 38), (0, 0, 24, 5.4, -4), (7, 1, 25, 5.5, 16), (-6, 4, 15, 4.0, -18), (3, 5, 13, 3.8, 10), (-14, 4, 10, 3.2, -46)],
        [(-11, 2, 16, 4.2, -30), (9, 1, 22, 5.2, 22), (-3, 0, 28, 6.0, -3), (14, 3, 12, 3.6, 44), (-7, 4, 13, 3.8, -20), (4, 4, 16, 4.2, 12), (-1, 6, 11, 3.4, -8)],
        [(-13, 1, 17, 4.4, -34), (11, 1, 19, 4.8, 30), (-6, 0, 21, 5.0, -16), (3, 0, 26, 5.6, 8), (8, 4, 14, 3.8, 26), (-2, 4, 15, 4.2, -6), (-9, 5, 11, 3.2, -36)],
        [(-9, 1, 20, 4.8, -22), (12, 1, 16, 4.2, 36), (1, 0, 27, 5.8, 5), (-13, 3, 13, 3.6, -42), (6, 3, 18, 4.6, 20), (-4, 4, 14, 3.8, -12), (10, 5, 10, 3.2, 40)],
        [(-12, 0, 16, 4.2, -28), (10, 2, 20, 5.0, 28), (-2, 1, 25, 5.6, -10), (5, 0, 23, 5.2, 12), (-8, 4, 14, 3.8, -24), (2, 5, 14, 4.0, 6), (13, 4, 11, 3.4, 45)],
    ]

    w, h = 64, 44
    hx, hy = 32, 30
    frames = []
    for var_i, spires in enumerate(layouts):
        buf = [[None] * w for _ in range(h)]

        # 1. Isometric diamond/ellipse ground shadow (shifted slightly right-down like AoE2 sunlight)
        for y in range(h):
            for x in range(w):
                dx = (x - (hx + 4)) / 24.0
                dy = (y - (hy + 2)) / 11.5
                if dx * dx + dy * dy <= 1.0:
                    buf[y][x] = ("s", 0)

        # 2. Dark mineral rock bed at base of crystal cluster
        for y in range(h):
            for x in range(w):
                dx = (x - hx) / 19.0
                dy = (y - (hy + 1)) / 8.5
                # Slight organic irregularity per variation
                ang = math.atan2(dy, dx)
                wobble = 0.08 * math.sin(ang * 3 + var_i * 1.7) + 0.05 * math.cos(ang * 5 - var_i)
                dist = math.hypot(dx, dy)
                if dist <= 1.0 + wobble:
                    if dist > 0.86 + wobble:
                        buf[y][x] = ("c", c_edge)
                    elif dx + dy < -0.25:
                        buf[y][x] = ("c", c_rock_l)
                    elif dx + dy < 0.35:
                        buf[y][x] = ("c", c_rock_m)
                    else:
                        buf[y][x] = ("c", c_rock_d)

        # 3. Draw crystal spires sorted back-to-front by base y
        for dx, dy, length, width, angle_deg in sorted(spires, key=lambda s: s[1]):
            draw_crystal_spire(buf, w, h, hx + dx, hy + dy, length, width, angle_deg,
                               c_dark, c_mid, c_lit, c_hi, c_edge)
            # 1px ice-white glint right near the tip
            rad = math.radians(angle_deg)
            gx = int(round(hx + dx + math.sin(rad) * (length * 0.82)))
            gy = int(round(hy + dy - math.cos(rad) * (length * 0.82)))
            if 0 <= gx < w and 0 <= gy < h:
                buf[gy][gx] = ("c", c_tip)

        frames.append((buf, hx, hy))
    return frames


def recolor_slp_in_place(data, remap):
    """Recolor normal palette-index pixels in an SLP 2.0N binary without altering commands/outlines."""
    out = bytearray(data)
    nf = struct.unpack_from("<i", data, 4)[0]
    for fi in range(nf):
        cmd_tab, outl, _, _, w, h, hx, hy = struct.unpack_from("<IIIIiiii", data, 32 + 32 * fi)
        for y in range(h):
            l, r = struct.unpack_from("<HH", data, outl + 4 * y)
            if l == 0x8000 or r == 0x8000:
                continue
            p = struct.unpack_from("<I", data, cmd_tab + 4 * y)[0]
            while True:
                c = data[p]
                p += 1
                lo = c & 0x0F
                if lo == 0x0F:
                    break
                if c & 3 == 0:
                    k = c >> 2
                    for j in range(k):
                        b = out[p + j]
                        if b in remap:
                            out[p + j] = remap[b]
                    p += k
                elif c & 3 == 1:
                    k = c >> 2
                    if k == 0:
                        p += 1
                elif lo == 2:
                    k = ((c & 0xF0) << 4) + data[p]
                    p += 1
                    for j in range(k):
                        b = out[p + j]
                        if b in remap:
                            out[p + j] = remap[b]
                    p += k
                elif lo == 3:
                    p += 1
                elif lo in (6, 7, 0x0A, 0x0B):
                    k = c >> 4
                    if k == 0:
                        k = data[p]
                        p += 1
                    if lo == 6:
                        p += k
                    elif lo == 7:
                        b = out[p]
                        if b in remap:
                            out[p] = remap[b]
                        p += 1
                    elif lo == 0x0A:
                        p += 1
                elif lo == 0x0E:
                    if c in (0x4E, 0x6E):
                        pass
                    elif c in (0x5E, 0x7E):
                        p += 1
    return bytes(out)


def render_slp_frame_rgba(data, frame_idx, pal):
    """Render one frame of any SLP 2.0N file to (w, h, rgba_bytes) for visual preview."""
    cmd_tab, outl, _, _, w, h, hx, hy = struct.unpack_from("<IIIIiiii", data, 32 + 32 * frame_idx)
    rgba = bytearray(w * h * 4)
    for y in range(h):
        l, r = struct.unpack_from("<HH", data, outl + 4 * y)
        if l == 0x8000 or r == 0x8000:
            continue
        x = l
        p = struct.unpack_from("<I", data, cmd_tab + 4 * y)[0]
        while True:
            c = data[p]
            p += 1
            lo = c & 0x0F
            if lo == 0x0F:
                break
            if c & 3 == 0:
                k = c >> 2
                for b in data[p:p + k]:
                    if 0 <= x < w:
                        o = (y * w + x) * 4
                        rgba[o:o + 3] = bytes(pal[b])
                        rgba[o + 3] = 255
                    x += 1
                p += k
            elif c & 3 == 1:
                k = c >> 2
                if k == 0:
                    k = data[p]
                    p += 1
                x += k
            elif lo == 2:
                k = ((c & 0xF0) << 4) + data[p]
                p += 1
                for b in data[p:p + k]:
                    if 0 <= x < w:
                        o = (y * w + x) * 4
                        rgba[o:o + 3] = bytes(pal[b])
                        rgba[o + 3] = 255
                    x += 1
                p += k
            elif lo == 3:
                k = ((c & 0xF0) << 4) + data[p]
                p += 1
                x += k
            elif lo in (6, 7, 0x0A, 0x0B):
                k = c >> 4
                if k == 0:
                    k = data[p]
                    p += 1
                if lo == 6:
                    for b in data[p:p + k]:
                        if 0 <= x < w:
                            o = (y * w + x) * 4
                            rgba[o:o + 3] = bytes(pal[16 + (b & 7)])
                            rgba[o + 3] = 255
                        x += 1
                    p += k
                elif lo == 7:
                    b = data[p]
                    p += 1
                    for _ in range(k):
                        if 0 <= x < w:
                            o = (y * w + x) * 4
                            rgba[o:o + 3] = bytes(pal[b])
                            rgba[o + 3] = 255
                        x += 1
                elif lo == 0x0A:
                    b = data[p]
                    p += 1
                    for _ in range(k):
                        if 0 <= x < w:
                            o = (y * w + x) * 4
                            rgba[o:o + 3] = bytes(pal[16 + (b & 7)])
                            rgba[o + 3] = 255
                        x += 1
                else:
                    for _ in range(k):
                        if 0 <= x < w:
                            o = (y * w + x) * 4
                            rgba[o:o + 4] = b"\x00\x00\x00\x80"
                        x += 1
            elif lo == 0x0E:
                if c in (0x4E, 0x6E):
                    x += 1
                elif c in (0x5E, 0x7E):
                    x += data[p]
                    p += 1
    return w, h, bytes(rgba)


def main(argv):
    if len(argv) < 3:
        sys.stderr.write("usage: make_crystal_sprites.py <game_dir> <out_graphics_dir> [--preview <png>]\n")
        return 2
    game_dir = argv[1]
    out_dir = argv[2]
    preview_path = argv[4] if len(argv) >= 5 and argv[3] == "--preview" else None

    pal = load_palette(game_dir)
    os.makedirs(out_dir, exist_ok=True)

    # 1. Original Crystal Deposit (90001.slp, 7 variations)
    dep_frames = make_crystal_deposit_frames(pal)
    dep_bytes = slpwrite.encode(dep_frames, comment=b"Wololoader Crystal")
    dep_path = os.path.join(out_dir, "90001.slp")
    with open(dep_path, "wb") as f:
        f.write(dep_bytes)
    print("wrote %s (%d bytes, %d frames)" % (dep_path, len(dep_bytes), len(dep_frames)))

    # 2. Male & Female carry crystal (recolored from player's 2117.slp / 2218.slp)
    carry_outputs = {}
    for src_id, dst_id in ((2117, 90002), (2218, 90003)):
        src_path = os.path.join(game_dir, "resources", "_common", "drs", "graphics", "%d.slp" % src_id)
        if not os.path.isfile(src_path):
            sys.stderr.write("warning: %s not found, skipping %d.slp\n" % (src_path, dst_id))
            continue
        with open(src_path, "rb") as f:
            src_data = f.read()
        dst_data = recolor_slp_in_place(src_data, GOLD_TO_CRYSTAL)
        dst_path = os.path.join(out_dir, "%d.slp" % dst_id)
        with open(dst_path, "wb") as f:
            f.write(dst_data)
        carry_outputs[dst_id] = dst_data
        print("wrote %s (%d bytes, from %d.slp)" % (dst_path, len(dst_data), src_id))

    if preview_path:
        from PIL import Image
        # Contact sheet on grass-green background (2x scaled for crisp inspection)
        sheet = Image.new("RGBA", (7 * 68 + 16, 160), (92, 124, 62, 255))
        for i in range(7):
            w, h, rgba = render_slp_frame_rgba(dep_bytes, i, pal)
            im = Image.frombytes("RGBA", (w, h), rgba)
            sheet.alpha_composite(im, (8 + i * 68, 12))
        y_row2 = 76
        x_cur = 12
        for sid in (90002, 90003):
            if sid not in carry_outputs:
                continue
            for fi in (0, 15, 30, 45, 60):
                w, h, rgba = render_slp_frame_rgba(carry_outputs[sid], fi, pal)
                im = Image.frombytes("RGBA", (w, h), rgba)
                sheet.alpha_composite(im, (x_cur, y_row2))
                x_cur += max(w + 10, 38)
            x_cur += 16
        sheet = sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST)
        sheet.save(preview_path)
        print("wrote preview %s" % preview_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
