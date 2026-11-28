from lib import *

C = 'units'
SOLID = {'team': '#3a62c0', 'leather': '#8a5428', 'red': '#c03424', 'green': '#4a8a2e', 'cream': '#d8c49a',
         'robe': '#c06a1e', 'steel': '#a9b6c4', 'purple': '#8040c8', 'skin': '#d69a68', 'black': '#3a3432',
         'wolf': '#8a8680'}


def arm(x1, y1, x2, y2, cloth='team', hand=True):
    """Arm from the shoulder (x1, y1) to the hand (x2, y2); solid colours because a gradient on a
    straight stroke has an empty bounding box."""
    g = (f'<path d="M{x1} {y1} L{x2} {y2}" stroke="{K}" stroke-width="4.6" stroke-linecap="round"/>'
         f'<path d="M{x1} {y1} L{x2} {y2}" stroke="{SOLID[cloth]}" stroke-width="2.6" stroke-linecap="round"/>')
    return g + (hand_(x2, y2) if hand else '')


def hand_(x, y):
    return f'<circle cx="{x}" cy="{y}" r="1.8" fill="url(#skin)" {S(.9)}/>'


def pauldrons(cloth='steel'):
    return (f'<ellipse cx="10.4" cy="24.6" rx="4" ry="2.8" fill="url(#{cloth})" {S(1)}/>'
            f'<ellipse cx="25.6" cy="24.6" rx="4" ry="2.8" fill="url(#{cloth})" {S(1)}/>')


def hair(color='#6a4020', x=18, y=13.5):
    return f'<path d="M{x - 4.9} {y} C{x - 5.6} {y - 7} {x + 5.6} {y - 7} {x + 4.9} {y} C{x + 3} {y - 3.2} {x - 3} {y - 3.2} {x - 4.9} {y} Z" fill="{color}" {S(1)}/>'


def rider(x, y, s, inner):
    """Re-places a bust drawn at the standard spot (head at 18,13.5) so its head lands on (x, y)."""
    return f'<g transform="translate({x - 18 * s:.2f} {y - 13.5 * s:.2f}) scale({s})">{inner}</g>'


SHADOW = '<ellipse cx="18" cy="33" rx="13" ry="2.4" fill="#000" fill-opacity=".35"/>'

# ---------------- infantry ----------------
icon(C, 'militia', ['infantry', 'swordsman', 'militia', 'melee', 'cheap'],
     torso(cloth='leather') + belt(18, 30.5) + head() + helm('cap')
     + arm(24.5, 24, 27, 28, 'leather', hand=False) + sword(27, 27.5, 25, 13) + hand_(27, 28.4))

icon(C, 'man_at_arms', ['infantry', 'swordsman', 'man at arms', 'melee', 'shield'],
     torso(cloth='steel') + f'<path d="M14 22 L22 22 L22.6 36 L13.4 36 Z" fill="url(#team)" {S(1)}/>'
     + head() + helm('nasal')
     + arm(25, 24, 28.5, 21, 'steel', hand=False) + sword(28.5, 20.5, 15, 13) + hand_(28.5, 21.4)
     + round_shield(9.5, 28, 6.4))

icon(C, 'long_swordsman', ['infantry', 'swordsman', 'long sword', 'melee', 'knight'],
     torso() + f'<path d="M18 22 V36 M12 28 H24" stroke="#e6c070" stroke-width="1.4"/>' + pauldrons()
     + head() + helm('great')
     + sword(24.5, 29, 18, 17) + hand_(24.6, 30.6) + hand_(24.2, 32.6))

icon(C, 'champion', ['infantry', 'swordsman', 'champion', 'elite', 'melee', 'two handed'],
     torso(cloth='red') + f'<path d="M18 22 V36 M11.5 28.5 H24.5" stroke="url(#gold)" stroke-width="2.2"/>' + pauldrons('gold')
     + f'<path d="M18 8 C17 4 19 1 23 1.5 C21 3 20.5 5 21 8 Z" fill="url(#team)" {S(.9)}/>'
     + head() + helm('great') + f'<rect x="16.6" y="6.4" width="2.8" height="3" fill="url(#gold)" {S(.7)}/>'
     + sword(11.5, 30, -22, 19, 1.05) + hand_(11.6, 31.6) + hand_(12.2, 33.4))

icon(C, 'spearman', ['infantry', 'spearman', 'spear', 'anti cavalry', 'melee'],
     spear(27.5, 37, 0, 29) + torso(cloth='leather') + f'<path d="M10 25 C14 23 22 23 26 25 L25 27.6 C21 26 15 26 11 27.6 Z" fill="url(#team)" {S(.9)}/>'
     + head(beard='#6a4020') + hair()
     + arm(24.5, 25, 27.5, 27, 'leather'))

icon(C, 'pikeman', ['infantry', 'pikeman', 'pike', 'anti cavalry', 'melee'],
     torso() + pauldrons() + head() + helm('kettle')
     + spear(13, 37, 24, 36, head='pike') + arm(24, 24.5, 19.4, 26.4, 'team'))

