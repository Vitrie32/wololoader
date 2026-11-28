from lib import *

C = 'general'

# ---- resources ----
icon(C, 'gold', ['resource', 'gold', 'money', 'treasure'],
     '<ellipse cx="18" cy="29" rx="13" ry="2.6" fill="#000" fill-opacity=".35"/>'
     f'<path d="M5 29 C6 22 11 17 18 16 C25 17 30 22 31 29 Z" fill="url(#gold)" {S(1.3)}/>'
     + ''.join(f'<ellipse cx="{x}" cy="{y}" rx="{r}" ry="{r * .8}" fill="url(#gold)" {S(.9)}/>'
               f'<ellipse cx="{x - r * .35}" cy="{y - r * .3}" rx="{r * .35}" ry="{r * .22}" fill="#fffbe0"/>'
               for x, y, r in [(10, 25, 3), (16, 22, 3.2), (23, 23, 3), (27, 26.5, 2.4), (13, 19.5, 2.4), (19.5, 18, 2.6), (8, 28, 2)])
     + sparkle(25, 13, 1.1) + sparkle(9, 15, .8))

def _log_body(x, y, r):
    return (f'<path d="M{x} {y - r} L{x + 6} {y - r - 5} L{x + 6} {y + r - 5} L{x} {y + r} Z" fill="url(#oak)" {S(1.1)}/>'
            f'<path d="M{x + 1} {y - r + 2.4} L{x + 6} {y - r - 1.8} M{x + 1} {y + 1} L{x + 6} {y - 3.2}" stroke="#5a3410" stroke-width=".6"/>')


def _log_end(x, y, r):
    return (f'<circle cx="{x}" cy="{y}" r="{r}" fill="url(#leather)" {S(1.2)}/>'
            f'<circle cx="{x}" cy="{y}" r="{r - 1.2}" fill="#f0cf94"/>'
            f'<circle cx="{x}" cy="{y}" r="{r - 2.8}" fill="none" stroke="#b07a3a" stroke-width=".6"/>'
            f'<circle cx="{x}" cy="{y}" r="{r - 4.4}" fill="none" stroke="#b07a3a" stroke-width=".6"/>'
            f'<circle cx="{x}" cy="{y}" r=".6" fill="#8a5520"/>')


_LOGS = [(17, 15, 5.6), (10.5, 25, 5.6), (23.5, 25, 5.6)]
icon(C, 'wood', ['resource', 'wood', 'lumber', 'logs'],
     '<ellipse cx="19" cy="30.6" rx="13" ry="2.2" fill="#000" fill-opacity=".35"/>'
     + ''.join(_log_body(*l) for l in _LOGS) + ''.join(_log_end(*l) for l in _LOGS))

icon(C, 'food', ['resource', 'food', 'meat', 'berries'],
     '<ellipse cx="18" cy="29.5" rx="12" ry="2.4" fill="#000" fill-opacity=".35"/>'
     # bone
     f'<path d="M22 13 L27.5 7.5" stroke="{K}" stroke-width="4.2" stroke-linecap="round"/>'
     f'<path d="M22 13 L27.5 7.5" stroke="#f6eed4" stroke-width="2.4" stroke-linecap="round"/>'
     f'<circle cx="28.4" cy="5.6" r="1.8" fill="url(#bone)" {S(.9)}/><circle cx="29.6" cy="7.4" r="1.8" fill="url(#bone)" {S(.9)}/>'
     # haunch
     f'<path d="M24 11.5 C27 14.5 26 22 20 26 C14 30 7 28.5 6 24 C5 19 10 14 16 12 C19 11 22 10.2 24 11.5 Z" fill="url(#red)" {S(1.3)}/>'
     f'<path d="M23 13 C24.5 16 23 21 18.6 24 C14 27 9 26.5 8 23.6" fill="none" stroke="#e8a070" stroke-width="1.4" stroke-opacity=".7"/>'
     f'<ellipse cx="13" cy="17.5" rx="3" ry="1.6" fill="#ffc0a0" fill-opacity=".6" transform="rotate(-25 13 17.5)"/>'
     + ''.join(f'<circle cx="{x}" cy="{y}" r="2.1" fill="url(#ruby)" {S(.8)}/><circle cx="{x - .6}" cy="{y - .7}" r=".5" fill="#fff"/>'
               for x, y in [(24, 28), (27.5, 26.5), (26.5, 30)]))

icon(C, 'stone', ['resource', 'stone', 'rock', 'blocks'],
     '<ellipse cx="18" cy="30.5" rx="14" ry="2.4" fill="#000" fill-opacity=".35"/><g transform="translate(-2.4 -5.6) scale(1.2)">'
     + ''.join(f'<path d="M{x} {y} l6 -3 l6 3 l-6 3 Z" fill="#f4efe4" {S(1)}/>'
               f'<path d="M{x} {y} l6 3 v6 l-6 -3 Z" fill="#b4ac9c" {S(1)}/>'
               f'<path d="M{x + 12} {y} l-6 3 v6 l6 -3 Z" fill="#7a7266" {S(1)}/>'
               for x, y in [(5, 21), (17, 21)])
     + ''.join(f'<path d="M{x} {y} l6 -3 l6 3 l-6 3 Z" fill="#f4efe4" {S(1)}/>'
               f'<path d="M{x} {y} l6 3 v6 l-6 -3 Z" fill="#b4ac9c" {S(1)}/>'
               f'<path d="M{x + 12} {y} l-6 3 v6 l6 -3 Z" fill="#7a7266" {S(1)}/>'
               for x, y in [(11, 15)]) + '</g>')

