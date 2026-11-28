from lib import *

C = 'buildings'


def shadow(cx=18, rx=14, cy=30.4):
    return f'<ellipse cx="{cx}" cy="{cy}" rx="{rx}" ry="2" fill="#000" fill-opacity=".35"/>'


def wall(x, y, w, h, fill='stone', courses=True):
    g = f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="url(#{fill})" {S(1.1)}/>'
    if courses and fill == 'stone':
        for k, yy in enumerate(range(int(y) + 3, int(y + h), 3)):
            g += f'<path d="M{x + .6} {yy} H{x + w - .6}" stroke="#7a7266" stroke-width=".45"/>'
    return g


def timber(x, y, w, h):
    """Half-timbered wall: cream plaster with dark beams."""
    return (f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="url(#cream)" {S(1.1)}/>'
            f'<path d="M{x} {y + h / 2} H{x + w} M{x + w / 3} {y} V{y + h} M{x + 2 * w / 3} {y} V{y + h} '
            f'M{x} {y} L{x + w / 3} {y + h / 2} M{x + w} {y} L{x + 2 * w / 3} {y + h / 2}" stroke="#5a3410" stroke-width=".9"/>')


def hip_roof(x1, x2, y, h, fill='roof', over=1.6):
    g = f'<path d="M{x1 - over} {y + h} L{x1 + 2.5} {y} L{x2 - 2.5} {y} L{x2 + over} {y + h} Z" fill="url(#{fill})" {S(1.1)}/>'
    for f in (.4, .75):
        d = (2.5 + over) * f
        g += f'<path d="M{x1 + 2.5 - d + .5:.2f} {y + h * f:.2f} H{x2 - 2.5 + d - .5:.2f}" stroke="#000" stroke-opacity=".3" stroke-width=".45"/>'
    return g


def gable(x1, x2, yb, yp, fill='roof'):
    xm = (x1 + x2) / 2
    return f'<path d="M{x1 - 1.4} {yb} L{xm} {yp} L{x2 + 1.4} {yb} Z" fill="url(#{fill})" {S(1.1)}/>'


def door(x, y, w=4, h=5):
    return (f'<path d="M{x} {y + h} V{y + w / 2} A{w / 2} {w / 2} 0 0 1 {x + w} {y + w / 2} V{y + h} Z" fill="url(#dwood)" {S(.9)}/>'
            f'<path d="M{x + w / 2} {y + .6} V{y + h}" stroke="#3a200a" stroke-width=".5"/>')


def win(x, y, w=2, h=2.6, lit=False):
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{"#ffd060" if lit else "#1e1612"}" {S(.6)}/>'