icon(C, 'archer', ['archer', 'ranged', 'bow', 'foot archer'],
     T(26, 22, 20, 1, f'<rect x="-2.4" y="-8" width="4.8" height="10" rx="1" fill="url(#leather)" {S(1)}/>'
       + ''.join(f'<line x1="{dx}" y1="-8" x2="{dx}" y2="-12" stroke="{K}" stroke-width="1.8"/><path d="M{dx - 1} -12 L{dx} -14 L{dx + 1} -12 Z" fill="#e8e0d0" {S(.5)}/>' for dx in (-1.2, 1.2)))
     + torso() + belt(18, 30.5) + head() + helm('cap')
     + bow(8.5, 23, 180, 1.05) + arm(12, 24.5, 6, 23, 'team'))

icon(C, 'longbowman', ['archer', 'ranged', 'longbow', 'bow', 'range'],
     torso(cloth='green') + belt(18, 30.5) + head(beard='#a86a30') + helm('hood')
     + bow(27.5, 19, 0, 1.15, long=True) + arm(23, 25, 29.6, 20, 'green'))

icon(C, 'crossbowman', ['archer', 'ranged', 'crossbow', 'arbalest'],
     torso() + pauldrons() + head() + helm('kettle')
     + T(17, 27, -6, 1,
         f'<rect x="-11" y="-1.3" width="19" height="2.6" rx=".8" fill="url(#oak)" {S(1)}/>'
         f'<path d="M-9 -1 L-3 -1 L-3 1 L-9 1" fill="none" stroke="#e8d0a0" stroke-width=".6"/>'
         f'<path d="M-9 -8 L-4 0 L-9 8" fill="none" stroke="#f0e6c8" stroke-width=".6"/>'
         f'<path d="M-9 -8 C-12 -4 -12 4 -9 8" fill="none" stroke="{K}" stroke-width="3"/>'
         f'<path d="M-9 -8 C-12 -4 -12 4 -9 8" fill="none" stroke="url(#steel)" stroke-width="1.5"/>'
         f'<path d="M-14 0 L-11.4 -1.2 L-11.4 1.2 Z" fill="url(#steel)" {S(.6)}/>')
     + hand_(12, 27.6) + hand_(23, 26.4))

icon(C, 'skirmisher', ['archer', 'skirmisher', 'javelin', 'anti archer', 'ranged'],
     torso(cloth='cream') + belt(18, 30.5) + head() + helm('cap')
     + spear(27.5, 34, -12, 25) + arm(24.5, 24.5, 26.8, 27, 'cream')
     + round_shield(9.5, 28.5, 5.6, 'leather'))

icon(C, 'hand_cannoneer', ['gunpowder', 'hand cannoneer', 'gun', 'ranged', 'musket'],
     torso(cloth='red') + f'<path d="M11.5 26 L24.5 26" stroke="#f0d080" stroke-width="1.2"/>' + head(beard='#4a2a12')
     + f'<path d="M9 11.8 C13 10 23 10 27 11.8 C23 13 13 13 9 11.8 Z" fill="url(#black)" {S(1)}/>'
       f'<path d="M13.6 11 C13.6 6 22.4 6 22.4 11 Z" fill="url(#black)" {S(1)}/>'
       f'<path d="M21 9 C24 4 28 3 30 4 C28 6 25 8 22 10 Z" fill="#f4f0e8" {S(.7)}/>'
     + f'<circle cx="4.5" cy="25" r="2.8" fill="url(#smoke)" opacity=".9"/><circle cx="6" cy="22" r="2" fill="url(#smoke)" opacity=".8"/>'
     + T(18, 25.5, -6, 1, f'<rect x="-10" y="-1.1" width="17" height="2.2" fill="url(#iron)" {S(.9)}/>'
         f'<path d="M5 -1.4 L13 -1.4 L14 2.8 L6 2 Z" fill="url(#oak)" {S(.9)}/>'
         f'<rect x="-10.6" y="-1.6" width="1.4" height="3.2" fill="url(#iron)" {S(.6)}/>')
     + hand_(14, 26.4) + hand_(24.4, 25.6))

# ---------------- cavalry ----------------
H = lambda **k: horse_head(-1.5, 3.2, .92, **k)

icon(C, 'scout', ['cavalry', 'scout', 'light cavalry', 'horse', 'explore'],
     rider(25.4, 8.5, .78, torso(cloth='leather') + head() + helm('cap'))
     + H(coat='camel', mane='#7a5a2e', barding=None))

icon(C, 'knight', ['cavalry', 'knight', 'heavy cavalry', 'horse', 'cavalier', 'paladin'],
     spear(31, 37, 0, 30)
     + f'<path d="M31.8 5.5 L36 7 L31.8 9.5 Z" fill="url(#team)" {S(.7)}/>'
     + rider(25.4, 8.5, .78, torso() + pauldrons() + head() + helm('great'))
     + H(coat='grey', chanfron=True, barding='team'))

icon(C, 'cavalry_archer', ['cavalry', 'archer', 'horse archer', 'ranged', 'mounted'],
     rider(25.4, 8.5, .78, torso(cloth='green') + head() + helm('cap'))
     + bow(29.6, 14, 0, .8) + arm(27.4, 16.4, 31.6, 14, 'green')
     + H(coat='horse', barding='green'))