icon(C, 'faith', ['resource', 'faith', 'holy', 'religion', 'monk'],
     '<circle cx="18" cy="18" r="15" fill="url(#glow)"/>'
     + ''.join(T(18, 18, a, 1, f'<path d="M-1.4 -7 L0 -14.5 L1.4 -7 Z" fill="url(#gold)" {S(.8)}/>') for a in range(0, 360, 45))
     + f'<circle cx="18" cy="18" r="7" fill="url(#gold)" {S(1.2)}/>'
       f'<circle cx="18" cy="18" r="5.2" fill="#fff8d0" {S(.7)}/>'
       f'<path d="M16.8 13.8 h2.4 v3 h3 v2.4 h-3 v4.6 h-2.4 v-4.6 h-3 v-2.4 h3 Z" fill="url(#gold)" {S(.8)}/>')

icon(C, 'medal', ['points', 'score', 'medal', 'award', 'victory'],
     f'<path d="M11 4 L17 4 L20 15 L15 17 Z" fill="url(#red)" {S(1)}/>'
     f'<path d="M25 4 L19 4 L16 15 L21 17 Z" fill="url(#team)" {S(1)}/>'
     f'<circle cx="18" cy="22.5" r="8.5" fill="url(#gold)" {S(1.3)}/>'
     f'<circle cx="18" cy="22.5" r="6.4" fill="none" stroke="#8c5a0c" stroke-width=".8"/>'
     f'<path d="{star_path(18, 22.8, 4.6, 2)}" fill="#fff3a6" {S(.8)}/>')

icon(C, 'crystal', ['resource', 'crystal', 'mana', 'gem', 'magic'],
     '<circle cx="18" cy="17" r="14" fill="url(#bglow)"/>'
     '<ellipse cx="18" cy="30" rx="11" ry="2.2" fill="#000" fill-opacity=".35"/>'
     f'<path d="M8 29 L7 19 L10 15 L13 19 L13 29 Z" fill="url(#crystal)" {S(1)}/>'
     f'<path d="M23 29 L23 17 L27 12 L30 17 L29 29 Z" fill="url(#crystal)" {S(1)}/>'
     f'<path d="M12 30 L12 13 L18 4 L24 13 L24 30 Z" fill="url(#crystal)" {S(1.2)}/>'
     f'<path d="M18 4 L18 30 M12 13 L18 16 L24 13" fill="none" stroke="#1a4e98" stroke-width=".7"/>'
     f'<path d="M13.5 14 L17 15.8 L17 27 L13.5 27 Z" fill="#ffffff" fill-opacity=".45"/>'
     + sparkle(26, 7, .9))

# ---- arrows / stats ----
icon(C, 'upgrade_arrow', ['upgrade', 'level up', 'arrow', 'improve', 'research'],
     '<circle cx="18" cy="18" r="14" fill="url(#ggl)"/>'
     f'<path d="M18 4 L30 17 L23 17 L23 31 L13 31 L13 17 L6 17 Z" fill="url(#upg)" {S(1.4)}/>'
     f'<path d="M18 7 L26.5 15.6 L21.6 15.6 L21.6 29.6" fill="none" stroke="#eaffd0" stroke-width=".9" stroke-opacity=".8"/>')

icon(C, 'downgrade_arrow', ['downgrade', 'decrease', 'arrow', 'debuff', 'down'],
     f'<path d="M18 32 L30 19 L23 19 L23 5 L13 5 L13 19 L6 19 Z" fill="url(#dng)" {S(1.4)}/>'
     f'<path d="M14.4 6.4 L14.4 20.4 L9.5 20.4" fill="none" stroke="#ffe0d0" stroke-width=".9" stroke-opacity=".8"/>')

icon(C, 'attack', ['attack', 'damage', 'strike', 'combat'],
     f'<path d="{star_path(17, 19, 14, 6, n=8, rot=-80)}" fill="url(#dng)" {S(1)}/>'
     f'<path d="{star_path(17, 19, 8, 3.6, n=8, rot=-70)}" fill="#ffe88a"/>'
     + sword(13, 23, 45, 18, 1.05))

icon(C, 'armor', ['armor', 'armour', 'defence', 'defense', 'cuirass', 'protection'],
     f'<path d="M9 6 L14 5 C15 8 21 8 22 5 L27 6 L30 11 L27 13 L26.6 24 C26.6 28 23 31 18 31 C13 31 9.4 28 9.4 24 L9 13 L6 11 Z" fill="url(#steel)" {S(1.3)}/>'
     f'<path d="M14 5 C15 8 21 8 22 5" fill="none" stroke="#e6ab2e" stroke-width="1.4"/>'
     f'<path d="M18 9 L18 30" stroke="#56616e" stroke-width=".9"/>'
     f'<path d="M10.5 21 C14 23 22 23 25.5 21 M10.6 25 C14 27 22 27 25.4 25" fill="none" stroke="#56616e" stroke-width=".8"/>'
     f'<path d="M12 10 C12 14 13 17 15 19" fill="none" stroke="#ffffff" stroke-width="1.2" stroke-opacity=".7"/>'
     f'<circle cx="9" cy="10" r=".8" fill="url(#gold)"/><circle cx="27" cy="10" r=".8" fill="url(#gold)"/>')

