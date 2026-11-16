"""Minimal SLP 2.0N writer (and reader for the same commands) for Wololoader mod sprites.

Only what a mod's building/unit art needs: transparent runs, colour runs, player-colour runs and
shadow runs. Command bytes (checked against openage's decoder, slp.pyx create_palette_color_row):
  lesser skip  n<<2 | 1       (n 1..63)          lesser copy  n<<2 | 0 + n bytes (n 1..63)
  greater skip (n>>8)<<4 | 3, n & 0xff            greater copy (n>>8)<<4 | 2, n & 0xff, + n bytes
  player list  n<<4 | 6 (n 1..15, else 6 then n) + n bytes (palette index of player 1's ramp)
  shadow       n<<4 | 0x0b (n 1..15, else 0x0b then n)
  end of row   0x0f
Frame: header "2.0N", frame count, 24-byte comment; 32-byte frame info (command table offset,
outline table offset, palette offset, properties, width, height, hotspot x, hotspot y); outline
table (left/right transparent counts per row, 0x8000 = empty row); command table (u32 per row).

A frame is given as rows of pixels: None = transparent, ("c", index), ("p", index), ("s", 0).
"""
import struct

def _runs(row):
    out = []
    for px in row:
        kind = "t" if px is None else px[0]
        if out and out[-1][0] == kind and len(out[-1][1]) < 4095:
            out[-1][1].append(px)
        else:
            out.append((kind, [px]))
    return out

def _encode_row(row):
    w = len(row)
    left = 0
    while left < w and row[left] is None: left += 1
    if left == w:
        return 0x8000, 0x8000, bytes([0x0F])
    right = 0
    while row[w - 1 - right] is None: right += 1
    b = bytearray()
    for kind, pxs in _runs(row[left:w - right]):
        n = len(pxs)
        if kind == "t":
            while n:
                k = min(n, 4095)
                if k < 64: b.append(k << 2 | 1)
                else: b += bytes([(k >> 8) << 4 | 3, k & 0xFF])
                n -= k
        elif kind == "c":
            i = 0
            while i < n:
                k = min(n - i, 4095)
                if k < 64: b.append(k << 2)
                else: b += bytes([(k >> 8) << 4 | 2, k & 0xFF])
                b += bytes(p[1] for p in pxs[i:i + k]); i += k
        elif kind in ("p", "s"):
            op = 0x06 if kind == "p" else 0x0B
            i = 0
            while i < n:
                k = min(n - i, 255)
                b += bytes([k << 4 | op]) if k < 16 else bytes([op, k])
                if kind == "p": b += bytes(p[1] for p in pxs[i:i + k])
                i += k
    b.append(0x0F)
    return left, right, bytes(b)

def encode(frames, comment=b"Wololoader"):
    """frames: list of (rows, hotspot_x, hotspot_y). Returns SLP file bytes."""
    head = b"2.0N" + struct.pack("<i", len(frames)) + comment[:24].ljust(24, b"\0")
    info_size = 32 * len(frames)
    body = bytearray()
    infos = []
    base = len(head) + info_size
    for rows, hx, hy in frames:
        h, w = len(rows), len(rows[0])
        enc = [_encode_row(r) for r in rows]
        outline_off = base + len(body)
        for l, r, _ in enc: body += struct.pack("<HH", l, r)
        cmd_tab_off = base + len(body)
        body += b"\0" * (4 * h)
        for i, (_, _, data) in enumerate(enc):
            struct.pack_into("<I", body, cmd_tab_off - base + 4 * i, base + len(body))
            body += data
        infos.append(struct.pack("<IIIIiiii", cmd_tab_off, outline_off, 0, 0, w, h, hx, hy))
    return head + b"".join(infos) + bytes(body)

def decode(data):
    """Reads files written by encode (and simple game files): list of (rows, hx, hy)."""
    n = struct.unpack_from("<i", data, 4)[0]
    out = []
    for f in range(n):
        cmd_tab, outl, _, _, w, h, hx, hy = struct.unpack_from("<IIIIiiii", data, 32 + 32 * f)
        rows = []
        for y in range(h):
            l, r = struct.unpack_from("<HH", data, outl + 4 * y)
            row = []
            if l == 0x8000 or r == 0x8000:
                rows.append([None] * w); continue
            row += [None] * l
            p = struct.unpack_from("<I", data, cmd_tab + 4 * y)[0]
            while True:
                c = data[p]; p += 1
                lo = c & 0x0F
                if lo == 0x0F: break
                if c & 3 == 0:
                    k = c >> 2; row += [("c", x) for x in data[p:p + k]]; p += k
                elif c & 3 == 1:
                    k = c >> 2
                    if k == 0: k = data[p]; p += 1
                    row += [None] * k
                elif lo == 2:
                    k = ((c & 0xF0) << 4) + data[p]; p += 1
                    row += [("c", x) for x in data[p:p + k]]; p += k
                elif lo == 3:
                    k = ((c & 0xF0) << 4) + data[p]; p += 1; row += [None] * k
                elif lo in (6, 7, 0x0A, 0x0B):
                    k = c >> 4
                    if k == 0: k = data[p]; p += 1
                    if lo == 6: row += [("p", x) for x in data[p:p + k]]; p += k
                    elif lo == 7: row += [("c", data[p])] * k; p += 1
                    elif lo == 0x0A: row += [("p", data[p])] * k; p += 1
                    else: row += [("s", 0)] * k
                elif lo == 0x0E:
                    raise ValueError("extended command 0x%02x not supported" % c)
                else:
                    raise ValueError("unknown command 0x%02x" % c)
            row += [None] * (w - len(row))
            rows.append(row)
        out.append((rows, hx, hy))
    return out