icon(C, 'camel_rider', ['cavalry', 'camel', 'anti cavalry', 'mounted', 'desert'],
     rider(25.6, 7, .74, torso(cloth='cream') + head() + helm('turban'))
     + T(-.5, 2, 0, .95,
         f'<path d="M29 36 C29 28 27 22 25 16 C24.4 13 23.4 10.4 21.6 9.4 L21 7 L19.4 8.8 C16 8.6 11.6 9.4 8.6 11.2 '
         f'C6.6 12.4 6.2 14.6 7.6 16.2 C8 17.2 8.6 17.8 9.4 17.8 C12.4 17 15.2 16.2 17.6 16.6 C19.4 18 20 24 19.6 36 Z" fill="url(#camel)" {S(1.3)}/>'
         f'<circle cx="16" cy="11.6" r=".9" fill="{K}"/>'
         f'<path d="M7 15.6 C8 15.2 9 15.6 9.4 16.6" fill="none" stroke="{K}" stroke-width=".8"/>'
         f'<path d="M9.6 12.4 L11.4 16.6 M11 13 C14 12 17.6 12 20.4 12.6" fill="none" stroke="#c03424" stroke-width="1.1"/>'
         f'<path d="M19.6 28 C22 26.6 26 26 29.2 26.6 L29.4 36 L19.6 36 Z" fill="url(#red)" {S(1)}/>'
         f'<path d="M19.8 30.6 L29.4 29.8" stroke="#f0d080" stroke-width=".9" stroke-dasharray="1.2 1"/>'))

icon(C, 'war_elephant', ['cavalry', 'elephant', 'war elephant', 'heavy', 'mounted'],
     # ears
     f'<path d="M11 11 C4 8 2 14 3 20 C4 26 7 29 11 27 Z" fill="url(#eleph)" {S(1.2)}/>'
     f'<path d="M25 11 C32 8 34 14 33 20 C32 26 29 29 25 27 Z" fill="url(#eleph)" {S(1.2)}/>'
     # head dome + trunk
     + f'<path d="M18 5 C11 5 9 10 9.6 16 C10 21 13 23 14.6 24 C15 28 14 31 12 34 L16 36 C18 33 19.4 29 21.4 24 C23 23 26 21 26.4 16 C27 10 25 5 18 5 Z" fill="url(#eleph)" {S(1.3)}/>'
     f'<path d="M15.6 26 L19.8 26.6 M15 29 L18.8 29.8" stroke="#46423f" stroke-width=".7"/>'
     # tusks
     + f'<path d="M14.2 22.4 C12 25 10 27 8 27 C9.4 25 11 23 12.2 21 Z" fill="url(#bone)" {S(.9)}/>'
       f'<path d="M21.8 22.4 C24 25 26 27 28 27 C26.6 25 25 23 23.8 21 Z" fill="url(#bone)" {S(.9)}/>'
     + f'<circle cx="13.6" cy="15" r=".9" fill="{K}"/><circle cx="22.4" cy="15" r=".9" fill="{K}"/>'
     # head cloth
     + f'<path d="M12 6.8 C15 5.2 21 5.2 24 6.8 L22 12 L18 14.4 L14 12 Z" fill="url(#red)" {S(1)}/>'
       f'<circle cx="18" cy="9.6" r="1.6" fill="url(#gold)" {S(.7)}/>'
       f'<path d="M14 12 L18 14.4 L22 12" fill="none" stroke="#f0d080" stroke-width=".8"/>')

# ---------------- civilians / religious ----------------
icon(C, 'monk', ['monk', 'priest', 'religious', 'healer', 'convert', 'faith'],
     '<circle cx="18" cy="13" r="10" fill="url(#glow)" opacity=".7"/>'
     + f'<rect x="26.6" y="5" width="2" height="31" rx=".8" fill="url(#oak)" {S(.9)}/>'
       f'<path d="M26.4 6.5 H28.8 M27.6 3.6 V9" stroke="{K}" stroke-width="2.6"/><path d="M25.7 6.5 H29.5 M27.6 3.4 V9.2" stroke="#e6ab2e" stroke-width="1.2"/>'
     + torso(cloth='robe') + f'<path d="M13 22.6 C14 26 22 26 23 22.6" fill="none" stroke="#6e3410" stroke-width="1.4"/>'
       f'<path d="M13 31 C16 30 20 30 23 31" stroke="#e8d8a8" stroke-width="1.4" fill="none"/>'
     + head() + helm('tonsure') + arm(24, 25, 27.6, 22, 'robe'))

icon(C, 'villager', ['villager', 'worker', 'peasant', 'gatherer', 'builder', 'economy'],
     T(9, 34, -12, 1, f'<rect x="-.8" y="-26" width="1.6" height="26" fill="url(#oak)" {S(.8)}/>'
       f'<path d="M-3.4 -24 V-30 M0 -24 V-31 M3.4 -24 V-30 M-3.6 -24 H3.6" fill="none" stroke="{K}" stroke-width="2.2"/>'
       f'<path d="M-3.4 -24 V-30 M0 -24 V-31 M3.4 -24 V-30 M-3.6 -24 H3.6" fill="none" stroke="#c8ccd4" stroke-width=".9"/>')
     + torso(cloth='cream') + f'<path d="M13 26 L23 26 L24 36 L12 36 Z" fill="url(#leather)" {S(.9)}/>'
     + head(beard='#7a4a20') + helm('straw') + arm(12, 25, 9.6, 27, 'cream'))