icon(C, 'speed', ['speed', 'movement', 'haste', 'boots', 'fast'],
     f'<path d="M3 13 H9 M2 18 H8 M4 23 H9" stroke="#f4e8c0" stroke-width="1.4" stroke-linecap="round" stroke-opacity=".8"/>'
     f'<path d="M13 5 L21 5 L21 19 C24 20 29 21 30 24 L30 28 L11 28 L11 22 Z" fill="url(#leather)" {S(1.3)}/>'
     f'<path d="M11 25.6 L30 25.6" stroke="#4e2c12" stroke-width="1"/>'
     f'<rect x="12.6" y="5" width="8.4" height="2.6" fill="url(#gold)" {S(.8)}/>'
     f'<path d="M21 9 C25 6 29 5 33 6 C30 7.4 30 8 32 9 C29 10 28.6 10.6 30.4 11.8 C27 12.6 24 13 21 13 Z" fill="url(#cream)" {S(1)}/>'
     f'<path d="M22.4 10 C25 9 27 8.4 29.4 8.2" fill="none" stroke="#a88a50" stroke-width=".6"/>')

icon(C, 'heal', ['heal', 'health', 'regenerate', 'restore', 'cure'],
     '<circle cx="18" cy="18" r="15" fill="url(#ggl)"/>'
     f'<path d="M14.5 6 H21.5 V14.5 H30 V21.5 H21.5 V30 H14.5 V21.5 H6 V14.5 H14.5 Z" fill="url(#upg)" {S(1.4)}/>'
     f'<path d="M16 7.4 H20 V16 H28.6" fill="none" stroke="#eaffd0" stroke-width=".9" stroke-opacity=".9"/>'
     + sparkle(8, 8, .9) + sparkle(28, 28, .8) + sparkle(28, 8, .6))

icon(C, 'repair', ['repair', 'build', 'fix', 'construct', 'hammer', 'saw'],
     # saw
     T(18, 18, 40, 1,
       f'<path d="M-3 -13 L3 -13 L3 9 L-3 9 Z" fill="url(#steel)" {S(1)}/>'
       + ''.join(f'<path d="M3 {y} l1.5 1 l-1.5 1" fill="url(#steel)" {S(.5)}/>' for y in range(-12, 8, 2))
       + f'<rect x="-3.8" y="9" width="7.6" height="6" rx="1.5" fill="url(#oak)" {S(1)}/><rect x="-1.6" y="10.6" width="3.2" height="2.6" rx="1" fill="{K}"/>')
     + T(18, 18, -40, 1,
         f'<rect x="-1.2" y="-6" width="2.4" height="20" rx=".8" fill="url(#oak)" {S(1)}/>'
         f'<path d="M-6.5 -11 L5 -11 L6.5 -9.5 L6.5 -6 L-6.5 -6 Z" fill="url(#iron)" {S(1.1)}/>'))

icon(C, 'convert', ['convert', 'conversion', 'change owner', 'wololo', 'swap', 'cycle'],
     '<circle cx="18" cy="18" r="12" fill="url(#glow)"/>'
     f'<path d="M18 5.5 A12.5 12.5 0 0 1 30.4 16.4 L33 16.4 L28.4 22 L23.6 16.4 L26.2 16.4 A8.4 8.4 0 0 0 18 9.6 Z" fill="url(#red)" {S(1.1)}/>'
     f'<path d="M18 30.5 A12.5 12.5 0 0 1 5.6 19.6 L3 19.6 L7.6 14 L12.4 19.6 L9.8 19.6 A8.4 8.4 0 0 0 18 26.4 Z" fill="url(#team)" {S(1.1)}/>'
     f'<circle cx="18" cy="15.6" r="2.3" fill="url(#cream)" {S(.8)}/><path d="M14 23 C14 19 22 19 22 23 Z" fill="url(#cream)" {S(.8)}/>')

icon(C, 'star', ['star', 'favourite', 'elite', 'rank', 'bonus'],
     '<circle cx="18" cy="18" r="14" fill="url(#glow)"/>'
     f'<path d="{star_path(18, 19, 13.5, 5.6)}" fill="url(#gold)" {S(1.4)}/>'
     f'<path d="M18 7 L19.6 15" stroke="#fffbe0" stroke-width="1" stroke-opacity=".9"/>'
     f'<path d="M8 15 L14.6 16" stroke="#fffbe0" stroke-width=".8" stroke-opacity=".8"/>')

icon(C, 'crown', ['crown', 'king', 'royal', 'regicide', 'leader'],
     f'<path d="M5 13 L11 19 L18 7 L25 19 L31 13 L28.5 27 L7.5 27 Z" fill="url(#gold)" {S(1.4)}/>'
     f'<rect x="7" y="25.5" width="22" height="4.6" rx="1" fill="url(#gold)" {S(1.2)}/>'
     f'<circle cx="18" cy="27.8" r="1.5" fill="url(#ruby)" {S(.6)}/><circle cx="11.5" cy="27.8" r="1.2" fill="url(#crystal)" {S(.6)}/><circle cx="24.5" cy="27.8" r="1.2" fill="url(#crystal)" {S(.6)}/>'
     f'<circle cx="5" cy="12.4" r="1.8" fill="url(#gold)" {S(.8)}/><circle cx="18" cy="6.2" r="1.9" fill="url(#gold)" {S(.8)}/><circle cx="31" cy="12.4" r="1.8" fill="url(#gold)" {S(.8)}/>'
     f'<circle cx="18" cy="19.5" r="2" fill="url(#ruby)" {S(.7)}/>')

