"""Shared pieces for the Scriptorium icon library: palette gradients, frame, weapon/figure helpers.

All icons use a 36x36 viewBox (the size of an AoE2 HD command button) and are rendered to 64x64 PNG.
Light comes from the top left: every gradient runs light (top-left) -> dark (bottom-right).
"""
import re

K = '#1b1106'          # outline colour (dark warm brown, like the existing mod icons)
ICONS = []             # (category, name, title, tags, body)


def S(w=1.2, c=K):
    return f'stroke="{c}" stroke-width="{w}" stroke-linejoin="round" stroke-linecap="round"'


DEFS = {}


def lin(i, *stops, v=False, h=False):
    x2, y2 = (0, 1) if v else ((1, 0) if h else (1, 1))
    n = len(stops)
    st = ''.join(f'<stop offset="{round(k / (n - 1) * 100)}%" stop-color="{c}"/>' for k, c in enumerate(stops))
    DEFS[i] = f'<linearGradient id="{i}" x1="0" y1="0" x2="{x2}" y2="{y2}">{st}</linearGradient>'


def rad(i, *stops, cx=.38, cy=.32, r=.7):
    n = len(stops)
    st = ''.join(f'<stop offset="{round(k / (n - 1) * 100)}%" stop-color="{c}" stop-opacity="{o}"/>'
                 for k, (c, o) in enumerate(stops))
    DEFS[i] = f'<radialGradient id="{i}" cx="{cx}" cy="{cy}" r="{r}">{st}</radialGradient>'


# ---- palette (light top-left -> dark bottom-right) -------------------------------------------
lin('steel', '#f4f8fc', '#a9b6c4', '#56616e')
lin('gold', '#fff3a6', '#e6ab2e', '#8c5a0c')
lin('oak', '#d39a52', '#8a5520', '#5a3410')
lin('dwood', '#9a6838', '#5e3816', '#3a200a')
lin('skin', '#f6d0a4', '#d69a68', '#a8693e')
lin('team', '#8ab2f2', '#3a62c0', '#1c3478')
lin('red', '#f07a5e', '#c03424', '#6e1410')
lin('green', '#a4d470', '#4a8a2e', '#22481a')
lin('leather', '#c48a52', '#8a5428', '#4e2c12')
lin('robe', '#f4b868', '#c06a1e', '#6e3410')
lin('cream', '#fbf2da', '#d8c49a', '#8e7a52')
lin('purple', '#d6a4ff', '#8040c8', '#3e1670')
lin('stone', '#eee8dc', '#b0a898', '#6a6256')
lin('roof', '#e47a54', '#a83c22', '#5e1c0e')
lin('thatch', '#f4d680', '#c4943a', '#7a561c')
lin('horse', '#c88a50', '#8a4e22', '#4a2810')
lin('grey', '#f4f0e8', '#b8b2a6', '#6e685e')
lin('camel', '#f0d6a0', '#c49e60', '#7a5a2e')
lin('eleph', '#c4c0bc', '#868280', '#46423f')
lin('wolf', '#d0ccc4', '#8a8680', '#46423c')
lin('water', '#6aa8e8', '#2a64b0', '#123462', v=True)
lin('black', '#6e6864', '#2e2a28', '#121010')
lin('ice', '#f4fdff', '#90d8f6', '#2a74b4')
lin('poison', '#dcff86', '#6ab820', '#2a5a0a')
lin('crystal', '#e8fbff', '#5ab8f0', '#1a4e98')
lin('ruby', '#ffb0b8', '#e02a40', '#6e0a18')
lin('brass', '#ffe8a0', '#c89a3a', '#6e4e14')
lin('bone', '#fffbea', '#dcd2b4', '#8e8466')
lin('iron', '#9a9ea6', '#555a62', '#26282e')
lin('grass', '#8cbc58', '#4a7a26', '#28461a', v=True)
lin('field', '#d8c060', '#9a8a30', v=True)
lin('upg', '#c8ff9a', '#4cb42a', '#1e5a10')
lin('dng', '#ffb08a', '#d8401c', '#6a1608')
lin('paper', '#fffbe6', '#ead8a8', '#a88a50')
lin('smoke', '#f0f0f0', '#a8a8a8', v=True)
rad('fire', ('#fffbd0', 1), ('#ffcc40', 1), ('#ee5a1a', 1), ('#9a1e08', 1), cx=.5, cy=.78, r=.85)
rad('glow', ('#fffbd8', .95), ('#ffd850', .45), ('#ffd040', 0), cx=.5, cy=.5, r=.5)
rad('bglow', ('#e8faff', .95), ('#60c0ff', .4), ('#60c0ff', 0), cx=.5, cy=.5, r=.5)
rad('ggl', ('#f0ffd8', .9), ('#80e040', .4), ('#80e040', 0), cx=.5, cy=.5, r=.5)
rad('pgl', ('#fbe8ff', .95), ('#c070ff', .45), ('#c070ff', 0), cx=.5, cy=.5, r=.5)
rad('hl', ('#ffffff', .24), ('#ffffff', 0), cx=.18, cy=.12, r=.85)
rad('vig', ('#000000', 0), ('#000000', 0), ('#000000', .42), cx=.5, cy=.45, r=.75)