icon(C, 'villager_woman', ['villager', 'worker', 'woman', 'peasant', 'gatherer', 'economy'],
     f'<path d="M12.6 14 C12 19 11.6 22 10.6 24 L25.4 24 C24.4 22 24 19 23.4 14 Z" fill="#7a4a20" {S(.9)}/>'
     + torso(cloth='red') + f'<path d="M14 23 C16 25 20 25 22 23 L23 36 L13 36 Z" fill="url(#cream)" {S(.9)}/>'
     + head() + helm('kerchief')
     + f'<path d="M20 28 C20 34 31 34 31 28 Z" fill="url(#thatch)" {S(1.1)}/>'
       f'<path d="M21 30 H30 M22 32 H29" stroke="#7a561c" stroke-width=".6"/>'
     + ''.join(f'<circle cx="{x}" cy="{y}" r="1.4" fill="url(#ruby)" {S(.6)}/>' for x, y in [(22.6, 27.4), (25.4, 26.8), (28.2, 27.4), (24, 25.4)])
     + arm(23.5, 24.6, 21, 29, 'red'))

icon(C, 'trade_cart', ['trade', 'cart', 'merchant', 'caravan', 'gold', 'economy'],
     SHADOW + f'<path d="M2 23 L10 24" stroke="{K}" stroke-width="2.6"/><path d="M2 23 L10 24" stroke="#a86a30" stroke-width="1.2"/>'
     + f'<path d="M14 12 C14 8 17 7 20 7 C23 7 25 8 25 12 Z" fill="url(#cream)" {S(1)}/>'
       f'<rect x="22" y="9.6" width="8" height="7" fill="url(#oak)" {S(1)}/><path d="M22 13 H30" stroke="#5a3410" stroke-width=".7"/>'
       f'<path d="M12 14 C11 11 13 9 15.4 9.6 C17 10 17.6 12 17 14 Z" fill="url(#cream)" {S(.9)}/>'
     + ''.join(f'<ellipse cx="{x}" cy="{y}" rx="2.4" ry="1.1" fill="url(#gold)" {S(.6)}/>' for x, y in [(19, 13.4), (21, 12.2), (17.4, 12.4)])
     + f'<path d="M9 14 L32 14 L30 24 L11 24 Z" fill="url(#oak)" {S(1.2)}/>'
       f'<path d="M10 17.4 H31.2 M10.6 20.6 H30.6" stroke="#5a3410" stroke-width=".7"/>'
     + wheel(16, 26, 5.4) + wheel(27.5, 27, 4))

# ---------------- siege ----------------
icon(C, 'battering_ram', ['siege', 'ram', 'battering ram', 'anti building'],
     SHADOW + f'<rect x="1.5" y="19.6" width="16" height="4.4" rx="1.4" fill="url(#oak)" {S(1)}/>'
       f'<path d="M1 18.4 L6 18.4 L6.6 25 L1 25 Z" fill="url(#iron)" {S(1)}/>'
     + f'<path d="M6 27 L18 8 L32 27 Z" fill="url(#leather)" {S(1.3)}/>'
       f'<path d="M10 21 L18 9 M14 24.4 L19.4 11 M22 25 L20 11 M27 25 L21 10" stroke="#5a3410" stroke-width=".7"/>'
       f'<path d="M17 8 L19 8 L33 27.4 L30.6 27.4 Z" fill="url(#dwood)" {S(.9)}/>'
     + f'<rect x="5" y="25.6" width="28" height="3" fill="url(#dwood)" {S(1)}/>'
     + wheel(10.5, 29.5, 3.6) + wheel(27, 29.5, 3.6))

icon(C, 'mangonel', ['siege', 'mangonel', 'onager', 'catapult', 'area damage'],
     SHADOW + f'<path d="M13 26 L18 12 L23 26" fill="none" stroke="{K}" stroke-width="3.2"/>'
       f'<path d="M13 26 L18 12 L23 26" fill="none" stroke="#8a5520" stroke-width="1.6"/>'
     + T(22, 24, -55, 1, f'<rect x="-1.4" y="-19" width="2.8" height="21" rx="1" fill="url(#dwood)" {S(1)}/>'
         f'<path d="M-4.4 -19 C-4.4 -24 4.4 -24 4.4 -19 Z" fill="url(#leather)" {S(1)}/>'
         f'<circle cx="0" cy="-21.4" r="2.8" fill="url(#stone)" {S(.9)}/>')
     + f'<rect x="4" y="25" width="28" height="3.4" fill="url(#oak)" {S(1.1)}/>'
     + f'<rect x="16.6" y="11" width="2.8" height="3" fill="url(#iron)" {S(.7)}/>'
     + wheel(9, 29.6, 3.6) + wheel(27, 29.6, 3.6))