icon(C, 'scroll', ['scroll', 'quest', 'message', 'letter', 'decree', 'objective'],
     f'<path d="M9 7 H27 V27 H9 Z" fill="url(#paper)" {S(1.2)}/>'
     f'<path d="M12 12 H24 M12 15.5 H24 M12 19 H21" stroke="#8a6a3a" stroke-width=".9"/>'
     f'<rect x="7" y="4.5" width="22" height="4.6" rx="2.3" fill="url(#paper)" {S(1.2)}/>'
     f'<rect x="7" y="25" width="22" height="4.6" rx="2.3" fill="url(#paper)" {S(1.2)}/>'
     f'<path d="M22 27 L21 33 L23 31.6 L25 33 L24.4 27 Z" fill="url(#red)" {S(.7)}/>'
     f'<circle cx="23" cy="26.4" r="2.6" fill="url(#red)" {S(.9)}/>')

icon(C, 'map', ['map', 'explore', 'reveal', 'treasure map', 'location'],
     f'<path d="M4 9 L12 6 L20 9 L32 6 L32 27 L20 30 L12 27 L4 30 Z" fill="url(#paper)" {S(1.2)}/>'
     f'<path d="M12 6 L12 27 M20 9 L20 30" stroke="#a88a50" stroke-width=".8"/>'
     f'<path d="M7 14 C9 18 13 13 15 18 C17 23 22 15 24 20" fill="none" stroke="#a83c22" stroke-width="1.1" stroke-dasharray="1.6 1.2"/>'
     f'<path d="M24.5 17.5 L28.5 21.5 M28.5 17.5 L24.5 21.5" stroke="#c03424" stroke-width="1.8" stroke-linecap="round"/>'
     f'<path d="M6 25 C8 23 10 24 10 21 M14 12 l2 -2 l2 2" fill="none" stroke="#6a8a3a" stroke-width=".9"/>')

icon(C, 'flag', ['flag', 'banner', 'capture', 'rally point', 'territory'],
     f'<rect x="8" y="4" width="2.2" height="28" rx=".8" fill="url(#oak)" {S(1)}/>'
     f'<circle cx="9.1" cy="4" r="1.6" fill="url(#gold)" {S(.8)}/>'
     f'<path d="M10 6.5 C15 4.5 19 9 23 7 C26 5.5 28 6 30 7 L30 19 C28 18 26 17.5 23 19 C19 21 15 16.5 10 18.5 Z" fill="url(#red)" {S(1.2)}/>'
     f'<path d="{star_path(19.5, 12.8, 3.6, 1.5)}" fill="url(#gold)" {S(.6)}/>'
     '<ellipse cx="9" cy="32" rx="5" ry="1.2" fill="#000" fill-opacity=".35"/>')

icon(C, 'clock', ['clock', 'time', 'timer', 'countdown'],
     f'<circle cx="18" cy="18" r="13" fill="url(#brass)" {S(1.3)}/>'
     f'<circle cx="18" cy="18" r="10.4" fill="url(#paper)" {S(1)}/>'
     + ''.join(T(18, 18, a, 1, f'<rect x="-.6" y="-9.6" width="1.2" height="{2.6 if a % 90 == 0 else 1.6}" fill="{K}"/>') for a in range(0, 360, 30))
     + f'<path d="M18 18 L18 10.5" stroke="{K}" stroke-width="1.6" stroke-linecap="round"/>'
       f'<path d="M18 18 L23.5 21" stroke="{K}" stroke-width="1.3" stroke-linecap="round"/>'
       f'<circle cx="18" cy="18" r="1.3" fill="url(#gold)" {S(.6)}/>')

icon(C, 'bell', ['bell', 'alarm', 'town bell', 'alert', 'notification'],
     f'<path d="M16.5 3.5 h3 v3 h-3 Z" fill="url(#brass)" {S(.9)}/>'
     f'<path d="M18 6 C11 6 10 12 10 17 C10 21 8 24 5.5 26 L30.5 26 C28 24 26 21 26 17 C26 12 25 6 18 6 Z" fill="url(#brass)" {S(1.3)}/>'
     f'<rect x="5" y="25.4" width="26" height="3" rx="1.4" fill="url(#brass)" {S(1.1)}/>'
     f'<circle cx="18" cy="30.4" r="2.4" fill="url(#iron)" {S(1)}/>'
     f'<path d="M13 10 C12 14 12.4 19 11.6 22" fill="none" stroke="#fff8d0" stroke-width="1.2" stroke-opacity=".7"/>'
     f'<path d="M3 10 C2 13 2 16 3 19 M33 10 C34 13 34 16 33 19" fill="none" stroke="#fff3a6" stroke-width="1" stroke-opacity=".7"/>')

icon(C, 'shield', ['shield', 'defence', 'defense', 'block', 'guard', 'protect'],
     f'<path d="M6 6 C12 4 24 4 30 6 C30 18 26 26 18 32 C10 26 6 18 6 6 Z" fill="url(#gold)" {S(1.4)}/>'
     f'<path d="M8.4 8 C13 6.6 23 6.6 27.6 8 C27.6 18 24.4 24.4 18 29.4 C11.6 24.4 8.4 18 8.4 8 Z" fill="url(#team)" {S(.9)}/>'
     f'<path d="M18 7.6 L18 29 C24 24.4 27.6 18 27.6 8 C23 6.6 18 7.6 18 7.6 Z" fill="url(#red)" fill-opacity=".9"/>'
     f'<path d="M8.4 16.5 H27.6" stroke="#e6ab2e" stroke-width="1.6"/>'
     f'<path d="M18 7 V29.4" stroke="#e6ab2e" stroke-width="1.6"/>')

icon(C, 'sword', ['sword', 'melee', 'weapon', 'infantry', 'blade'],
     sword(18, 23.5, 0, 14, 1.4) + sparkle(24, 7, .9))