# category tints (vertical: lighter top, dark bottom)
BG = {'units': ('bgU', '#6c2e22', '#250d09'),
      'buildings': ('bgB', '#5f8cb4', '#1d3654'),
      'general': ('bgG', '#53402a', '#19110a')}
for _id, a, b in BG.values():
    lin(_id, a, b, v=True)
DEFS['clip'] = '<clipPath id="clip"><rect x="2" y="2" width="32" height="32" rx="2"/></clipPath>'


def frame(cat, body):
    bg = BG[cat][0]
    ground = ''
    if cat == 'buildings':
        # every building stands on a grass strip, like the game's building portraits
        ground = ('<path d="M0 27.5 C9 25.8 27 25.8 36 27.5 L36 36 L0 36 Z" fill="url(#grass)"/>'
                  '<path d="M0 27.5 C9 25.8 27 25.8 36 27.5" fill="none" stroke="#c8e890" stroke-width=".5" stroke-opacity=".6"/>')
    return (f'<rect x="1" y="1" width="34" height="34" rx="3" fill="url(#{bg})"/>\n'
            f'<g clip-path="url(#clip)">\n<rect width="36" height="36" fill="url(#hl)"/>\n{ground}\n{body}\n'
            f'<rect width="36" height="36" fill="url(#vig)"/>\n</g>\n'
            f'<rect x="1" y="1" width="34" height="34" rx="3" fill="none" stroke="#120b05" stroke-width="1.8"/>\n'
            f'<rect x="2.5" y="2.5" width="31" height="31" rx="2" fill="none" stroke="#d4a64a" stroke-width="0.9" stroke-opacity="0.75"/>')


def svg(cat, body):
    inner = frame(cat, body)
    used = []
    for m in re.finditer(r'url\(#([\w-]+)\)', inner):
        if m.group(1) not in used:
            used.append(m.group(1))
    defs = '\n    '.join(DEFS[u] for u in used)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 36 36" width="36" height="36">\n'
            f'  <defs>\n    {defs}\n  </defs>\n{inner}\n</svg>\n')


def icon(cat, name, tags, body, title=None):
    ICONS.append((cat, name, title or name.replace('_', ' ').capitalize(), tags, body))


# ---- helpers ------------------------------------------------------------------------------------
def T(x, y, a=0, s=1, inner=''):
    return f'<g transform="translate({x} {y}) rotate({a}) scale({s})">{inner}</g>'


def sword(x, y, a=0, L=16, s=1, blade='steel'):
    """Sword with its guard at (x, y), pointing up when a=0."""
    g = (f'<path d="M-1.3 0 L-1.3 {-L + 3} L0 {-L} L1.3 {-L + 3} L1.3 0 Z" fill="url(#{blade})" {S(1)}/>'
         f'<line x1="0" y1="-1" x2="0" y2="{-L + 3.5}" stroke="#5a6470" stroke-width=".5"/>'
         f'<rect x="-1" y="1" width="2" height="3.6" fill="url(#leather)" {S(.8)}/>'
         f'<rect x="-4" y="-.6" width="8" height="2" rx=".8" fill="url(#gold)" {S(.9)}/>'
         f'<circle cx="0" cy="5.4" r="1.3" fill="url(#gold)" {S(.8)}/>')
    return T(x, y, a, s, g)


