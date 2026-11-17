#!/usr/bin/env python3
"""Writes the default mod icon (the same shield Scriptorium draws, scriptorium/ui/app.js
generatedIconSvg) as <mod dir>/icon.png for mods that have none, so the in-game Mods screen
shows it too.  usage: make_default_icon.py <mod dir>...   (needs rsvg-convert)"""
import colorsys, json, os, re, subprocess, sys
from xml.sax.saxutils import escape


def h33(s):
    h = 5381
    for ch in s:
        h = ((h * 33) ^ ord(ch)) & 0xffffffff
    return h


def initials(name):
    words = [w for w in re.split(r"[\s_\-]+", name.strip()) if w]
    if not words:
        return "M"
    if len(words) == 1:
        return words[0][0].upper()
    return (words[0][0] + words[1][0]).upper()


def hsl(h, s, l):
    r, g, b = colorsys.hls_to_rgb(h / 360, l, s)
    return "#%02x%02x%02x" % (round(r * 255), round(g * 255), round(b * 255))


def svg(folder, name):
    hue = h33(folder or name or "mod") % 360
    ini = initials(name or folder)
    fs = 20 if len(ini) > 1 else 25
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">'
            '<path d="M32 3 L56 11 V30 C56 46 46 56 32 61 C18 56 8 46 8 30 V11 Z" fill="%s" stroke="%s" stroke-width="2.5" stroke-linejoin="round"/>'
            '<path d="M32 9 L50 15 V30 C50 42 43 50 32 54.5 C21 50 14 42 14 30 V15 Z" fill="none" stroke="#ffffff" stroke-opacity="0.14" stroke-width="1.5"/>'
            '<text x="32" y="40" text-anchor="middle" font-family="Georgia, serif" font-size="%d" font-weight="700" fill="#f1e6c8">%s</text></svg>'
            % (hsl(hue, .40, .28), hsl(hue, .50, .50), fs, escape(ini)))


for d in sys.argv[1:]:
    out = os.path.join(d, "icon.png")
    if os.path.exists(out):
        continue
    folder = os.path.basename(os.path.normpath(d))
    try:
        name = json.load(open(os.path.join(d, "mod.json"), encoding="utf-8-sig")).get("name") or folder
    except (OSError, ValueError):
        name = folder
    subprocess.run(["rsvg-convert", "-w", "256", "-h", "256", "-o", out], input=svg(folder, name).encode(), check=True)
    print("wrote", out)