icon(C, 'bow', ['bow', 'arrow', 'ranged', 'archery', 'weapon'],
     bow(17, 19, 45, 1.45, arrow=True))

icon(C, 'fire', ['fire', 'burn', 'flame', 'damage over time', 'torch'],
     '<circle cx="18" cy="20" r="13" fill="url(#glow)"/>' + flame(18, 31, 1.75))

icon(C, 'ice', ['ice', 'frost', 'freeze', 'cold', 'slow', 'snowflake'],
     '<circle cx="18" cy="18" r="14" fill="url(#bglow)"/>'
     + ''.join(T(18, 18, a, 1,
                 f'<path d="M-1.4 0 L-1.4 -10 L0 -13 L1.4 -10 L1.4 0 Z" fill="url(#ice)" {S(.9)}/>'
                 f'<path d="M0 -7 L-3.6 -10.4 M0 -7 L3.6 -10.4" stroke="{K}" stroke-width="2.6" stroke-linecap="round"/>'
                 f'<path d="M0 -7 L-3.6 -10.4 M0 -7 L3.6 -10.4" stroke="#d0f2ff" stroke-width="1.2" stroke-linecap="round"/>')
               for a in range(0, 360, 60))
     + f'<path d="M18 13.6 L21.8 15.8 L21.8 20.2 L18 22.4 L14.2 20.2 L14.2 15.8 Z" fill="url(#ice)" {S(1)}/>')

icon(C, 'poison', ['poison', 'toxic', 'venom', 'damage over time', 'plague'],
     '<circle cx="18" cy="19" r="13" fill="url(#ggl)"/>'
     f'<path d="M18 4 C21 10 27 15 27 22 C27 27.5 23 31 18 31 C13 31 9 27.5 9 22 C9 15 15 10 18 4 Z" fill="url(#poison)" {S(1.3)}/>'
     f'<circle cx="18" cy="21" r="4.2" fill="#f4f0dc" {S(.8)}/><rect x="15.8" y="23.6" width="4.4" height="2.6" rx=".6" fill="#f4f0dc" {S(.8)}/>'
     f'<circle cx="16.4" cy="21" r="1.1" fill="#2a3a0a"/><circle cx="19.6" cy="21" r="1.1" fill="#2a3a0a"/>'
     f'<path d="M12.5 17 C12.5 14 14.6 11 16 9.6" fill="none" stroke="#f0ffc0" stroke-width="1.1" stroke-opacity=".8"/>'
     f'<circle cx="27.5" cy="9" r="1.6" fill="url(#poison)" {S(.7)}/><circle cx="8" cy="11" r="1.1" fill="url(#poison)" {S(.6)}/>')

icon(C, 'lightning', ['lightning', 'storm', 'shock', 'energy', 'thunder'],
     '<circle cx="18" cy="18" r="14" fill="url(#glow)"/>'
     f'<path d="M21 3 L9 19 L16.5 19 L12.5 33 L27 14 L19.5 14 L24 3 Z" fill="url(#gold)" {S(1.3)}/>'
     f'<path d="M20.6 5 L12 17.4" stroke="#fffbe0" stroke-width="1" stroke-opacity=".9"/>')

icon(C, 'coin_bag', ['gold', 'money', 'coins', 'purse', 'tribute', 'loot'],
     '<ellipse cx="17" cy="30.5" rx="12" ry="2" fill="#000" fill-opacity=".35"/>'
     f'<path d="M13 10 C9 14 6 19 6 24 C6 29 10 31 16 31 C22 31 26 29 26 24 C26 19 23 14 19 10 Z" fill="url(#leather)" {S(1.3)}/>'
     f'<path d="M12 6 L14 10 L18 10 L20 6 C18 7 14 7 12 6 Z" fill="url(#leather)" {S(1)}/>'
     f'<path d="M12.5 10.4 C15 11.4 17 11.4 19.5 10.4" fill="none" stroke="#e6ab2e" stroke-width="1.6"/>'
     f'<path d="M10 18 C9 21 9 24 10 27" fill="none" stroke="#e8b882" stroke-width="1.1" stroke-opacity=".7"/>'
     + ''.join(f'<ellipse cx="{x}" cy="{y}" rx="3.6" ry="1.5" fill="url(#gold)" {S(.8)}/>' for x, y in [(27, 30), (27, 28), (27, 26)])
     + f'<circle cx="16" cy="22" r="3.4" fill="url(#gold)" {S(.9)}/><circle cx="16" cy="22" r="2" fill="none" stroke="#8c5a0c" stroke-width=".6"/>')

icon(C, 'chest', ['chest', 'treasure', 'loot', 'reward', 'storage'],
     '<ellipse cx="18" cy="30.5" rx="13" ry="2" fill="#000" fill-opacity=".35"/>'
     f'<path d="M5 15 L31 15 L31 29 L5 29 Z" fill="url(#oak)" {S(1.3)}/>'
     f'<path d="M5 15 C5 7 9 6 18 6 C27 6 31 7 31 15 Z" fill="url(#oak)" {S(1.3)}/>'
     f'<path d="M5 20 H31" stroke="#5a3410" stroke-width=".7"/><path d="M5 24.5 H31" stroke="#5a3410" stroke-width=".7"/>'
     f'<path d="M9.5 6.6 V29 M26.5 6.6 V29" stroke="{K}" stroke-width="3"/><path d="M9.5 6.6 V29 M26.5 6.6 V29" stroke="#c89a3a" stroke-width="1.6"/>'
     f'<path d="M5 15 H31" stroke="{K}" stroke-width="2.6"/><path d="M5 15 H31" stroke="#c89a3a" stroke-width="1.2"/>'
     f'<rect x="15.4" y="13" width="5.2" height="6.4" rx="1" fill="url(#gold)" {S(.9)}/><circle cx="18" cy="15.6" r=".9" fill="{K}"/><rect x="17.6" y="15.8" width=".8" height="2" fill="{K}"/>')