def spear(x, y, a=0, L=26, s=1, head='leaf'):
    """Spear from its butt at (x, y) pointing up when a=0."""
    g = f'<rect x="-.85" y="{-L + 3}" width="1.7" height="{L - 3}" rx=".6" fill="url(#oak)" {S(.8)}/>'
    if head == 'leaf':
        g += f'<path d="M0 {-L - 5} L2 {-L + .5} L0 {-L + 3} L-2 {-L + .5} Z" fill="url(#steel)" {S(.9)}/>'
    else:  # halberd-like pike head: long thin point
        g += f'<path d="M0 {-L - 6} L1.2 {-L + 1} L0 {-L + 3} L-1.2 {-L + 1} Z" fill="url(#steel)" {S(.9)}/>'
    return T(x, y, a, s, g)


def bow(x, y, a=0, s=1, arrow=False, long=False):
    h = 14 if long else 11
    g = ''
    if arrow:
        g += (f'<line x1="-6" y1="0" x2="5" y2="0" {S(2)}/>'
              f'<line x1="-6" y1="0" x2="5" y2="0" stroke="#e8d0a0" stroke-width=".9"/>'
              f'<path d="M5 -1.4 L8 0 L5 1.4 Z" fill="url(#steel)" {S(.6)}/>'
              f'<path d="M-6 0 L-8 -1.6 M-6 0 L-8 1.6" stroke="#c03424" stroke-width=".9"/>')
    g += (f'<line x1="-3" y1="{-h}" x2="-3" y2="{h}" stroke="#f0e6c8" stroke-width=".6"/>'
          f'<path d="M-3 {-h} C4 {-h * .6} 4 {h * .6} -3 {h}" fill="none" stroke="{K}" stroke-width="3.2" stroke-linecap="round"/>'
          f'<path d="M-3 {-h} C4 {-h * .6} 4 {h * .6} -3 {h}" fill="none" stroke="url(#oak)" stroke-width="1.7" stroke-linecap="round"/>'
          f'<rect x="1.6" y="-1.6" width="1.8" height="3.2" fill="url(#leather)" {S(.6)}/>')
    return T(x, y, a, s, g)


def axe(x, y, a=0, L=17, s=1, double=False):
    g = f'<rect x="-.9" y="{-L}" width="1.8" height="{L}" rx=".6" fill="url(#oak)" {S(.8)}/>'
    g += f'<path d="M.5 {-L + 1} L4 {-L - 2.5} C7 {-L + 1} 7 {-L + 5} 4 {-L + 8} L.5 {-L + 5} Z" fill="url(#steel)" {S(1)}/>'
    if double:
        g += f'<path d="M-.5 {-L + 1} L-4 {-L - 2.5} C-7 {-L + 1} -7 {-L + 5} -4 {-L + 8} L-.5 {-L + 5} Z" fill="url(#steel)" {S(1)}/>'
    return T(x, y, a, s, g)


def round_shield(x, y, r=6, fill='team'):
    return (f'<circle cx="{x}" cy="{y}" r="{r}" fill="url(#{fill})" {S(1.2)}/>'
            f'<circle cx="{x}" cy="{y}" r="{r - 1.3}" fill="none" stroke="#e6c070" stroke-width=".7" stroke-opacity=".8"/>'
            f'<circle cx="{x}" cy="{y}" r="{r * .3}" fill="url(#steel)" {S(.8)}/>')