icon(C, 'scorpion', ['siege', 'scorpion', 'ballista', 'bolt', 'ranged'],
     SHADOW + f'<path d="M18 19 L9 33 M18 19 L27 33 M18 19 L18 33" fill="none" stroke="{K}" stroke-width="3"/>'
       f'<path d="M18 19 L9 33 M18 19 L27 33 M18 19 L18 33" fill="none" stroke="#8a5520" stroke-width="1.5"/>'
     + f'<path d="M10 7 L22 18 L10 29" fill="none" stroke="#f0e6c8" stroke-width=".7"/>'
       f'<path d="M10 7 C5 11 5 25 10 29" fill="none" stroke="{K}" stroke-width="3.4"/>'
       f'<path d="M10 7 C5 11 5 25 10 29" fill="none" stroke="url(#oak)" stroke-width="1.8"/>'
       f'<rect x="6" y="16.6" width="26" height="3" rx=".8" fill="url(#oak)" {S(1)}/>'
       f'<rect x="5" y="13" width="3.8" height="10" rx="1" fill="url(#iron)" {S(.9)}/>'
       f'<path d="M3 15 L22 15" stroke="{K}" stroke-width="2"/><path d="M3 15 L22 15" stroke="#e8d0a0" stroke-width=".9"/>'
       f'<path d="M1 15 L4.4 13.4 L4.4 16.6 Z" fill="url(#steel)" {S(.6)}/>')

icon(C, 'trebuchet', ['siege', 'trebuchet', 'long range', 'anti building'],
     SHADOW + f'<path d="M9 30 L18 11 L27 30" fill="none" stroke="{K}" stroke-width="3.6"/>'
       f'<path d="M9 30 L18 11 L27 30" fill="none" stroke="#8a5520" stroke-width="2"/>'
     + T(18, 11.5, -62, 1, f'<rect x="-1.2" y="-15" width="2.4" height="24" rx=".8" fill="url(#dwood)" {S(1)}/>')
     + f'<path d="M4.8 4.6 L5.8 13.4" stroke="#e8d8a8" stroke-width=".7"/><circle cx="5.8" cy="14.4" r="1.9" fill="url(#stone)" {S(.8)}/>'
     + f'<rect x="23.6" y="15" width="7" height="7" rx=".8" fill="url(#iron)" {S(1)}/>'
     + f'<rect x="3" y="29" width="30" height="3" fill="url(#oak)" {S(1.1)}/>'
       f'<circle cx="18" cy="11.5" r="1.4" fill="url(#iron)" {S(.7)}/>')

icon(C, 'bombard_cannon', ['siege', 'gunpowder', 'cannon', 'bombard', 'anti building'],
     SHADOW + f'<circle cx="4.5" cy="13" r="3.4" fill="url(#smoke)" opacity=".9"/><circle cx="7.6" cy="9.4" r="2.4" fill="url(#smoke)" opacity=".8"/>'
     + f'<path d="M10 28 L31 25 L31 29 L12 31 Z" fill="url(#oak)" {S(1.1)}/>'
     + T(18, 19, -14, 1, f'<path d="M-12 -3.4 L12 -4.6 L12 4.6 L-12 3.4 Z" fill="url(#iron)" {S(1.2)}/>'
         + ''.join(f'<rect x="{x}" y="-5.2" width="2" height="10.4" rx=".6" fill="url(#iron)" {S(.8)}/>' for x in (-12.6, -4, 4.6))
         + f'<ellipse cx="-12.6" cy="0" rx="1.4" ry="3" fill="#120e0c"/>'
           f'<circle cx="13.6" cy="0" r="2.6" fill="url(#iron)" {S(.8)}/>'
           f'<path d="M-9 -2.2 L10 -3.2" stroke="#c4c8d0" stroke-width=".9"/>')
     + wheel(16, 28, 5))

# ---------------- ships ----------------
def hull(x1=4, x2=32, y=22, h=7, fill='oak', inner=''):
    return (f'<path d="M{x1} {y} L{x2} {y} L{x2 - 4} {y + h} L{x1 + 4} {y + h} Z" fill="url(#{fill})" {S(1.2)}/>'
            f'<path d="M{x1 + 1} {y + 2.4} L{x2 - 1} {y + 2.4}" stroke="#f0d080" stroke-width=".9"/>' + inner)


icon(C, 'galley', ['ship', 'navy', 'galley', 'warship', 'water'],
     f'<rect x="17.1" y="3" width="1.8" height="20" fill="url(#dwood)" {S(.8)}/>'
     f'<path d="M8.6 6 L27.4 6 C28.6 11 28.6 15 27.4 20 L8.6 20 C7.4 15 7.4 11 8.6 6 Z" fill="url(#team)" {S(1.2)}/>'
     f'<path d="M8.2 13 C14 12 22 12 27.8 13" fill="none" stroke="#f4f0e8" stroke-width="1.6"/>'
     f'<path d="M10 6 V20 M26 6 V20" stroke="#1c3478" stroke-width=".5"/>'
     f'<path d="M3.5 20.4 L1.6 17.6" stroke="{K}" stroke-width="1.6"/>'
     + ''.join(f'<path d="M{x} 25 L{x - 3} 31" stroke="{K}" stroke-width="1.8"/><path d="M{x} 25 L{x - 3} 31" stroke="#d39a52" stroke-width=".8"/>' for x in (11, 16, 21, 26))
     + hull() + water(28))