icon(C, 'key', ['key', 'unlock', 'door', 'access', 'quest item'],
     T(18, 18, -45, 1,
       f'<circle cx="0" cy="-8" r="6" fill="url(#gold)" {S(1.3)}/><circle cx="0" cy="-8" r="2.6" fill="url(#bgG)" {S(1)}/>'
       f'<rect x="-1.5" y="-2.6" width="3" height="16" fill="url(#gold)" {S(1.1)}/>'
       f'<path d="M1.5 7 h3.6 v2.4 h-1.6 v1.6 h2 v2.6 h-4" fill="url(#gold)" {S(1)}/>'
       f'<rect x="-1.2" y="-2" width="1" height="15" fill="#fffbe0" fill-opacity=".7"/>'))

icon(C, 'book', ['book', 'knowledge', 'research', 'lore', 'tome', 'technology'],
     f'<path d="M7 9 L23 5 L30 8 L14 12 Z" fill="url(#red)" {S(1.1)}/>'
     f'<path d="M14 12 L30 8 L30 26 L14 30 Z" fill="url(#red)" {S(1.2)}/>'
     f'<path d="M7 9 L14 12 L14 30 L7 27 Z" fill="url(#paper)" {S(1.1)}/>'
     f'<path d="M8.5 12 L12.5 13.8 M8.5 15 L12.5 16.8 M8.5 18 L12.5 19.8 M8.5 21 L12.5 22.8 M8.5 24 L12.5 25.8" stroke="#b49a68" stroke-width=".5"/>'
     f'<path d="M7 27 L14 30 L30 26" fill="none" stroke="{K}" stroke-width="1.2"/>'
     f'<path d="M18 14.4 L26 12.4 L26 22 L18 24 Z" fill="none" stroke="#e6ab2e" stroke-width="1"/>'
     f'<path d="{star_path(22, 18.2, 2.6, 1.1)}" fill="url(#gold)"/>'
     f'<path d="M27 8.6 L27 4.5 L28.6 4.5 L28.6 8.2" fill="url(#gold)" {S(.5)}/>')

icon(C, 'potion', ['potion', 'elixir', 'flask', 'drink', 'buff', 'magic'],
     '<circle cx="18" cy="21" r="13" fill="url(#glow)" opacity=".6"/>'
     f'<rect x="14.4" y="4" width="7.2" height="4.4" rx="1.2" fill="url(#oak)" {S(1)}/>'
     f'<path d="M15 8 L21 8 L21 13 C26 14.6 29 18.4 29 22.6 C29 28 24 31.5 18 31.5 C12 31.5 7 28 7 22.6 C7 18.4 10 14.6 15 13 Z" fill="#d8eef4" fill-opacity=".7" {S(1.3)}/>'
     f'<path d="M7.6 21 C11 19.4 14 22 18 21 C22 20 25 19 28.4 21 C29.2 26 25 31 18 31 C11 31 7 26.4 7.6 21 Z" fill="url(#ruby)"/>'
     f'<path d="M15 8 L21 8 L21 13 C26 14.6 29 18.4 29 22.6 C29 28 24 31.5 18 31.5 C12 31.5 7 28 7 22.6 C7 18.4 10 14.6 15 13 Z" fill="none" {S(1.3)}/>'
     f'<path d="M11 18 C10 20 10 23 11 25" fill="none" stroke="#ffffff" stroke-width="1.4" stroke-opacity=".8"/>'
     f'<circle cx="21" cy="25" r="1.2" fill="#ffd0d8" fill-opacity=".8"/><circle cx="16" cy="27" r=".8" fill="#ffd0d8" fill-opacity=".8"/>')

icon(C, 'skull', ['skull', 'death', 'kill', 'danger', 'defeat', 'undead'],
     f'<path d="M18 4 C10 4 6 9 6 15.5 C6 20 8 22.6 10.6 24 L10.6 28.5 L25.4 28.5 L25.4 24 C28 22.6 30 20 30 15.5 C30 9 26 4 18 4 Z" fill="url(#bone)" {S(1.3)}/>'
     f'<ellipse cx="13.2" cy="16.4" rx="3.2" ry="3.6" fill="#2a1a0a"/><ellipse cx="22.8" cy="16.4" rx="3.2" ry="3.6" fill="#2a1a0a"/>'
     f'<path d="M18 20 L16.2 23.4 L19.8 23.4 Z" fill="#2a1a0a"/>'
     f'<path d="M14 28.5 V25.4 M18 28.5 V25.4 M22 28.5 V25.4" stroke="{K}" stroke-width=".9"/>'
     f'<path d="M10 10 C11 7.6 13 6.4 15.4 6" fill="none" stroke="#ffffff" stroke-width="1.2" stroke-opacity=".8"/>')

icon(C, 'plus', ['plus', 'add', 'increase', 'more', 'bonus'],
     f'<path d="M14.5 5 H21.5 V14.5 H31 V21.5 H21.5 V31 H14.5 V21.5 H5 V14.5 H14.5 Z" fill="url(#gold)" {S(1.5)}/>'
     f'<path d="M16 6.5 H20 V16 H29.5" fill="none" stroke="#fffbe0" stroke-width="1" stroke-opacity=".9"/>')