def kite_shield(x, y, s=1, fill='team', a=0):
    g = (f'<path d="M-6 -7 C-2 -8.5 2 -8.5 6 -7 C6 0 3 6 0 9 C-3 6 -6 0 -6 -7 Z" fill="url(#{fill})" {S(1.2)}/>'
         f'<path d="M-4.6 -5.8 C-1.5 -7 1.5 -7 4.6 -5.8 C4.6 -.5 2.3 4.4 0 7 C-2.3 4.4 -4.6 -.5 -4.6 -5.8 Z" fill="none" stroke="#f0d080" stroke-width=".7"/>'
         f'<path d="M0 -6.5 L0 6 M-4.5 -2 L4.5 -2" stroke="#f0d080" stroke-width="1.3"/>')
    return T(x, y, a, s, g)


def torso(x=18, cloth='team', w=11, top=21.5):
    return (f'<path d="M{x - w} 36 C{x - w} {top + 4} {x - w + 4} {top} {x} {top} '
            f'C{x + w - 4} {top} {x + w} {top + 4} {x + w} 36 Z" fill="url(#{cloth})" {S(1.3)}/>')


def head(x=18, y=13.5, r=4.6, skin='skin', eyes=True, beard=None):
    g = f'<rect x="{x - 1.8}" y="{y + r - 1.5}" width="3.6" height="3.5" fill="url(#{skin})" {S(1)}/>'
    g += f'<circle cx="{x}" cy="{y}" r="{r}" fill="url(#{skin})" {S(1.2)}/>'
    if beard:
        g += (f'<path d="M{x - r + .6} {y + .5} C{x - r + 1} {y + r + 2.5} {x + r - 1} {y + r + 2.5} {x + r - .6} {y + .5} '
              f'C{x + 2} {y + 2.4} {x - 2} {y + 2.4} {x - r + .6} {y + .5} Z" fill="{beard}" {S(.9)}/>')
    if eyes:
        g += (f'<circle cx="{x - 1.7}" cy="{y - .2}" r=".6" fill="{K}"/><circle cx="{x + 1.7}" cy="{y - .2}" r=".6" fill="{K}"/>')
    return g


def belt(x=18, y=30, w=10):
    return f'<rect x="{x - w}" y="{y}" width="{2 * w}" height="2" fill="url(#leather)" {S(.8)}/>'