def crenel(x1, x2, y, n=None, fill='stone'):
    w = x2 - x1
    n = n or max(2, int(w // 3.2))
    step = w / (2 * n - 1)
    return ''.join(f'<rect x="{x1 + 2 * k * step:.2f}" y="{y - 2.2}" width="{step:.2f}" height="2.4" fill="url(#{fill})" {S(.8)}/>' for k in range(n))


def banner(x, y, color='team', h=7):
    return (f'<path d="M{x} {y} V{y - h}" stroke="{K}" stroke-width="1.4"/><path d="M{x} {y} V{y - h}" stroke="#d39a52" stroke-width=".6"/>'
            f'<path d="M{x + .4} {y - h} L{x + 5} {y - h + 1.4} L{x + .4} {y - h + 3} Z" fill="url(#{color})" {S(.7)}/>')


def round_tower(x, y, w, h, roof='roof', cone=6):
    return (wall(x, y, w, h) + f'<path d="M{x - 1} {y + .4} L{x + w / 2} {y - cone} L{x + w + 1} {y + .4} Z" fill="url(#{roof})" {S(1)}/>')


def ground_water(y=26.5):
    return water(y)


icon(C, 'town_center', ['town center', 'tc', 'main building', 'villagers', 'economy'],
     shadow(rx=15)
     + wall(4, 19, 28, 10.4) + hip_roof(4, 32, 14, 5, 'roof')
     + timber(12, 9, 12, 6) + gable(12, 24, 9.2, 3, 'team')
     + banner(7, 14.4, 'team', 6) + banner(29, 14.4, 'team', 6)
     + door(16, 22.6, 4, 6.8) + win(7.4, 21.6, 2, 2.6, True) + win(26.6, 21.6, 2, 2.6, True)
     + win(14.6, 10.6, 1.8, 2.4, True) + win(19.6, 10.6, 1.8, 2.4, True))

icon(C, 'house', ['house', 'population', 'home', 'economy', 'dwelling'],
     shadow(rx=11) + timber(9, 16, 18, 13) + gable(9, 27, 16.6, 6, 'thatch')
     + f'<path d="M9.4 15.6 L18 7 L26.6 15.6" fill="none" stroke="#7a561c" stroke-width=".6"/>'
     + door(16, 22.4, 4, 6.6) + win(11, 18.6, 2.6, 2.6, True) + win(22.4, 18.6, 2.6, 2.6, True)
     + f'<rect x="22" y="5.6" width="2.6" height="4" fill="url(#stone)" {S(.8)}/>')

icon(C, 'barracks', ['barracks', 'infantry', 'military', 'training'],
     shadow(rx=15) + wall(3, 15, 30, 14.4) + hip_roof(3, 33, 9, 6, 'roof')
     + door(15.6, 21.4, 4.8, 8) + win(6, 18, 2, 3) + win(28, 18, 2, 3)
     + kite_shield(9.6, 22.4, .55) + kite_shield(26.4, 22.4, .55, 'red')
     + sword(18, 7.4, 0, 6, .7) + banner(4.4, 9, 'team', 6))

icon(C, 'archery_range', ['archery range', 'archers', 'military', 'training'],
     shadow(rx=15) + f'<path d="M3 29 V13 M15 29 V13" stroke="{K}" stroke-width="2.6"/><path d="M3 29 V13 M15 29 V13" stroke="#a86a30" stroke-width="1.2"/>'
     + f'<path d="M1 14 L9 7 L17 14 Z" fill="url(#thatch)" {S(1)}/>'
     + f'<path d="M3 21 H15" stroke="{K}" stroke-width="2"/><path d="M3 21 H15" stroke="#a86a30" stroke-width=".9"/>'
     + f'<path d="M20 30 L24 21 M30 30 L26 21" stroke="{K}" stroke-width="1.8"/><path d="M20 30 L24 21 M30 30 L26 21" stroke="#a86a30" stroke-width=".8"/>'
     + f'<circle cx="25" cy="17" r="7" fill="url(#thatch)" {S(1.1)}/><circle cx="25" cy="17" r="4.8" fill="#f8f2e0" {S(.6)}/>'
       f'<circle cx="25" cy="17" r="3" fill="url(#red)" {S(.6)}/><circle cx="25" cy="17" r="1.2" fill="#f8f2e0"/>'
     + f'<path d="M26 16 L31 11" stroke="{K}" stroke-width="1.6"/><path d="M26 16 L31 11" stroke="#e8d0a0" stroke-width=".7"/>'
       f'<path d="M31 11 L33 9.6 M31 11 L32.4 12.4 M31 11 L29.6 9.4" stroke="#c03424" stroke-width=".8"/>')

icon(C, 'stable', ['stable', 'cavalry', 'horses', 'military', 'training'],
     shadow(rx=15) + f'<rect x="5" y="14" width="22" height="15.4" fill="url(#oak)" {S(1.1)}/>'
     + ''.join(f'<path d="M{x} 14.6 V29" stroke="#5a3410" stroke-width=".6"/>' for x in range(8, 27, 3))
     + gable(5, 27, 14.6, 5, 'roof')
     + f'<path d="M11 29.4 V20 C11 17 21 17 21 20 V29.4 Z" fill="url(#dwood)" {S(1)}/>'
       f'<path d="M11 20 L21 29.4 M21 20 L11 29.4" stroke="#8a5520" stroke-width=".9"/>'
     + f'<path d="M13.6 12.6 C13.6 8.4 18.4 8.4 18.4 12.6" fill="none" stroke="{K}" stroke-width="2.8"/>'
       f'<path d="M13.6 12.6 C13.6 8.4 18.4 8.4 18.4 12.6" fill="none" stroke="#c8ccd4" stroke-width="1.4"/>'
     + f'<path d="M26 22 H34 M26 26 H34 M28 20 V30 M32 20 V30" stroke="{K}" stroke-width="1.8"/>'
       f'<path d="M26 22 H34 M26 26 H34 M28 20 V30 M32 20 V30" stroke="#c88a50" stroke-width=".8"/>')

icon(C, 'siege_workshop', ['siege workshop', 'siege', 'engineering', 'military'],
     shadow(rx=15) + f'<rect x="3" y="15" width="21" height="14.4" fill="url(#oak)" {S(1.1)}/>'
     + ''.join(f'<path d="M3.4 {y} H23.6" stroke="#5a3410" stroke-width=".6"/>' for y in (18.6, 22.2, 25.8))
     + hip_roof(3, 24, 10, 5, 'thatch')
     + f'<path d="M24 29 L30 6" stroke="{K}" stroke-width="2.6"/><path d="M24 29 L30 6" stroke="#a86a30" stroke-width="1.2"/>'
       f'<path d="M30 6 L20 8.6" stroke="{K}" stroke-width="2"/><path d="M30 6 L20 8.6" stroke="#a86a30" stroke-width=".9"/>'
       f'<path d="M21 8.4 V14" stroke="#e8d8a8" stroke-width=".6"/>'
     + wheel(27.6, 24.4, 5.2) + door(8, 21, 6, 8.4))

icon(C, 'castle', ['castle', 'fortress', 'stronghold', 'military', 'unique units'],
     shadow(rx=15) + wall(8, 13, 20, 16.4) + crenel(8, 28, 13)
     + wall(3, 9, 7, 20.4) + crenel(3, 10, 9, 2) + wall(26, 9, 7, 20.4) + crenel(26, 33, 9, 2)
     + wall(14, 6, 8, 8) + crenel(14, 22, 6, 2)
     + f'<path d="M14.6 29.4 V24 C14.6 20.4 21.4 20.4 21.4 24 V29.4 Z" fill="#1e1612" {S(.9)}/>'
       f'<path d="M15.4 22.6 V29.4 M17 21.4 V29.4 M18.6 21.4 V29.4 M20.2 22.2 V29.4 M15 24.6 H21 M15 27 H21" stroke="#8a8e96" stroke-width=".55"/>'
     + win(5.6, 14, 1.6, 3) + win(28.6, 14, 1.6, 3) + win(17.2, 8.6, 1.6, 2.6)
     + banner(18, 3.8, 'team', 3.6) + banner(6.4, 6.4, 'red', 4) + banner(29.4, 6.4, 'red', 4))

icon(C, 'wonder', ['wonder', 'monument', 'victory', 'cathedral', 'great building'],
     '<circle cx="18" cy="12" r="12" fill="url(#glow)" opacity=".7"/>'
     + shadow(rx=15)
     + wall(4, 19, 28, 10.4) + crenel(4, 32, 19, 6)
     + wall(10, 13, 16, 7, courses=False)
     + f'<path d="M10.4 13.4 C10.4 5 25.6 5 25.6 13.4 Z" fill="url(#gold)" {S(1.1)}/>'
       f'<path d="M14 11 C14 8 16 6.6 18 6.4" fill="none" stroke="#fffbe0" stroke-width="1" stroke-opacity=".8"/>'
       f'<path d="M18 6.2 V2.6 M16.6 3.8 H19.4" stroke="{K}" stroke-width="1.8"/><path d="M18 6.2 V2.6 M16.6 3.8 H19.4" stroke="#ffe080" stroke-width=".8"/>'
     + round_tower(3, 11, 4, 8.4, 'gold', 6) + round_tower(29, 11, 4, 8.4, 'gold', 6)
     + ''.join(f'<path d="M{x} 29.4 V24 A1.4 1.4 0 0 1 {x + 2.8} 24 V29.4 Z" fill="#1e1612" {S(.6)}/>' for x in (7, 12, 21.2, 26.2))
     + door(16, 22.4, 4, 7) + win(13, 15, 1.6, 3, True) + win(17.2, 15, 1.6, 3, True) + win(21.4, 15, 1.6, 3, True))

icon(C, 'market', ['market', 'trade', 'economy', 'gold', 'merchant', 'shop'],
     shadow(rx=15) + timber(4, 13, 14, 16.4) + gable(4, 18, 13.6, 5, 'roof')
     + f'<path d="M17 18 L33 18 L33 29.4 L17 29.4 Z" fill="url(#oak)" {S(1)}/>'
     + ''.join(f'<path d="M{15 + k * 3.8} 12 L{18.8 + k * 3.8} 12 L{18.4 + k * 3.8} 18.6 L{14.4 + k * 3.8} 18.6 Z" fill="{["#c03424", "#f4ecd8"][k % 2]}" {S(.6)}/>' for k in range(5))
     + f'<path d="M14.4 18.6 Q16.4 20.4 18.4 18.6 Q20.4 20.4 22.2 18.6 Q24.2 20.4 26 18.6 Q28 20.4 30 18.6 Q32 20.4 33.8 18.6" fill="#c03424" {S(.6)}/>'
     + f'<rect x="18" y="23" width="14" height="6.4" fill="url(#dwood)" {S(.9)}/>'
     + f'<ellipse cx="21" cy="22.4" rx="2.2" ry="1.6" fill="url(#ruby)" {S(.6)}/><ellipse cx="25.4" cy="22.2" rx="2.2" ry="1.6" fill="url(#gold)" {S(.6)}/>'
       f'<path d="M28 23 C27.4 20 29 19.4 29.8 19.4 C30.6 19.4 32.2 20 31.6 23 Z" fill="url(#leather)" {S(.6)}/>'
     + door(8.6, 22.4, 4.4, 7) + win(6, 15.6, 2, 2.4, True))

icon(C, 'monastery', ['monastery', 'church', 'monks', 'religion', 'faith', 'relics'],
     shadow(rx=15) + wall(4, 16, 18, 13.4) + gable(4, 22, 16.4, 8, 'roof')
     + wall(22, 9, 9, 20.4) + f'<path d="M21 9.4 L26.5 2.6 L32 9.4 Z" fill="url(#roof)" {S(1)}/>'
     + f'<path d="M26.5 2.6 V-1" stroke="{K}" stroke-width="1.6"/>'
     + f'<circle cx="13" cy="20.6" r="2.8" fill="url(#crystal)" {S(.8)}/><path d="M13 17.8 V23.4 M10.2 20.6 H15.8" stroke="#1a4e98" stroke-width=".6"/>'
     + f'<path d="M24.6 16.4 V13.6 A1.9 1.9 0 0 1 28.4 13.6 V16.4 Z" fill="#1e1612" {S(.6)}/>'
       f'<path d="M25.4 14.6 C25.4 13 27.6 13 27.6 14.6 L28 16 L25 16 Z" fill="url(#brass)"/>'
     + door(11, 24, 4, 5.4) + door(24.6, 23, 3.8, 6.4)
     + f'<path d="M13 4.6 V10.4 M10.8 6.4 H15.2" stroke="{K}" stroke-width="2.4"/><path d="M13 4.6 V10.4 M10.8 6.4 H15.2" stroke="#ffe080" stroke-width="1.1"/>')

icon(C, 'university', ['university', 'research', 'technology', 'knowledge', 'school'],
     shadow(rx=15) + wall(4, 15, 28, 14.4)
     + f'<path d="M10 15.4 C10 4.6 26 4.6 26 15.4 Z" fill="url(#team)" {S(1.1)}/>'
       f'<path d="M14 13 C14 9 16 7.4 18 7.2" fill="none" stroke="#d0e4ff" stroke-width="1" stroke-opacity=".8"/>'
       f'<rect x="16.8" y="3" width="2.4" height="3.6" fill="url(#gold)" {S(.7)}/>'
     + f'<rect x="3" y="14.4" width="30" height="2" fill="url(#stone)" {S(.8)}/>'
     + ''.join(f'<rect x="{x}" y="16.4" width="2" height="13" fill="url(#bone)" {S(.6)}/>' for x in (6, 10.4, 23.6, 28))
     + door(15.4, 21, 5.2, 8.4)
     + T(18, 19, 0, .42, f'<path d="M-7 -3 L0 -1 L7 -3 L7 5 L0 7 L-7 5 Z" fill="url(#paper)" {S(1.4)}/><path d="M0 -1 V7" stroke="{K}" stroke-width="1.2"/>'))

icon(C, 'blacksmith', ['blacksmith', 'forge', 'smithy', 'upgrades', 'armor', 'weapons'],
     shadow(rx=15) + f'<circle cx="27" cy="5" r="3.4" fill="url(#smoke)" opacity=".85"/><circle cx="30.6" cy="3" r="2.4" fill="url(#smoke)" opacity=".7"/>'
     + wall(24.6, 7.6, 4.6, 10) + timber(4, 15, 22, 14.4) + hip_roof(4, 26, 10, 5.4, 'roof')
     + f'<rect x="6.6" y="19.6" width="7" height="9.8" fill="#2a1608" {S(.9)}/>'
       f'<path d="M7.6 29.4 C8 25 12.4 25 12.6 29.4 Z" fill="url(#fire)"/>'
     + f'<path d="M18 26 C19 26.6 20 26.6 21 26.4 L21 25 L29.6 25 L29.6 26.6 L27.6 27 L27.4 29 L29 29.6 L22.6 29.6 L24 29 L23.6 27.2 C21 27.6 19 27.4 18 26 Z" fill="url(#iron)" {S(.9)}/>'
     + sparkle(26, 22.4, .8, '#ffd060') + sparkle(30, 21, .55, '#ffb030'))

icon(C, 'mill', ['mill', 'windmill', 'food', 'farms', 'economy'],
     shadow(rx=10) + f'<path d="M12 29.4 L14.6 13 L21.4 13 L24 29.4 Z" fill="url(#stone)" {S(1.1)}/>'
     + f'<path d="M13.4 13.4 L18 7.6 L22.6 13.4 Z" fill="url(#thatch)" {S(1)}/>' + door(16, 23.6, 4, 5.8)
     + T(18, 12, 20, 1, ''.join(T(0, 0, a, 1, f'<rect x="-.7" y="-12.6" width="1.4" height="12.6" fill="url(#dwood)" {S(.6)}/>'
                                   f'<rect x=".7" y="-12" width="3.6" height="9" fill="url(#cream)" {S(.7)}/>'
                                   f'<path d="M.7 -9 H4.3 M.7 -6 H4.3 M2.5 -12 V-3" stroke="#8e7a52" stroke-width=".4"/>') for a in (0, 90, 180, 270)))
     + f'<circle cx="18" cy="12" r="1.5" fill="url(#iron)" {S(.7)}/>')

icon(C, 'lumber_camp', ['lumber camp', 'wood', 'lumberjack', 'drop off', 'economy'],
     shadow(rx=15) + f'<path d="M3 29.4 L5 14 L20 14 L20 29.4 Z" fill="url(#oak)" {S(1.1)}/>'
     + f'<path d="M2 15 L6 9 L21.6 9 L21.6 15 Z" fill="url(#thatch)" {S(1)}/>'
     + f'<path d="M6 29.4 V18 H18 V29.4" fill="#2a1608" {S(.8)}/>'
     + ''.join(f'<circle cx="{x}" cy="{y}" r="2.4" fill="url(#leather)" {S(.8)}/><circle cx="{x}" cy="{y}" r="1.4" fill="#f0cf94"/><circle cx="{x}" cy="{y}" r=".4" fill="#8a5520"/>'
               for x, y in [(8.6, 27.4), (13.4, 27.4), (11, 23.4), (15.8, 23.4)][::1])
     + f'<path d="M22 30 L22 25.4 C24 24.6 28 24.6 30 25.4 L30 30 Z" fill="url(#oak)" {S(1)}/>'
       f'<ellipse cx="26" cy="25.4" rx="4" ry="1.4" fill="#f0cf94" {S(.8)}/>'
     + axe(25.4, 25.6, 25, 10, .95))

icon(C, 'mining_camp', ['mining camp', 'gold', 'stone', 'mine', 'drop off', 'economy'],
     shadow(rx=15) + f'<path d="M2 29.4 C2 18 7 10 14 9 C20 9 26 14 27 29.4 Z" fill="url(#stone)" {S(1.1)}/>'
       f'<path d="M6 22 L9 20 M20 16 L23 18.6 M10 13.4 L13 13" stroke="#6a6256" stroke-width=".7"/>'
     + f'<path d="M9 29.4 V20 H19 V29.4 Z" fill="#160e08" {S(.9)}/>'
       f'<path d="M9 29.4 V19.4 H19.4 V29.4" fill="none" stroke="{K}" stroke-width="2.8"/><path d="M9 29.4 V19.4 H19.4 V29.4" fill="none" stroke="#a86a30" stroke-width="1.3"/>'
     + f'<path d="M22 21 L33 21 L31.6 27 L23.4 27 Z" fill="url(#oak)" {S(1)}/>'
     + ''.join(f'<circle cx="{x}" cy="{y}" r="1.7" fill="url(#gold)" {S(.6)}/>' for x, y in [(24.6, 20.4), (27.4, 19.8), (30.4, 20.4), (26, 18.4), (29, 18.2)])
     + wheel(25, 28.4, 1.9, 'iron') + wheel(30, 28.4, 1.9, 'iron')
     + T(14, 16, -30, .8, f'<rect x="-.8" y="-1" width="1.6" height="11" fill="url(#oak)" {S(.7)}/>'
         f'<path d="M-7 0 C-3 -3 3 -3 7 0 C3 -1.6 -3 -1.6 -7 0 Z" fill="url(#iron)" {S(.8)}/>'))

icon(C, 'dock', ['dock', 'ships', 'navy', 'harbor', 'water', 'fishing'],
     ground_water(24)
     + f'<path d="M2 22 L22 22 L22 25 L2 25 Z" fill="url(#oak)" {S(1)}/>'
     + ''.join(f'<rect x="{x}" y="25" width="2" height="8" fill="url(#dwood)" {S(.7)}/>' for x in (4, 11, 18))
     + timber(4, 12, 12, 10) + gable(4, 16, 12.4, 6, 'roof')
     + f'<path d="M20 22 V5 L31 9" fill="none" stroke="{K}" stroke-width="2.6"/><path d="M20 22 V5 L31 9" fill="none" stroke="#a86a30" stroke-width="1.2"/>'
       f'<path d="M30 8.6 V15" stroke="#e8d8a8" stroke-width=".6"/><rect x="28.4" y="15" width="3.4" height="3" fill="url(#oak)" {S(.7)}/>'
     + f'<path d="M23 25.4 L33 25.4 L31.6 28.6 L24.6 28.6 Z" fill="url(#oak)" {S(.9)}/>')

icon(C, 'watch_tower', ['watch tower', 'tower', 'defense', 'vision', 'outpost', 'wooden'],
     shadow(rx=8) + f'<path d="M11 30 L15 13 M25 30 L21 13 M12.5 24 L23.5 24 M13.6 19 L22.4 19 M12.4 24 L22.4 19 M23.6 24 L13.6 19" fill="none" stroke="{K}" stroke-width="2.2"/>'
       f'<path d="M11 30 L15 13 M25 30 L21 13 M12.5 24 L23.5 24 M13.6 19 L22.4 19 M12.4 24 L22.4 19 M23.6 24 L13.6 19" fill="none" stroke="#a86a30" stroke-width="1"/>'
     + f'<rect x="10" y="10" width="16" height="4" fill="url(#oak)" {S(1)}/>'
       f'<path d="M11 10 V6 M25 10 V6" stroke="{K}" stroke-width="1.6"/>'
       f'<path d="M8.6 6.6 L18 1.6 L27.4 6.6 Z" fill="url(#thatch)" {S(1)}/>'
     + f'<circle cx="18" cy="8" r="1.6" fill="url(#fire)"/>')

icon(C, 'guard_tower', ['guard tower', 'tower', 'defense', 'keep', 'stone'],
     shadow(rx=9) + wall(11, 11, 14, 18.4) + crenel(10, 26, 11.4, 4)
     + f'<path d="M10 9.4 L18 1.4 L26 9.4 Z" fill="url(#roof)" {S(1)}/>'
     + f'<path d="M10.4 9 H25.6" stroke="{K}" stroke-width="1"/>'
     + win(16.8, 14, 2.4, 4) + door(15.6, 23, 4.8, 6.4) + banner(18, 1.6, 'team', 0.1))

icon(C, 'bombard_tower', ['bombard tower', 'tower', 'gunpowder', 'cannon', 'defense'],
     shadow(rx=12) + wall(8, 12, 20, 17.4) + crenel(7, 29, 12.4, 5)
     + f'<rect x="7" y="20" width="22" height="2.4" fill="url(#stone)" {S(.8)}/>'
     + f'<circle cx="18" cy="16.6" r="3.2" fill="#1e1612" {S(.9)}/>'
       f'<path d="M14 16.6 L4 17.4 L4 21 L14 19.6 Z" fill="url(#iron)" {S(.9)}/><ellipse cx="4" cy="19.2" rx="1" ry="1.8" fill="#120e0c" {S(.6)}/>'
     + f'<circle cx="3.6" cy="12" r="2" fill="url(#smoke)" opacity=".8"/>'
     + door(16, 24, 4, 5.4))

icon(C, 'gate', ['gate', 'wall', 'gatehouse', 'portcullis', 'defense'],
     shadow(rx=15) + wall(2, 15, 9, 14.4) + crenel(2, 11, 15, 2) + wall(25, 15, 9, 14.4) + crenel(25, 34, 15, 2)
     + wall(9, 8, 18, 21.4) + crenel(9, 27, 8, 4)
     + f'<path d="M12 29.4 V19 C12 13.6 24 13.6 24 19 V29.4 Z" fill="#1e1612" {S(1)}/>'
     + ''.join(f'<path d="M{x} {17 if 14 < x < 22 else 18.4} V29.4" stroke="#9a9ea6" stroke-width=".8"/>' for x in (13.6, 16.2, 18.8, 21.4))
     + ''.join(f'<path d="M12.6 {y} H23.4" stroke="#9a9ea6" stroke-width=".8"/>' for y in (20, 23.4, 26.8))
     + banner(18, 5.6, 'team', 0.1))

icon(C, 'stone_wall', ['wall', 'stone wall', 'fortification', 'defense'],
     shadow(rx=16) + f'<path d="M0 14 L36 14 L36 29.4 L0 29.4 Z" fill="url(#stone)" {S(1.1)}/>'
     + ''.join(f'<path d="M0 {y} H36" stroke="#7a7266" stroke-width=".55"/>' for y in (18, 22, 26))
     + ''.join(f'<path d="M{x + (3 if r % 2 else 0)} {14 + 4 * r} V{18 + 4 * r}" stroke="#7a7266" stroke-width=".55"/>' for r in range(4) for x in range(3, 36, 6))
     + crenel(1, 35, 14, 6)
     + f'<path d="M2 15 H34" stroke="#fffbe8" stroke-width=".7" stroke-opacity=".6"/>')

icon(C, 'palisade_wall', ['wall', 'palisade', 'wooden wall', 'fortification', 'defense'],
     shadow(rx=16) + ''.join(
         f'<path d="M{x} 30 L{x} {12 + (k % 3) * 1.4} L{x + 2.2} {8 + (k % 3) * 1.4} L{x + 4.4} {12 + (k % 3) * 1.4} L{x + 4.4} 30 Z" fill="url(#oak)" {S(1)}/>'
         f'<path d="M{x + 1.4} {14 + (k % 3)} V29" stroke="#5a3410" stroke-width=".5"/>'
         for k, x in enumerate([1, 5.4, 9.8, 14.2, 18.6, 23, 27.4, 31.8]))
     + f'<path d="M0 17 H36 M0 25 H36" stroke="{K}" stroke-width="2"/><path d="M0 17 H36 M0 25 H36" stroke="#8a5520" stroke-width=".9"/>')

icon(C, 'temple', ['temple', 'shrine', 'religion', 'faith', 'ancient', 'greek'],
     shadow(rx=15) + f'<rect x="3" y="27" width="30" height="3" fill="url(#stone)" {S(.9)}/><rect x="5" y="24.6" width="26" height="2.6" fill="url(#stone)" {S(.8)}/>'
     + ''.join(f'<rect x="{x}" y="14" width="2.6" height="10.6" fill="url(#bone)" {S(.7)}/>' for x in (6, 11, 16.7, 22.4, 27.4))
     + f'<rect x="4" y="11.4" width="28" height="2.8" fill="url(#stone)" {S(.9)}/>'
       f'<path d="M3 11.6 L18 3.6 L33 11.6 Z" fill="url(#stone)" {S(1.1)}/>'
       f'<path d="M9 10.4 L18 5.8 L27 10.4 Z" fill="#a09888" {S(.5)}/>'
       f'<circle cx="18" cy="8.4" r="1.4" fill="url(#gold)" {S(.5)}/>')

icon(C, 'shrine', ['shrine', 'torii', 'religion', 'faith', 'japanese', 'sacred'],
     shadow(rx=13)
     + f'<path d="M2 8 C10 9.6 26 9.6 34 8 L33 11.4 C26 12.4 10 12.4 3 11.4 Z" fill="url(#red)" {S(1.1)}/>'
       f'<rect x="1.6" y="6.6" width="32.8" height="2.2" rx="1" fill="url(#black)" {S(.9)}/>'
       f'<rect x="6" y="15" width="24" height="2.4" fill="url(#red)" {S(.9)}/>'
       f'<rect x="9" y="11" width="3" height="19" fill="url(#red)" {S(1)}/><rect x="24" y="11" width="3" height="19" fill="url(#red)" {S(1)}/>'
       f'<rect x="8.4" y="28" width="4.2" height="2" fill="url(#black)" {S(.7)}/><rect x="23.4" y="28" width="4.2" height="2" fill="url(#black)" {S(.7)}/>'
       f'<rect x="16.6" y="11.6" width="2.8" height="3.4" fill="url(#black)" {S(.7)}/>'
     + f'<path d="M16 30 L17 22 L19 22 L20 30 Z" fill="url(#stone)" {S(.8)}/><rect x="15" y="20" width="6" height="2.4" fill="url(#stone)" {S(.8)}/>'
       f'<rect x="16.4" y="22.6" width="3.2" height="2.2" fill="#ffd060"/>')

icon(C, 'outpost', ['outpost', 'vision', 'scout', 'tower', 'cheap', 'wooden'],
     shadow(rx=7) + f'<path d="M14 30 L17 16 M22 30 L19 16 M15 24 L21 24" fill="none" stroke="{K}" stroke-width="2"/>'
       f'<path d="M14 30 L17 16 M22 30 L19 16 M15 24 L21 24" fill="none" stroke="#a86a30" stroke-width=".9"/>'
     + f'<path d="M12 16 L24 16 L22.6 19 L13.4 19 Z" fill="url(#oak)" {S(1)}/>'
       f'<path d="M13.6 16 C13.6 13 22.4 13 22.4 16 Z" fill="url(#iron)" {S(.9)}/>'
     + '<circle cx="18" cy="9" r="9" fill="url(#glow)" opacity=".8"/>' + flame(18, 14.4, .75))

icon(C, 'harbor', ['harbor', 'harbour', 'port', 'ships', 'navy', 'water', 'stone'],
     ground_water(23)
     + f'<path d="M0 19 L22 19 L24 30 L0 30 Z" fill="url(#stone)" {S(1.1)}/>'
       + ''.join(f'<path d="M0 {y} H{22 + (y - 19) * .18}" stroke="#7a7266" stroke-width=".5"/>' for y in (22, 25, 28))
     + wall(3, 9, 12, 10) + crenel(3, 15, 9, 3)
     + f'<rect x="25.4" y="3" width="1.6" height="21" fill="url(#dwood)" {S(.7)}/>'
       f'<path d="M27 5 L33 13 L27 17 Z" fill="url(#cream)" {S(.9)}/>'
       f'<path d="M21 22 L33 22 L31 27 L23 27 Z" fill="url(#oak)" {S(1)}/>'
     + T(9, 14.6, 0, .55, f'<circle cx="0" cy="-4" r="1.8" fill="none" {S(1.6)}/><path d="M0 -2.2 V6 M-4.4 3 C-3.6 6 3.6 6 4.4 3 M-2.6 -.4 H2.6" fill="none" stroke="{K}" stroke-width="3"/>'
         f'<circle cx="0" cy="-4" r="1.8" fill="none" stroke="#ffe080" stroke-width="1"/><path d="M0 -2.2 V6 M-4.4 3 C-3.6 6 3.6 6 4.4 3 M-2.6 -.4 H2.6" fill="none" stroke="#ffe080" stroke-width="1.4"/>'))

icon(C, 'granary', ['granary', 'silo', 'food', 'storage', 'economy'],
     shadow(rx=14) + f'<path d="M7 29.4 V15 C7 13.6 20 13.6 20 15 V29.4 Z" fill="url(#stone)" {S(1.1)}/>'
       f'<path d="M7.4 19 C11 20 16 20 19.6 19 M7.4 24 C11 25 16 25 19.6 24" fill="none" stroke="#7a7266" stroke-width=".55"/>'
       f'<path d="M5.4 15.4 L13.5 4 L21.6 15.4 C17 17 10 17 5.4 15.4 Z" fill="url(#thatch)" {S(1.1)}/>'
     + door(11.4, 23, 4.2, 6.4)
     + ''.join(f'<path d="M{x - 3} 30 C{x - 3.6} 26 {x - 2} 23.6 {x} 23.4 C{x + 2} 23.6 {x + 3.6} 26 {x + 3} 30 Z" fill="url(#cream)" {S(.9)}/><path d="M{x - 1} 24 L{x + 1} 24" stroke="#a88a50" stroke-width="1"/>'
               for x in (24.4, 29.6))
     + f'<path d="M27 22 C26.6 19 27.4 17.6 28.6 17.4 L30 22 Z" fill="url(#thatch)" {S(.7)}/>')

icon(C, 'farm', ['farm', 'field', 'food', 'wheat', 'crops', 'economy'],
     f'<path d="M0 16 L36 16 L36 36 L0 36 Z" fill="url(#leather)"/>'
     + ''.join(f'<path d="M{18 + (x - 18) * .45} 16 L{x} 36" stroke="#4e2c12" stroke-width="1.1"/>' for x in range(-14, 52, 7))
     + ''.join(T(x, y, 0, s, f'<path d="M0 0 V-8" stroke="#8a7020" stroke-width="1"/>'
                 f'<ellipse cx="0" cy="-8.6" rx="1.3" ry="2.8" fill="url(#thatch)" {S(.5)}/>'
                 f'<path d="M0 -3 L-2 -5.4 M0 -4.4 L2 -6.4" stroke="#a89030" stroke-width=".7"/>')
               for x, y, s in [(5, 34, 1.1), (11, 33, 1), (25, 33, 1), (31, 34, 1.1), (8, 25, .75), (28, 25, .75), (14, 22, .6), (22, 22, .6)])
     + f'<path d="M2 16 L13 16 L13 12 L7.5 9 L2 12 Z" fill="url(#cream)" {S(.8)}/><path d="M1 12.4 L7.5 8 L14 12.4" fill="none" stroke="#a83c22" stroke-width="1.4"/>')

icon(C, 'well', ['well', 'water', 'village', 'decoration', 'economy'],
     shadow(rx=11) + f'<path d="M10 9 V24 M26 9 V24" stroke="{K}" stroke-width="2.6"/><path d="M10 9 V24 M26 9 V24" stroke="#a86a30" stroke-width="1.2"/>'
     + gable(8, 28, 10, 3, 'roof')
     + f'<rect x="10" y="12.6" width="16" height="1.6" fill="url(#oak)" {S(.7)}/><path d="M18 14.2 V18" stroke="#e8d8a8" stroke-width=".6"/>'
       f'<path d="M16.4 18 L19.6 18 L19.2 21 L16.8 21 Z" fill="url(#oak)" {S(.7)}/>'
     + f'<path d="M7 21 L29 21 L29 29.4 L7 29.4 Z" fill="url(#stone)" {S(1.1)}/>'
       f'<ellipse cx="18" cy="21" rx="11" ry="2" fill="url(#stone)" {S(1)}/><ellipse cx="18" cy="21" rx="8.6" ry="1.2" fill="#1a3456"/>'
       f'<path d="M7.4 25 H28.6 M12 21.4 V25 M24 21.4 V25 M18 25 V29" stroke="#7a7266" stroke-width=".55"/>')

icon(C, 'tent', ['tent', 'camp', 'nomad', 'military camp', 'temporary'],
     shadow(rx=14) + f'<path d="M3 29.4 L18 6 L33 29.4 Z" fill="url(#cream)" {S(1.2)}/>'
     + ''.join(f'<path d="M{18 + (x - 18) * .0} 6.6 L{x} 29.4" stroke="#c03424" stroke-width="2.4"/>' for x in (8, 18, 28))
     + f'<path d="M3 29.4 L18 6 L33 29.4 Z" fill="none" {S(1.2)}/>'
     + f'<path d="M14 29.4 L18 17 L22 29.4 Z" fill="#2a1608" {S(.9)}/>'
     + banner(18, 6.4, 'team', 4.6))

icon(C, 'lighthouse', ['lighthouse', 'tower', 'coast', 'vision', 'water', 'beacon'],
     '<circle cx="18" cy="7" r="10" fill="url(#glow)"/>'
     + ground_water(25) + f'<path d="M6 30 C7 25 11 23 18 23 C25 23 29 25 30 30 L30 36 L6 36 Z" fill="url(#stone)" {S(1.1)}/>'
     + f'<path d="M13 24 L15 10 L21 10 L23 24 Z" fill="#f4ecd8" {S(1.1)}/>'
       f'<path d="M13.7 19 L22.3 19 L22.9 22.4 L13.2 22.4 Z M14.6 13 L21.4 13 L21.9 16.4 L14.1 16.4 Z" fill="url(#red)"/>'
       f'<path d="M13 24 L15 10 L21 10 L23 24 Z" fill="none" {S(1.1)}/>'
       f'<rect x="13.4" y="8.6" width="9.2" height="1.8" fill="url(#iron)" {S(.7)}/>'
       f'<rect x="15" y="4.6" width="6" height="4" fill="#ffe680" {S(.8)}/>'
       f'<path d="M14.4 4.8 L18 1.4 L21.6 4.8 Z" fill="url(#iron)" {S(.8)}/>')

icon(C, 'ruins', ['ruins', 'ancient', 'relic', 'decoration', 'broken', 'archaeology'],
     shadow(rx=15) + f'<rect x="5" y="10" width="4.6" height="19.4" fill="url(#bone)" {S(1)}/>'
       f'<path d="M5 10 L6.4 8 L8 9.4 L9.6 7.4 L9.6 10 Z" fill="url(#bone)" {S(.8)}/>'
       f'<rect x="4" y="27.6" width="6.6" height="2" fill="url(#stone)" {S(.8)}/>'
     + f'<rect x="20" y="17" width="4.6" height="12.4" fill="url(#bone)" {S(1)}/><path d="M20 17 L22 15 L24.6 16.4 V17 Z" fill="url(#bone)" {S(.8)}/>'
       f'<rect x="19" y="27.6" width="6.6" height="2" fill="url(#stone)" {S(.8)}/>'
     + f'<path d="M3 8.6 L17 6 L17.4 8.4 L3.4 11 Z" fill="url(#stone)" {S(.9)}/>'
     + f'<path d="M24 30 L26 25.6 L33 26.6 L32 30 Z" fill="url(#stone)" {S(.9)}/><path d="M12 30 L13 27.4 L17 27.8 L16.6 30 Z" fill="url(#stone)" {S(.8)}/>'
     + f'<path d="M6.4 12 V27 M7.8 12 V27 M21.4 18.6 V27 M23 18.6 V27" stroke="#b4aa88" stroke-width=".5"/>'
     + f'<path d="M9.6 22 C12 20 13 17 12.4 14" fill="none" stroke="#4a8a2e" stroke-width="1"/>')

icon(C, 'statue', ['statue', 'monument', 'decoration', 'hero', 'memorial'],
     shadow(rx=11) + f'<rect x="9" y="22" width="18" height="7.4" fill="url(#stone)" {S(1.1)}/><rect x="7.6" y="20" width="20.8" height="2.6" fill="url(#stone)" {S(.9)}/>'
       f'<rect x="7.6" y="27.4" width="20.8" height="2.4" fill="url(#stone)" {S(.9)}/>'
     + f'<path d="M14 20 L14.6 12 C14 11 15 9.6 16.4 9.6 L19.6 9.6 C21 9.6 22 11 21.4 12 L22 20 Z" fill="url(#gold)" {S(1)}/>'
       f'<circle cx="18" cy="7" r="2.6" fill="url(#gold)" {S(.9)}/>'
       f'<path d="M21 11 L25 4" stroke="{K}" stroke-width="2"/><path d="M21 11 L25 4" stroke="#ffe080" stroke-width=".9"/>'
       f'<path d="M15.4 20 V15 M20.6 20 V15" stroke="#8c5a0c" stroke-width=".6"/>')

icon(C, 'bridge', ['bridge', 'river', 'crossing', 'road', 'stone'],
     ground_water(26) + f'<path d="M0 14 L36 14 L36 30 L30 30 C30 22 24 20 18 20 C12 20 6 22 6 30 L0 30 Z" fill="url(#stone)" {S(1.1)}/>'
       f'<path d="M0 17 H36" stroke="#7a7266" stroke-width=".55"/>'
     + crenel(0, 36, 14, 8)
     + f'<path d="M6.6 29 C7 23 12 20.6 18 20.6 C24 20.6 29 23 29.4 29" fill="none" stroke="#7a7266" stroke-width=".8"/>'
     + f'<path d="M7 31 C9 30 12 32 14 31 C16 30 20 32 22 31 C24 30 27 32 29 31" fill="none" stroke="#bfe0ff" stroke-width=".7"/>')