icon(C, 'fire_ship', ['ship', 'navy', 'fire ship', 'fire', 'water'],
     f'<circle cx="16" cy="14" r="13" fill="url(#glow)" opacity=".7"/>'
     f'<rect x="17.1" y="5" width="1.8" height="18" fill="url(#dwood)" {S(.8)}/>'
     f'<path d="M9 7 L27 7 L25 12 L27 19 L19 17 L9 19 L11 13 Z" fill="url(#red)" {S(1.1)}/>'
     + flame(7.5, 23, .9) + flame(14, 23, .62) + flame(24, 23, .72)
     + hull(fill='dwood') + water(28))

icon(C, 'transport_ship', ['ship', 'navy', 'transport', 'carry', 'water'],
     f'<rect x="21.1" y="4" width="1.8" height="16" fill="url(#dwood)" {S(.8)}/>'
     f'<path d="M15 6 L28 6 L29 16 L14 16 Z" fill="url(#cream)" {S(1.1)}/>'
     f'<rect x="6" y="15" width="7" height="7" fill="url(#oak)" {S(1)}/><path d="M6 15 L13 22 M13 15 L6 22" stroke="#5a3410" stroke-width=".7"/>'
     f'<rect x="10.4" y="11.6" width="5.4" height="5.4" fill="url(#oak)" {S(1)}/>'
     f'<path d="M24 22 C23 18 25 16.4 27.6 16.6 C30 17 30.6 19.6 30 22 Z" fill="url(#cream)" {S(.9)}/>'
     + hull(2, 34, 21, 8) + water(28))

icon(C, 'fishing_ship', ['ship', 'fishing', 'boat', 'food', 'economy', 'water'],
     f'<rect x="12.1" y="5" width="1.6" height="18" fill="url(#dwood)" {S(.8)}/>'
     f'<path d="M13.7 6 L24 18 L13.7 18 Z" fill="url(#cream)" {S(1)}/>'
     f'<path d="M5 9 L11 9 L9 22 L4 22 Z" fill="none" stroke="#e8d8a8" stroke-width=".6"/>'
     f'<path d="M5 12 L10.6 12 M4.6 15 L10.2 15 M4.4 18 L9.8 18 M7 9 L6 22" stroke="#e8d8a8" stroke-width=".5"/>'
     + hull(3, 27, 22, 6)
     + f'<path d="M26 18 C28 15 31 15 33 17 L35 15 L35 20 L33 18.6 C31 21 28 21 26 18 Z" fill="url(#steel)" {S(.9)}/>'
       f'<circle cx="28" cy="17.4" r=".5" fill="{K}"/>'
     + water(28))

# ---------------- special / heroes ----------------
icon(C, 'berserker', ['infantry', 'berserker', 'viking', 'axe', 'rage', 'melee'],
     torso(cloth='skin') + f'<path d="M14 26 C16 27 18 27 18 26 M18 26 C18 27 20 27 22 26" fill="none" stroke="#a8693e" stroke-width=".8"/>'
     + f'<path d="M7 27 L9 22 L12 23.6 L14 21 L17 23 L19 21 L22 23 L24 21 L27 24 L29 27 C26 25 22 24.6 18 24.6 C14 24.6 10 25 7 27 Z" fill="url(#wolf)" {S(1)}/>'
     + belt(18, 31) + head(beard='#c8642a') + helm('horned')
     + axe(27, 31, 12, 23, 1, double=True) + hand_(27.2, 28.6))

icon(C, 'throwing_axeman', ['infantry', 'axe', 'throwing', 'franks', 'ranged', 'melee'],
     torso(cloth='green') + belt(18, 30.5) + head(beard='#d8a040') + hair('#d8a040')
     + axe(27, 30, 20, 11) + hand_(27, 30)
     + T(8.5, 9, -60, .85, f'<rect x="-.9" y="-6" width="1.8" height="10" rx=".6" fill="url(#oak)" {S(.8)}/>'
         f'<path d="M.5 -5 L4 -8.5 C7 -5 7 -1 4 2 L.5 -1 Z" fill="url(#steel)" {S(1)}/>')
     + f'<path d="M3 15 C3.6 18 6 19.6 9 19.6 M14 5 C15.4 6 16 7.6 15.8 9" fill="none" stroke="#f4e8c0" stroke-width="1" stroke-opacity=".8"/>')

icon(C, 'samurai', ['infantry', 'samurai', 'katana', 'japanese', 'unique unit', 'melee'],
     torso(cloth='red') + ''.join(f'<path d="M{9 - k * .3} {25 + k * 3} H{27 + k * .3}" stroke="#e6ab2e" stroke-width=".9"/>' for k in range(4))
     + pauldrons('iron') + head() + helm('kabuto')
     + f'<path d="M24.4 28 C27 20 28.4 12 28.6 3.5 C30 11 29 20 26.4 28.6 Z" fill="url(#steel)" {S(.9)}/>'
       f'<ellipse cx="25.4" cy="28.6" rx="2.4" ry="1" fill="url(#gold)" {S(.7)}/>'
       f'<rect x="24.2" y="29.4" width="2" height="5" fill="url(#black)" {S(.7)}/>'
     + hand_(25.2, 31) + hand_(25.4, 33.4))