# helmets drawn over head(x, y, r=4.6)
def helm(kind, x=18, y=13.5):
    if kind == 'cap':
        return f'<path d="M{x - 4.9} {y - .4} C{x - 5} {y - 6.5} {x + 5} {y - 6.5} {x + 4.9} {y - .4} Z" fill="url(#leather)" {S(1.1)}/>'
    if kind == 'nasal':
        return (f'<path d="M{x - 5} {y} C{x - 5.2} {y - 7.4} {x + 5.2} {y - 7.4} {x + 5} {y} Z" fill="url(#steel)" {S(1.1)}/>'
                f'<rect x="{x - .8}" y="{y - 1}" width="1.6" height="4" fill="url(#steel)" {S(.8)}/>'
                f'<line x1="{x - 5}" y1="{y - .6}" x2="{x + 5}" y2="{y - .6}" stroke="#e6c070" stroke-width=".8"/>')
    if kind == 'kettle':
        return (f'<path d="M{x - 7.5} {y - 1} C{x - 4} {y - 3} {x + 4} {y - 3} {x + 7.5} {y - 1} L{x + 5} {y - 2.4} '
                f'C{x + 5} {y - 8} {x - 5} {y - 8} {x - 5} {y - 2.4} Z" fill="url(#steel)" {S(1.1)}/>')
    if kind == 'great':
        return (f'<path d="M{x - 5} {y + 4.5} L{x - 5.2} {y - 2} C{x - 5} {y - 7.5} {x + 5} {y - 7.5} {x + 5.2} {y - 2} L{x + 5} {y + 4.5} Z" fill="url(#steel)" {S(1.1)}/>'
                f'<rect x="{x - 4.2}" y="{y - 1}" width="8.4" height="1.4" fill="{K}"/>'
                f'<line x1="{x}" y1="{y + 1}" x2="{x}" y2="{y + 4}" stroke="{K}" stroke-width=".8"/>')
    if kind == 'hood':
        return (f'<path d="M{x - 6} {y + 7} C{x - 7} {y - 3} {x - 4} {y - 7.5} {x} {y - 7} C{x + 4} {y - 7.5} {x + 7} {y - 3} {x + 6} {y + 7} '
                f'L{x + 4} {y + 4} C{x + 4.5} {y - 2} {x + 3} {y - 4.6} {x} {y - 4.6} C{x - 3} {y - 4.6} {x - 4.5} {y - 2} {x - 4} {y + 4} Z" fill="url(#green)" {S(1.1)}/>')
    if kind == 'horned':
        return (f'<path d="M{x - 4.5} {y - 3} C{x - 8} {y - 4} {x - 9} {y - 8} {x - 8} {y - 11} C{x - 7} {y - 8} {x - 5.5} {y - 6.5} {x - 3.5} {y - 6} Z" fill="url(#bone)" {S(.9)}/>'
                f'<path d="M{x + 4.5} {y - 3} C{x + 8} {y - 4} {x + 9} {y - 8} {x + 8} {y - 11} C{x + 7} {y - 8} {x + 5.5} {y - 6.5} {x + 3.5} {y - 6} Z" fill="url(#bone)" {S(.9)}/>'
                f'<path d="M{x - 5} {y - .5} C{x - 5.2} {y - 7.4} {x + 5.2} {y - 7.4} {x + 5} {y - .5} Z" fill="url(#iron)" {S(1.1)}/>')
    if kind == 'crown':
        return (f'<path d="M{x - 5} {y - 2} L{x - 5.5} {y - 8} L{x - 2.6} {y - 5} L{x} {y - 9} L{x + 2.6} {y - 5} L{x + 5.5} {y - 8} L{x + 5} {y - 2} Z" fill="url(#gold)" {S(1)}/>'
                f'<circle cx="{x}" cy="{y - 3.6}" r=".9" fill="#d02030" {S(.5)}/>')
    if kind == 'bandana':
        return (f'<path d="M{x - 4.9} {y - 1} C{x - 5} {y - 6.5} {x + 5} {y - 6.5} {x + 4.9} {y - 1} Z" fill="url(#red)" {S(1.1)}/>'
                f'<path d="M{x - 4.6} {y - 2} L{x - 8} {y + 1} L{x - 6.5} {y + 2.2} Z" fill="url(#red)" {S(.8)}/>')
    if kind == 'kabuto':
        return (f'<path d="M{x - 7} {y + 1.5} L{x - 5} {y - 2.5} C{x - 5} {y - 7.5} {x + 5} {y - 7.5} {x + 5} {y - 2.5} L{x + 7} {y + 1.5} L{x + 4.8} {y} L{x - 4.8} {y} Z" fill="url(#black)" {S(1.1)}/>'
                f'<path d="M{x} {y - 5} L{x - 4} {y - 11} L{x - 1} {y - 6.5} Z M{x} {y - 5} L{x + 4} {y - 11} L{x + 1} {y - 6.5} Z" fill="url(#gold)" {S(.8)}/>'
                f'<line x1="{x - 5}" y1="{y - 2.5}" x2="{x + 5}" y2="{y - 2.5}" stroke="#c03424" stroke-width=".9"/>')
    if kind == 'wizard':
        return (f'<path d="M{x - 8} {y - 2} C{x - 4} {y - 4} {x + 4} {y - 4} {x + 8} {y - 2} C{x + 4} {y - 1} {x - 4} {y - 1} {x - 8} {y - 2} Z" fill="url(#purple)" {S(1)}/>'
                f'<path d="M{x - 4.5} {y - 3} C{x - 3} {y - 8} {x + 1} {y - 13} {x + 7} {y - 14} C{x + 3} {y - 11} {x + 3.5} {y - 7} {x + 4.5} {y - 3} Z" fill="url(#purple)" {S(1)}/>'
                f'<path d="M{x + 1.2} {y - 8} l.6 1.3 1.4 .1 -1.1 .9 .4 1.4 -1.3 -.8 -1.3 .8 .4 -1.4 -1.1 -.9 1.4 -.1 Z" fill="#fff3a6"/>')
    if kind == 'straw':
        return (f'<path d="M{x - 8.5} {y - 1.2} C{x - 4} {y - 3.5} {x + 4} {y - 3.5} {x + 8.5} {y - 1.2} C{x + 4} {y - .2} {x - 4} {y - .2} {x - 8.5} {y - 1.2} Z" fill="url(#thatch)" {S(1)}/>'
                f'<path d="M{x - 4} {y - 2.4} C{x - 4} {y - 7} {x + 4} {y - 7} {x + 4} {y - 2.4} Z" fill="url(#thatch)" {S(1)}/>'
                f'<line x1="{x - 4}" y1="{y - 3.2}" x2="{x + 4}" y2="{y - 3.2}" stroke="#a83c22" stroke-width="1"/>')
    if kind == 'kerchief':
        return (f'<path d="M{x - 5.4} {y + 3} C{x - 6} {y - 7} {x + 6} {y - 7} {x + 5.4} {y + 3} L{x + 4} {y + 1} C{x + 4.2} {y - 3.5} {x - 4.2} {y - 3.5} {x - 4} {y + 1} Z" fill="url(#cream)" {S(1)}/>')
    if kind == 'tonsure':
        return (f'<path d="M{x - 4.7} {y - .5} C{x - 5} {y - 3} {x - 4} {y - 4} {x - 3} {y - 3.6} L{x - 2.8} {y - 1.6} Z" fill="#6a4020"/>'
                f'<path d="M{x + 4.7} {y - .5} C{x + 5} {y - 3} {x + 4} {y - 4} {x + 3} {y - 3.6} L{x + 2.8} {y - 1.6} Z" fill="#6a4020"/>')
    if kind == 'turban':
        return (f'<path d="M{x - 5.4} {y - 1} C{x - 6.5} {y - 8.5} {x + 6.5} {y - 8.5} {x + 5.4} {y - 1} C{x + 2} {y - 2.4} {x - 2} {y - 2.4} {x - 5.4} {y - 1} Z" fill="url(#cream)" {S(1.1)}/>'
                f'<path d="M{x - 4.5} {y - 3.5} C{x - 1} {y - 6.5} {x + 2} {y - 6} {x + 5} {y - 4.4}" fill="none" stroke="#a88a50" stroke-width=".7"/>'
                f'<circle cx="{x}" cy="{y - 3.2}" r=".9" fill="#2a8a6a" {S(.5)}/>')
    if kind == 'plumed':
        return (f'<path d="M{x + 1} {y - 6} C{x + 4} {y - 12} {x + 9} {y - 12} {x + 10} {y - 8} C{x + 7} {y - 9.4} {x + 5} {y - 8} {x + 3.5} {y - 5} Z" fill="url(#red)" {S(.9)}/>'
                f'<path d="M{x - 5} {y + 1} C{x - 5.2} {y - 7.4} {x + 5.2} {y - 7.4} {x + 5} {y + 1} L{x + 3.6} {y + 1} L{x + 3.6} {y - 1.2} L{x - 3.6} {y - 1.2} L{x - 3.6} {y + 1} Z" fill="url(#gold)" {S(1.1)}/>')
    return ''