icon(C, 'minus', ['minus', 'remove', 'decrease', 'less', 'penalty'],
     f'<path d="M5 14.5 H31 V21.5 H5 Z" fill="url(#dng)" {S(1.5)}/>'
     f'<path d="M6.5 16 H29.5" fill="none" stroke="#ffe0d0" stroke-width="1" stroke-opacity=".9"/>')

icon(C, 'population', ['population', 'people', 'pop cap', 'units', 'villagers', 'group'],
     ''.join(f'<circle cx="{x}" cy="{y}" r="{r}" fill="url(#skin)" {S(1)}/>'
             f'<path d="M{x - r * 1.9} {y + r * 4} C{x - r * 1.9} {y + r * 1.4} {x + r * 1.9} {y + r * 1.4} {x + r * 1.9} {y + r * 4} Z" fill="url(#{c})" {S(1.1)}/>'
             for x, y, r, c in [(9.5, 13, 3, 'leather'), (26.5, 13, 3, 'leather'), (18, 12, 3.8, 'team')]))

icon(C, 'eye', ['eye', 'vision', 'line of sight', 'reveal', 'spy', 'scout'],
     f'<path d="M3 18 C8 9 28 9 33 18 C28 27 8 27 3 18 Z" fill="#f8f2e4" {S(1.4)}/>'
     f'<circle cx="18" cy="18" r="6.6" fill="url(#green)" {S(1)}/>'
     f'<circle cx="18" cy="18" r="3" fill="{K}"/><circle cx="16" cy="16" r="1.4" fill="#fff"/>'
     f'<path d="M3 18 C8 9 28 9 33 18" fill="none" stroke="{K}" stroke-width="2"/>'
     + ''.join(f'<path d="M{x} {y1} L{x + dx} {y2}" stroke="{K}" stroke-width="1.2" stroke-linecap="round"/>' for x, dx, y1, y2 in [(10, -1.4, 12, 8.6), (18, 0, 10.2, 6.4), (26, 1.4, 12, 8.6)]))

icon(C, 'hourglass', ['hourglass', 'time', 'wait', 'duration', 'timer'],
     f'<rect x="8" y="4" width="20" height="3.4" rx="1" fill="url(#oak)" {S(1.1)}/>'
     f'<rect x="8" y="28.6" width="20" height="3.4" rx="1" fill="url(#oak)" {S(1.1)}/>'
     f'<path d="M11 7.4 C11 14 16 15.5 16 18 C16 20.5 11 22 11 28.6 L25 28.6 C25 22 20 20.5 20 18 C20 15.5 25 14 25 7.4 Z" fill="#d8eef4" fill-opacity=".55" {S(1.1)}/>'
     f'<path d="M13 11 C15 14 17 15 18 15 C19 15 21 14 23 11 Z" fill="url(#thatch)"/>'
     f'<path d="M18 17 L18 24" stroke="#e8c060" stroke-width=".8"/>'
     f'<path d="M12.2 28.4 C13 24.6 16 23 18 23 C20 23 23 24.6 23.8 28.4 Z" fill="url(#thatch)"/>'
     f'<path d="M9.4 7.4 V28.6 M26.6 7.4 V28.6" stroke="{K}" stroke-width="2.6"/><path d="M9.4 7.4 V28.6 M26.6 7.4 V28.6" stroke="#b07a3a" stroke-width="1.2"/>')

icon(C, 'gear', ['gear', 'settings', 'cog', 'machine', 'engineering', 'options'],
     ''.join(T(18, 18, a, 1, f'<rect x="-2.6" y="-14" width="5.2" height="6" rx=".8" fill="url(#steel)" {S(1)}/>') for a in range(0, 360, 45))
     + f'<circle cx="18" cy="18" r="10" fill="url(#steel)" {S(1.3)}/>'
       f'<circle cx="18" cy="18" r="4" fill="url(#bgG)" {S(1.1)}/>'
       f'<path d="M11 13 C12 11 14 9.6 16 9" fill="none" stroke="#fff" stroke-width="1.2" stroke-opacity=".7"/>')

icon(C, 'heart', ['heart', 'health', 'hit points', 'life', 'love'],
     f'<path d="M18 31 C10 25 4 20 4 13 C4 8.4 7.4 5 11.4 5 C14.4 5 16.6 6.8 18 9.4 C19.4 6.8 21.6 5 24.6 5 C28.6 5 32 8.4 32 13 C32 20 26 25 18 31 Z" fill="url(#ruby)" {S(1.4)}/>'
     f'<path d="M8 11 C8 9 9.6 7.6 11.6 7.6" fill="none" stroke="#fff" stroke-width="1.6" stroke-linecap="round" stroke-opacity=".85"/>')

icon(C, 'anvil', ['anvil', 'blacksmith', 'forge', 'smithing', 'upgrade'],
     '<ellipse cx="18" cy="31" rx="12" ry="1.8" fill="#000" fill-opacity=".35"/>'
     f'<path d="M3 11 C6 14 9 14 11 14 L11 13 L31 13 L31 18 L27 18 C24 19 23 21 23 24 L26 25 L26 30 L10 30 L10 25 L13 24 C13 21 12 19 9 18 C6 17 4 15 3 11 Z" fill="url(#iron)" {S(1.3)}/>'
     f'<path d="M11 13 L31 13 L31 14.6 L11 14.6 Z" fill="#c4c8d0"/>'
     + sparkle(22, 8, .9, '#ffd060') + sparkle(27, 5.5, .6, '#ffb030') + sparkle(16, 6.4, .55, '#ffd060'))