icon(C, 'hero', ['hero', 'champion', 'leader', 'legendary', 'unique', 'elite'],
     '<circle cx="18" cy="13" r="12" fill="url(#glow)"/>'
     + f'<path d="M7 36 C5 30 6 24 11 21.4 L25 21.4 C30 24 31 30 29 36 Z" fill="url(#red)" {S(1.2)}/>'
     + torso(w=9) + f'<path d="M12 27 H24 M18 22 V36" stroke="url(#gold)" stroke-width="1.6"/>' + pauldrons('gold')
     + head() + helm('plumed')
     + arm(26, 24, 29, 20, 'team', hand=False) + sword(29, 19.5, 8, 14) + hand_(29, 20.4) + sparkle(31, 4, .9))

icon(C, 'king', ['king', 'regicide', 'royal', 'leader', 'vip'],
     torso(cloth='red') + f'<path d="M7.6 26 C10 22 14 21.4 18 21.4 C22 21.4 26 22 28.4 26 C26 27.4 22 25 18 25 C14 25 10 27.4 7.6 26 Z" fill="#f8f4ea" {S(1)}/>'
     + ''.join(f'<path d="M{x} {y} v1.2" stroke="{K}" stroke-width=".9"/>' for x, y in [(11, 24), (14.4, 23), (18, 22.6), (21.6, 23), (25, 24)])
     + head(beard='#d8d4cc') + helm('crown')
     + f'<rect x="27.4" y="12" width="1.8" height="22" rx=".6" fill="url(#gold)" {S(.8)}/><circle cx="28.3" cy="11" r="2.2" fill="url(#gold)" {S(.8)}/>'
       f'<circle cx="28.3" cy="11" r=".9" fill="url(#crystal)"/>' + hand_(28.2, 27))

icon(C, 'queen', ['queen', 'royal', 'leader', 'vip', 'noble'],
     f'<path d="M12.6 12 C11 18 11 24 9.4 28 L26.6 28 C25 24 25 18 23.4 12 Z" fill="#7a3a16" {S(.9)}/>'
     + torso(cloth='purple') + f'<path d="M13 21.8 C15 25 21 25 23 21.8" fill="none" stroke="url(#gold)" stroke-width="1.6"/>'
       f'<circle cx="18" cy="25.4" r="1.2" fill="url(#ruby)" {S(.6)}/>'
     + head(r=4.3) + f'<path d="M13.6 12.6 C14 8 22 8 22.4 12.6 C20 10.6 16 10.6 13.6 12.6 Z" fill="#7a3a16"/>'
     + T(18, 12.2, 0, .72, helm('crown', 0, 0)))

icon(C, 'mercenary', ['mercenary', 'hired', 'sellsword', 'gold', 'infantry', 'melee'],
     torso(cloth='leather') + f'<path d="M10 22.4 L26 32" stroke="{K}" stroke-width="2.4"/><path d="M10 22.4 L26 32" stroke="#c48a52" stroke-width="1.1"/>'
     + head(eyes=False, beard='#3a2410') + helm('bandana')
     + f'<path d="M13.4 12 L22.6 14.6" stroke="{K}" stroke-width=".8"/><ellipse cx="16" cy="13.4" rx="1.6" ry="1.3" fill="{K}"/><circle cx="19.8" cy="13.6" r=".6" fill="{K}"/>'
     + T(27, 28, 20, 1, f'<path d="M-1 0 L-1.6 -11 C1 -12 3 -10 2.4 -7 L1 0 Z" fill="url(#steel)" {S(.9)}/>'
         f'<rect x="-3" y="-.4" width="6" height="1.6" rx=".6" fill="url(#iron)" {S(.7)}/><rect x="-.7" y="1.2" width="1.4" height="3" fill="url(#leather)" {S(.6)}/>')
     + hand_(27.4, 29.6)
     + f'<circle cx="7.5" cy="28" r="3.4" fill="url(#gold)" {S(.9)}/><circle cx="7.5" cy="28" r="2" fill="none" stroke="#8c5a0c" stroke-width=".6"/>' + sparkle(6, 22, .7))

icon(C, 'petard', ['petard', 'bomb', 'explosive', 'suicide', 'anti building', 'gunpowder'],
     torso(cloth='leather') + head() + helm('cap')
     + arm(10.5, 25, 13.5, 28.5, 'leather', hand=False) + arm(25.5, 25, 22.5, 28.5, 'leather', hand=False)
     + f'<circle cx="18" cy="28.5" r="6" fill="url(#black)" {S(1.2)}/><circle cx="16" cy="26.4" r="1.6" fill="#ffffff" fill-opacity=".35"/>'
       f'<rect x="16.6" y="21.4" width="2.8" height="2" fill="url(#iron)" {S(.7)}/>'
       f'<path d="M18 21.4 C18 19 21 18.6 22 17" fill="none" stroke="#c8b080" stroke-width="1"/>'
     + sparkle(22.6, 16.4, 1.1, '#ffd060') + hand_(13.2, 28.4) + hand_(22.8, 28.4))