def horse_head(x=0, y=0, s=1, coat='horse', barding=None, chanfron=False, mane='#3a200c', bridle=True):
    """Horse head and neck facing left; local box about 6..30 x 5..36."""
    g = (f'<path d="M29 36 C28.5 27 27 19 23 12 L22.6 6.2 L20.4 10.2 C17 10.8 14 13.6 10.2 17.8 '
         f'C8 20 6 21.6 6.3 23.8 C6.6 25.6 9 26.2 11.6 24.9 C14 23.8 16 24 17.4 25.6 C19 28.4 19.6 31.5 19.6 36 Z" '
         f'fill="url(#{coat})" {S(1.3)}/>'
         f'<path d="M23 12 C26 15 28.6 20 29.6 27 L31.6 27 C30.6 19 28 13.6 24 10.4 Z" fill="{mane}" {S(.9)}/>'
         f'<circle cx="16.3" cy="15.2" r="1" fill="{K}"/><circle cx="16" cy="14.9" r=".35" fill="#fff"/>'
         f'<path d="M7.4 22.6 C8 22.2 8.6 22.4 8.8 23" fill="none" stroke="{K}" stroke-width=".8"/>')
    if chanfron:
        g += (f'<path d="M20 11 C16.5 11.6 13 14.4 9.6 18.4 L11.6 20.4 C14.6 17.6 17.6 15 21 14 Z" fill="url(#steel)" {S(.9)}/>'
              f'<path d="M20.8 11 L22 6.5 L23.5 11.5 Z" fill="url(#steel)" {S(.7)}/>')
    if bridle:
        g += (f'<path d="M10.4 18.8 L12.4 23.6 M12 19.6 C14.6 18 17.5 17.4 20.4 17.4 L21.6 12.4" fill="none" stroke="#3a1f0a" stroke-width="1.1"/>')
    if barding:
        g += (f'<path d="M18.6 28 C22 26.4 26 25.4 29.4 25.8 L30 36 L19.4 36 Z" fill="url(#{barding})" {S(1.1)}/>'
              f'<path d="M19.2 30 C22.4 28.6 26 27.8 29.6 28.2" fill="none" stroke="#f0d080" stroke-width=".8"/>')
    return T(x, y, 0, s, g)


