import os, sys, json, subprocess, importlib
sys.path.insert(0, os.path.dirname(__file__))
import lib
mods = sys.argv[1:] or ['general', 'units', 'buildings']
for m in mods:
    importlib.import_module(m)
ROOT = '<repo>/wololoader/scriptorium/icon_library'
from PIL import Image, ImageDraw
cat_list = {}
names = set()
for cat, name, title, tags, body in lib.ICONS:
    assert name not in names, name; names.add(name)
    d = os.path.join(ROOT, cat); os.makedirs(d, exist_ok=True)
    sp = os.path.join(d, name + '.svg'); pp = os.path.join(d, name + '.png')
    open(sp, 'w').write(lib.svg(cat, body))
    r = subprocess.run(['rsvg-convert', '-w', '64', '-h', '64', sp, '-o', pp], capture_output=True, text=True)
    if r.returncode or r.stderr: print('ERR', name, r.stderr)
    cat_list.setdefault(cat, []).append((name, title, tags))
# contact sheets
for cat, items in cat_list.items():
    for size in (64, 36):
        cols = 10; cw = size + 8; ch = size + (14 if size == 64 else 8)
        rows = (len(items) + cols - 1) // cols
        sheet = Image.new('RGB', (cols * cw + 8, rows * ch + 8), (52, 50, 46))
        dr = ImageDraw.Draw(sheet)
        for i, (name, _, _) in enumerate(items):
            im = Image.open(os.path.join(ROOT, cat, name + '.png')).convert('RGBA')
            if size != 64: im = im.resize((size, size), Image.LANCZOS)
            x = 8 + (i % cols) * cw; y = 8 + (i // cols) * ch
            sheet.paste(im, (x, y), im)
            if size == 64: dr.text((x, y + size + 1), name[:11], fill=(220, 220, 210))
        sheet.save(f'/tmp/icon-research/sheet_{cat}_{size}.png')
    print(cat, len(items))
order = {'units': 0, 'buildings': 1, 'general': 2}
cat_entries = []
for cat, name, title, tags, body in sorted(lib.ICONS, key=lambda i: order[i[0]]):
    cat_entries.append({'id': name, 'name': title, 'category': cat, 'tags': tags,
                        'svg': f'{cat}/{name}.svg', 'png': f'{cat}/{name}.png'})
if len(mods) == 3:
    with open(os.path.join(ROOT, 'catalog.json'), 'w') as f:
        f.write('[\n' + ',\n'.join('  ' + json.dumps(e, ensure_ascii=False) for e in cat_entries) + '\n]\n')
    print('catalog', len(cat_entries))
