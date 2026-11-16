#!/usr/bin/env python3
"""Builds the release installers (run by `make release`).

usage: package.py <version> <game dir> <out dir> <setup stub exe>

Release tree (the same files `make install` puts into the game):
  core/      version.dll, Wololoader Base mod, Mods button images (Scriptorium is left out for now)
  examples/  wololoader/mods/Monklanians, wololoader/mods/DevPanel
The Mods button images are made from the game's own button art by make_menu_icon.py at
`make install`, so they are copied from <game dir>/wololoader/resources.

Outputs: wololoader-windows-setup.exe (stub + payload, see installer/wlsetup.c),
         wololoader-linux-setup.sh (installer/install.sh.in + tar.gz).
"""
import io
import os
import shutil
import struct
import sys
import tarfile

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def tree(version, game):
    files = []   # (release path "core/..." or "examples/...", source path)

    def add(rel, src):
        if not os.path.isfile(src):
            sys.exit("package: missing %s" % src)
        files.append((rel, src))

    def add_dir(rel, src):
        for root, _, names in os.walk(src):
            for n in sorted(names):
                if n.endswith((".pyc", ".log")) or n.startswith("."):
                    continue
                p = os.path.join(root, n)
                add(rel + "/" + os.path.relpath(p, src).replace(os.sep, "/"), p)

    add("core/version.dll", os.path.join(HERE, "build/version.dll"))
    base = "core/wololoader/mods/WololoaderBase"
    add(base + "/mod.json", os.path.join(HERE, "base_mod/mod.json"))
    add(base + "/icon.png", os.path.join(HERE, "art/logo_256.png"))
    add_dir(base + "/icons", os.path.join(HERE, "base_mod/icons"))
    add_dir("core/wololoader/resources", os.path.join(game, "wololoader/resources"))
    for m in ("Monklanians", "DevPanel"):
        add_dir("examples/wololoader/mods/" + m, os.path.join(HERE, "examples", m))
    return sorted(files)


def windows(files, stub, out):
    data, index = io.BytesIO(), io.BytesIO()
    for rel, src in files:
        kind, path = rel.split("/", 1)
        flags = (1 if os.path.basename(path) == "mod.json" else 0) | (2 if kind == "examples" else 0)
        blob = open(src, "rb").read()
        p = path.encode("utf-8")
        index.write(struct.pack("<BH", flags, len(p)) + p + struct.pack("<II", data.tell(), len(blob)))
        data.write(blob)
    # the stub wants absolute file offsets: shift every entry by the stub size
    stub_bytes = open(stub, "rb").read()
    idx = bytearray(index.getvalue())
    pos = 0
    while pos < len(idx):
        n = struct.unpack_from("<H", idx, pos + 1)[0]
        o = pos + 3 + n
        off = struct.unpack_from("<I", idx, o)[0]
        struct.pack_into("<I", idx, o, off + len(stub_bytes))
        pos = o + 8
    with open(out, "wb") as f:
        f.write(stub_bytes + data.getvalue() + idx + struct.pack("<I", len(idx)) + b"WLPAYLD1")


def linux(files, version, out):
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz", format=tarfile.PAX_FORMAT) as t:
        for rel, src in files:
            info = t.gettarinfo(src, rel)
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            info.mode = 0o644
            info.mtime = int(os.path.getmtime(src))
            with open(src, "rb") as fh:
                t.addfile(info, fh)
    head = open(os.path.join(HERE, "installer/install.sh.in"), encoding="utf-8").read().replace("@VERSION@", version)
    with open(out, "wb") as f:
        f.write(head.encode("utf-8") + buf.getvalue())
    os.chmod(out, 0o755)


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    version, game, out_dir, stub = sys.argv[1:]
    files = tree(version, game)
    os.makedirs(out_dir, exist_ok=True)
    w = os.path.join(out_dir, "wololoader-windows-setup.exe")
    l = os.path.join(out_dir, "wololoader-linux-setup.sh")
    windows(files, stub, w)
    linux(files, version, l)
    for p in (w, l):
        print("%s  %.1f MB" % (p, os.path.getsize(p) / 1e6))
    print("%d files" % len(files))


if __name__ == "__main__":
    main()