icon(C, 'hammer', ['hammer', 'build', 'construct', 'craft', 'work'],
     T(18, 18, 35, 1,
       f'<rect x="-1.5" y="-6" width="3" height="21" rx="1" fill="url(#oak)" {S(1.1)}/>'
       f'<rect x="-1.5" y="10" width="3" height="5" rx="1" fill="url(#leather)" {S(.9)}/>'
       f'<path d="M-9 -12 L6 -12 L9 -10 L9 -6 L6 -4 L-9 -4 Z" fill="url(#iron)" {S(1.3)}/>'
       f'<path d="M-8 -10.8 L5.6 -10.8" stroke="#c4c8d0" stroke-width="1"/>'))

icon(C, 'torch', ['torch', 'light', 'fire', 'burn', 'night'],
     '<circle cx="18" cy="12" r="12" fill="url(#glow)"/>'
     + T(18, 18, 15, 1,
         f'<path d="M-2 -2 L2 -2 L1.2 15 L-1.2 15 Z" fill="url(#oak)" {S(1.1)}/>'
         f'<path d="M-3 -4.6 L3 -4.6 L2.2 -.6 L-2.2 -.6 Z" fill="url(#iron)" {S(1)}/>')
     + flame(17, 13.4, 1.05))

icon(C, 'crossed_swords', ['battle', 'combat', 'fight', 'war', 'swords', 'attack'],
     sword(11, 25, 45, 15, 1.25) + sword(25, 25, -45, 15, 1.25))

icon(C, 'lock', ['lock', 'locked', 'unavailable', 'closed', 'secure'],
     f'<path d="M11 16 L11 11 C11 6.6 14 4 18 4 C22 4 25 6.6 25 11 L25 16" fill="none" stroke="{K}" stroke-width="4.4"/>'
     f'<path d="M11 16 L11 11 C11 6.6 14 4 18 4 C22 4 25 6.6 25 11 L25 16" fill="none" stroke="url(#steel)" stroke-width="2.4"/>'
     f'<rect x="7" y="15" width="22" height="16" rx="2.4" fill="url(#gold)" {S(1.3)}/>'
     f'<circle cx="18" cy="21.5" r="2.2" fill="{K}"/><path d="M16.9 22 L16.2 27 L19.8 27 L19.1 22 Z" fill="{K}"/>'
     f'<path d="M9 18 V28" stroke="#fffbe0" stroke-width="1" stroke-opacity=".8"/>')

icon(C, 'trophy', ['trophy', 'cup', 'win', 'victory', 'achievement', 'reward'],
     f'<path d="M10 8 C4 8 4 16 11 17 M26 8 C32 8 32 16 25 17" fill="none" stroke="{K}" stroke-width="3.4"/>'
     f'<path d="M10 8 C4 8 4 16 11 17 M26 8 C32 8 32 16 25 17" fill="none" stroke="#e6ab2e" stroke-width="1.6"/>'
     f'<path d="M9 5 L27 5 L27 9 C27 16 23 20 18 20 C13 20 9 16 9 9 Z" fill="url(#gold)" {S(1.3)}/>'
     f'<rect x="16" y="20" width="4" height="5" fill="url(#gold)" {S(1)}/>'
     f'<path d="M11 25 L25 25 L27 31 L9 31 Z" fill="url(#gold)" {S(1.2)}/>'
     f'<rect x="13.4" y="27" width="9.2" height="2.2" fill="#6e4e14"/>'
     f'<path d="M12 7 C12 12 13 15 15 17" fill="none" stroke="#fffbe0" stroke-width="1.2" stroke-opacity=".85"/>')

icon(C, 'gem', ['gem', 'jewel', 'ruby', 'relic', 'treasure', 'precious'],
     '<circle cx="18" cy="17" r="13" fill="url(#glow)" opacity=".55"/>'
     f'<path d="M5 13 L11 6 L25 6 L31 13 L18 31 Z" fill="url(#ruby)" {S(1.4)}/>'
     f'<path d="M5 13 L31 13 M11 6 L14 13 L18 6 L22 13 L25 6 M14 13 L18 31 L22 13" fill="none" stroke="#6e0a18" stroke-width=".8"/>'
     f'<path d="M11 6 L14 13 L5 13 Z" fill="#ffd0d8" fill-opacity=".7"/><path d="M14 13 L18 6 L22 13 Z" fill="#ffffff" fill-opacity=".35"/>'
     + sparkle(10.5, 8.6, .8))

icon(C, 'check', ['check', 'done', 'complete', 'yes', 'confirm', 'objective complete'],
     f'<path d="M4 18 L9 13 L15 19 L27 5 L32 10 L15 30 Z" fill="url(#upg)" {S(1.5)}/>'
     f'<path d="M6 18 L9 15 L15 21.4 L27 7.4" fill="none" stroke="#eaffd0" stroke-width=".9" stroke-opacity=".85"/>')

icon(C, 'cancel', ['cancel', 'no', 'close', 'stop', 'delete', 'failed'],
     f'<path d="M5 10 L10 5 L18 13 L26 5 L31 10 L23 18 L31 26 L26 31 L18 23 L10 31 L5 26 L13 18 Z" fill="url(#dng)" {S(1.5)}/>'
     f'<path d="M7 10 L10 7 L18 15 L26 7" fill="none" stroke="#ffe0d0" stroke-width=".9" stroke-opacity=".85"/>')