icon(C, 'slinger', ['slinger', 'sling', 'ranged', 'anti infantry', 'stone'],
     torso(cloth='cream') + belt(18, 30.5) + head() + hair('#3a2410')
     + arm(24.5, 24.5, 27, 19, 'cream')
     + f'<path d="M27 18 C28 12 31 8 32 5" fill="none" stroke="#c48a52" stroke-width=".9"/>'
       f'<circle cx="32" cy="4.6" r="2" fill="url(#stone)" {S(.8)}/>'
       f'<path d="M22 4 C25 1.6 30 1 34 2.6 M23.6 8 C25 6 26.6 5 28 4.6" fill="none" stroke="#f4e8c0" stroke-width=".9" stroke-opacity=".8"/>')

icon(C, 'mage', ['mage', 'wizard', 'magic', 'sorcerer', 'fantasy', 'spell'],
     f'<circle cx="7" cy="7" r="7" fill="url(#bglow)"/>'
     + f'<rect x="6" y="8" width="2" height="28" rx=".8" fill="url(#dwood)" {S(.9)}/>'
       f'<circle cx="7" cy="7" r="2.8" fill="url(#crystal)" {S(.9)}/><circle cx="6.2" cy="6.2" r=".8" fill="#fff"/>'
     + torso(cloth='purple') + f'<path d="M12 24 L18 22 L24 24 M12.4 34 H23.6" fill="none" stroke="url(#gold)" stroke-width="1.2"/>'
     + head(beard='#e8e4dc') + f'<path d="M15 19 C16 25 20 25 21 19 Z" fill="#e8e4dc" {S(.8)}/>' + helm('wizard')
     + arm(12, 25, 8, 22, 'purple'))

icon(C, 'skeleton_warrior', ['undead', 'skeleton', 'fantasy', 'monster', 'melee', 'curse'],
     torso(cloth='black') + ''.join(f'<path d="M{18 - w} {y} C{18 - w / 2} {y - 1.4} {18 + w / 2} {y - 1.4} {18 + w} {y}" fill="none" stroke="#dcd2b4" stroke-width="1.1"/>' for w, y in [(5, 26), (5.6, 28.6), (5.8, 31.2)])
     + f'<path d="M18 23 V34" stroke="#dcd2b4" stroke-width="1.4"/>'
     + f'<path d="M18 7.6 C13 7.6 12.6 11 12.8 14 C13 16 14 17 15 17.4 L15 20 L21 20 L21 17.4 C22 17 23 16 23.2 14 C23.4 11 23 7.6 18 7.6 Z" fill="url(#bone)" {S(1.1)}/>'
       f'<ellipse cx="15.8" cy="13.4" rx="1.6" ry="1.9" fill="#1a0a04"/><ellipse cx="20.2" cy="13.4" rx="1.6" ry="1.9" fill="#1a0a04"/>'
       f'<circle cx="15.8" cy="13.6" r=".6" fill="#ff4020"/><circle cx="20.2" cy="13.6" r=".6" fill="#ff4020"/>'
       f'<path d="M17 18.2 V20 M19 18.2 V20" stroke="{K}" stroke-width=".7"/>'
     + T(27, 29, 22, 1, f'<path d="M-1.3 0 L-1.3 -12 L.2 -14 L1.3 -11 L.6 -8 L1.3 -6 L1.3 0 Z" fill="url(#leather)" {S(.9)}/>'
         f'<rect x="-3.6" y="-.4" width="7.2" height="1.8" fill="url(#iron)" {S(.7)}/>')
     + f'<circle cx="27.2" cy="30" r="1.6" fill="url(#bone)" {S(.8)}/>')

icon(C, 'wolf', ['animal', 'wolf', 'gaia', 'wild', 'hunt', 'monster'],
     f'<path d="M31 36 C31 26 29 18 26 13 L25 5 L21.4 10.4 L19.4 5.4 L16.6 11.4 C13 13 9 16 5 18.6 C3.4 19.6 3.6 21.4 5.2 21.6 L7 21.8 '
     f'C8 23.6 10 23.8 13 23 C15 24 16 27 16.4 30 L16 36 Z" fill="url(#wolf)" {S(1.3)}/>'
     f'<path d="M16.4 30 L18.6 27 L19.4 31 L21.6 27.4 L22.4 32 L24.6 28 L25.4 34 L28 30" fill="none" stroke="#46423c" stroke-width=".9"/>'
     f'<path d="M20.6 8.4 L21.4 11 M24.4 7.6 L25 11" stroke="#46423c" stroke-width="1"/>'
     f'<path d="M15.6 14.4 L18.2 14 L17.4 15.6 Z" fill="#ffd040" {S(.6)}/>'
     f'<circle cx="4.6" cy="19.6" r="1.2" fill="{K}"/>'
     f'<path d="M7 21.8 L12 21.6 M8.6 21.8 L9 23 M10.6 21.7 L10.8 22.8" stroke="{K}" stroke-width=".8"/>')