def flame(x, y, s=1):
    g = (f'<path d="M0 0 C-5 0 -6.5 -4.5 -4.5 -8.5 C-3.8 -6 -2.6 -5.5 -2 -6 C-3 -9.5 -1 -13 2 -15 C1.4 -12 3.6 -10.4 4.5 -8 '
         f'C5 -9 5.4 -10 5.2 -11 C7.4 -8 7 -1 0 0 Z" fill="url(#fire)" {S(1)}/>'
         f'<path d="M0 -1.6 C-2.4 -1.6 -3 -4 -1.8 -6 C-1.2 -4.6 0 -4.5 .2 -5.6 C1 -7.6 2.6 -8 2.6 -8 C2.2 -6 3.8 -4.8 2.6 -2.8 C2 -1.9 1 -1.6 0 -1.6 Z" fill="#fff6b0"/>')
    return T(x, y, 0, s, g)


def water(y=27):
    return (f'<path d="M0 {y} C4 {y - 1} 8 {y + 1} 12 {y} C16 {y - 1} 20 {y + 1} 24 {y} C28 {y - 1} 32 {y + 1} 36 {y} L36 36 L0 36 Z" fill="url(#water)"/>'
            f'<path d="M3 {y + 3} q2 -1 4 0 M14 {y + 5} q2 -1 4 0 M25 {y + 3} q2 -1 4 0" fill="none" stroke="#bfe0ff" stroke-width=".7" stroke-opacity=".8"/>')


def wheel(x, y, r=4, fill='oak'):
    return (f'<circle cx="{x}" cy="{y}" r="{r}" fill="url(#{fill})" {S(1.1)}/>'
            f'<circle cx="{x}" cy="{y}" r="{r - 1.4}" fill="none" stroke="#3a200a" stroke-width=".7"/>'
            f'<path d="M{x - r + 1} {y} H{x + r - 1} M{x} {y - r + 1} V{y + r - 1}" stroke="#3a200a" stroke-width=".7"/>'
            f'<circle cx="{x}" cy="{y}" r="1" fill="url(#iron)" {S(.6)}/>')


def sparkle(x, y, s=1, c='#fff6c0'):
    return T(x, y, 0, s, f'<path d="M0 -3 L.7 -.7 L3 0 L.7 .7 L0 3 L-.7 .7 L-3 0 L-.7 -.7 Z" fill="{c}"/>')


def star_path(cx, cy, R, r, n=5, rot=-90):
    import math
    pts = []
    for k in range(2 * n):
        rr = R if k % 2 == 0 else r
        a = math.radians(rot + k * 180 / n)
        pts.append(f'{cx + rr * math.cos(a):.2f} {cy + rr * math.sin(a):.2f}')
    return 'M' + ' L'.join(pts) + ' Z'
