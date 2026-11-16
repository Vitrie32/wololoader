# Research: UI sound playback and the on-screen countdown clocks

Game: AoK HD.exe 5.8, base 0x400000. Disassembly quoted from `~/.cache/wl-test/exe.s` (objdump, no raw bytes).
GameScreen = `[0xaf3d90]`. Marks: **[V]** = read in the disassembly, **[U]** = unverified / inferred.

---

## 1. SOUND

### 1.1 The play function: `0x62bce0` **[V]**

```
62bce0: push %ebp / mov %esp,%ebp / push $-1 / push $0x946b08 / ...        ; SEH prologue
62bd08: mov  0x8(%ebp),%eax          ; id = first stack arg
62bd0b: push %eax
62bd0c: lea  -0x14(%ebp),%eax
62bd0f: push %eax                    ; &out (sound object ptr, refcounted)
62bd10: call 0x62b580                ; lookup, ecx (this) is passed through unchanged
62bd1c: mov  -0x14(%ebp),%ecx
62bd1f: test %ecx,%ecx
62bd21: je   0x62bd28                ; null entry = silently nothing
62bd23: mov  (%ecx),%eax
62bd25: call *0x8(%eax)              ; vtable slot 2 of the sound object = PLAY (no args)
 ...
62bd58: ret  $0xc
```

- Convention: `__thiscall`, `ret 0xc`, ECX = sound manager `[0xaf2c50]`.
- Stack args: `id, x, y`. **Only `id` is read** (`0x8(%ebp)`); the other two are always pushed as 0, 0.
- The lookup `0x62b580` (ret 8) is **[V]**:
  ```
  62b5a3: mov  0xc(%ebp),%eax           ; id
  62b5ad: cmp  $0xffffffff,%eax
  62b5b0: jne  0x62b5bd                 ; id == -1 -> *out = 0 (no sound)
  62b5bd: cmp  $0x33,%eax
  62b5c0: jae  0x62b5f0                 ; id >= 51 -> call 0x46ae70 (error path, do not do this)
  62b5c2: mov  (%ecx,%eax,4),%ecx       ; ecx[id] = sound object
  62b5ce: incl 0x4(%ecx)                ; add ref
  ```
  So `[0xaf2c50]` points to a plain array of 0x33 (51) sound-object pointers (the object is 0xcc = 51*4 bytes:
  `62b625: push $0xcc / call 0x8d47df (operator new) / mov %eax,%ecx / call 0x62b2c0` and `62b656: mov %eax,0xaf2c50`).
- Safety for our DLL: check `[0xaf2c50] != 0` (before the sound system is created, `0x62b580` would read through NULL),
  and `0 <= id < 0x33`. A NULL slot is harmless.

Real call sites **[V]**:

```
4b2454: mov  0xaf2c50,%ecx        ; (cmpw $0x3a,0x16(%eax) just before: some event type 0x3a)
4b245a: push $0x0
4b245c: push $0x0
4b245e: push $0x25                ; id 0x25 = capsheep.wav
4b2460: call 0x62bce0

6342c1: mov  0xaf2c50,%ecx        ; the chat listener (0x634270), runs for every chat line of type != 1
6342c7: push $0x0
6342c9: push $0x0
6342cb: push $0x2                 ; id 2 = chatrcvd.wav (the chat blip)
6342cd: call 0x62bce0
```

C for the DLL:

```c
typedef void (__thiscall *snd_play_fn)(void *mgr, int id, int a, int b);   /* ret 0xc */
#define SND_MGR   (*(void **)0xaf2c50)
static void wl_play_ui_sound(int id) {
    if (!SND_MGR || id < 0 || id >= 0x33) return;
    ((snd_play_fn)0x62bce0)(SND_MGR, id, 0, 0);
}
```

(Call from the game thread, e.g. inside the GameScreen frame hook. Not tested in game.)

### 1.2 Which argument of `0x664840` is the sound id **[V]**

`TMessagePanel::add_message` (0x664840, `__thiscall`, **`ret $0x20`** = 8 stack args):

| arg | offset | meaning | proof |
|---|---|---|---|
| 1 | 0x8  | type | `mov 0x8(%ebp),%esi`, `cmp %esi,0x130(%ebx)` |
| 2 | 0xc  | text | `mov 0xc(%ebp),%eax` |
| 3 | 0x10 | colour (byte) | `mov 0x10(%ebp),%al / mov %al,0x344(%ebx)` |
| 4 | 0x14 | flag (byte) | `mov 0x14(%ebp),%al / mov %al,0x345(%ebx)` |
| 5 | 0x18 | refcounted object (a **font**, see 2.3) | `mov 0x18(%ebp),%edi ... incl 0x4(%edi)`, stored at `0x340(%ebx)` |
| 6 | 0x1c | duration in ms (`> 0` is stored at `0x33c`) | `66496c: mov 0x1c(%ebp),%eax / test / jle / mov %eax,0x33c(%ebx)` |
| 7 | 0x20 | optional char* (32 chars max, a name/prefix) | `0x663a50(dst, -0x24(%ebp), 0x20)` in the queue path |
| 8 | **0x24** | **sound id, -1 = none** | below |

```
66499e: mov  0x24(%ebp),%eax
6649a1: cmp  $0xffffffff,%eax
6649a4: je   0x6649b6               ; -1 -> no sound
6649a6: mov  0xaf2c50,%ecx
6649ac: push $0x0
6649ae: push $0x0
6649b0: push %eax                   ; id
6649b1: call 0x62bce0
```

If the panel is busy the message is queued (`664b9d` path) and the id is saved: `664beb: mov %eax,0x228(%esi)` where
`%eax = 0x24(%ebp)`; the replayer `0x664c90` pushes it again (`664ca9: push 0x228(%edx)` ... `call 0x664840`), so the sound is played later.

What the game passes:
- `display_message` 0x6cbed0 always pushes `-1, 0, -1, 0, flag, color, text, type`, so arg7 = 0 and **arg8 (sound) = -1**:
  ```
  6cbed9: push $0xffffffff   ; arg8 sound = -1
  6cbedb: push $0x0          ; arg7
  6cbedd: push $0xffffffff   ; arg6 duration
  6cbedf: push $0x0          ; arg5 font
  ```
  => `wl.chat`/`display_message` can never carry a sound. Every one of the ~28 notification sites in 0x6ba3xx..0x6c2dxx
  (e.g. `6ba65f..6ba66e`, `6bb020..6bb042`) also pushes `$0xffffffff` as the first (= arg8) push, i.e. **no site of the
  notification panel carries a sound id**; the game plays those sounds separately via direct `0x62bce0` calls.
- The only `0x664840` caller with a real id is the line builder `0x6bf910` (the one that feeds the top chat area):
  `6bf9b1: push $0x2` is the first push of the call frame. Because `0x472020` is a tiny `ret 4` array getter
  (`472026: mov (%ecx,%eax,4),%eax`, here `[0xada2a8][0xb]` = the font), the final frame is
  `type=3|0, text, colour, 0, font, ms, p2, 2` so **sound id 2 (chatrcvd)** is passed there. [V from the push sequence;
  not run.]

Easiest ways to get a sound from our DLL:
1. Call `wl_play_ui_sound(id)` directly (preferred, no side effect on the panel).
2. Or call `0x664840` yourself with 8 args (`ret 0x20`) and arg8 = id (needs a panel object and a font: more risk).

### 1.3 Sound id table (`0x62b370` registration) **[V]**

The table is filled by 52 calls of `0x62b370` (thiscall on a local holder, 5 stack args:
`slot, name, DRS id, mode(1|2), volume 1.0f`, `ret 0x14`) in the function at `0x62b6b0`, e.g.

```
62b6b0: push %ebp ... push $0x3f800000 ; volume  | push $0x1 | push $0xc47c (DRS id) | push $0x9ce9cc ("button1.wav") | push $0x0 (slot)
62b6d0: call 0x62b370
```

Slot -> wav name (names read from `.rdata`, `0x9ce9cc..`; slot numbers are the first pushed arg). Slot 2 is registered twice
(`0xc47e` mode 1 at `62b70c`, then mode 2 at `62b81a`); 0x30 and 0x31 share DRS id 0xc4bf.

| id | wav | likely use |
|---|---|---|
| 0x00 | button1.wav | generic button click |
| 0x01 | button2.wav | button click 2 |
| 0x02 | **chatrcvd.wav** | chat message blip **[V: 0x6342cb]** |
| 0x03 | cantdo.wav | "cannot do" error (many sites 0x62ea7f, 0x6318dd, ...) **[V constant, meaning from name]** |
| 0x04 | tribute.wav | tribute received |
| 0x05 | resdone.wav | research done |
| 0x06 | pkilled.wav | player killed / resigned |
| 0x07 | pdropped.wav | player dropped |
| 0x08 | mstart.wav | monument/wonder (mission) start |
| 0x09 | mdone.wav | mission done |
| 0x0a | mkilled.wav | mission killed |
| 0x0b | artheld.wav | relic held (sites 0x632db7...) |
| 0x0c | artlost.wav | relic lost |
| 0x0d | convwarn.wav | being converted warning |
| 0x0e | convdone.wav | conversion done |
| 0x0f | atakwarn.wav | "you are under attack" |
| 0x10 | enemwarn.wav | enemy warning |
| 0x11 | flare.wav | flare |
| 0x12 | needres.wav | need resources |
| 0x13 | housing.wav | housing (needhous?) **[V 0x4bd056]** |
| 0x14 | loosebld.wav | building lost |
| 0x15 | takebld.wav | building taken |
| 0x16 | archupg.wav | archery upgrade |
| 0x17 | fortupg.wav | fortification upgrade |
| 0x18 | flaresnd.wav | flare sound |
| 0x19 | farmdied.wav | farm died (0x631c5b) |
| 0x1a | gather.wav | gather |
| 0x1b | resdone2.wav | research done 2 |
| 0x1c..0x22 | button3..button9.wav | click variants |
| 0x23 | rollover.wav | mouse-over |
| 0x24 | needhous.wav | need houses (0x633a83) |
| 0x25 | capsheep.wav | sheep captured **[V 0x4b245e]** |
| 0x26 | capgaia.wav | Gaia unit captured (0x5aab85) |
| 0x27 | townbell.wav | **town bell** |
| 0x28 | townrcal.wav | town bell recall (villagers back to work) |
| 0x29 | buy.wav | market buy |
| 0x2a | sell.wav | market sell |
| 0x2b | gatel.wav | gate locked |
| 0x2c | gateu.wav | gate unlocked |
| 0x2d | objctive.wav | objective (0x631d8d) |
| 0x2e | puprelic.wav | relic picked up |
| 0x2f | wolfwarn.wav | wolf warning |
| 0x30 | spies.wav | spies / treason |
| 0x31 | treaty.wav | treaty |
| 0x32 | athiesm.wav | atheism |

Names are exact (from the exe); the "use" column is inferred from the file name. **The monk "wololo" is not in this
table [V: no such wav among the 51].** It is a unit/dat sound (played from the unit's sound ids in the dat through the
sound system `0xada234` with `0x46bba0`/`0x46bfc0`, seen in `0x62b370`); I did not trace that path. **[U]**

`bell` request: id `0x27` (townbell.wav) is the closest thing to a bell.

---

## 2. ON-SCREEN COUNTERS (wonder / relic / monument timers)

### 2.1 What they are **[V]**

GameScreen owns **10 "countdown clock" panels** in an array at `GameScreen+0xbfc` (pointers, 4 bytes each, i = 0..9) plus
a `treaty_countdown` panel at `+0xc24`. Creation (`0x6b6023..0x6b6180`), class ctor `0x71b1b0` (object size `0x16c`):

```
6b60c5: push $0x16c            ; sizeof
6b60ca: call 0x8d47df          ; game operator new
...
6b60f7: call 0x71b1b0          ; ctor(this, parent)
...
6b613c: push $0x9d55a0         ; "countdown_clock[%d]"
6b6148: call 0x433ec0          ; format the name
...
6b617d: cmp  $0xa,%eax / jl 0x6b60c5      ; 10 panels
```

Vtable `0x9d99e4` (set by the ctor at `71b26e`). Slots that differ from the base panel: 0 (dtor 0x71b390),
**15 = 0x71b410 (draw)**, **17 = 0x71b950 (per-frame update of the value)**.

Per-panel fields: `+0x144` mode, `+0x14c` display kind (1 = debug clock), `+0x158` game/world, `+0x15c` player object,
`+0x164` current value (years), `+0x134` text colour, `+0x130` font.

The text builder is **`0x71b580`** (`__thiscall`, no stack args, plain `ret`, returns `char*` = static buffer `0xaf4178`, 4096 bytes):

```
71b8a0..71b8b5 : (value+4)/5*5   ; rounds to 5 years  (mul by 0xcccccccd, shr 2, *5)
71b8b8: sub $0x4,%eax / je 0x71b8e1      ; mode 4 -> push $0x2c25
71b8bd: sub $0x1,%eax / je 0x71b8da      ; mode 5 -> push $0x2c26
71b8c2: sub $0x1,%eax ... je 0x71b8d3    ; mode 6 -> push $0x2c27
71b8cc: push $0x2c24                     ; other  -> push $0x2c24
71b8fc: call 0x483f00                    ; string table lookup (id -> text)
71b918: call 0x42fc10                    ; sprintf into 0xaf4178 (0x1000 bytes)
71b920: mov $0xaf4178,%eax / ... ret
```

String ids (from `resources/en/strings/key-value/key-value-strings-utf8.txt`, 0x2c24 = 11300):
`11300 "%d Years"`, `11301 "Wonder: %d Years"`, `11302 "Relics: %d Years"`, `11303 "Monument %d Years"`.
So modes: **4 = Wonder, 5 = Relics, 6 = Monument**, i.e. the "Wonder: 4:32" style lines (years in the English text).

Draw (slot 15, `0x71b410`): calls `0x71b580` for the text, then draws it with the panel's font into the panel rect:

```
71b43d: call 0x71b580            ; text -> %edi
71b46d: call 0x49e3c0 / 71b483: call 0x49ea90   ; draw context + font (+0x130)
71b4fb: call 0x4a16f0            ; text draw, ret $0x14 (5 stack args: ctx, text, len, &rect, flags)
```

Who selects the mode: `GameScreen::update_clocks` = **`0x6c60f0`** (`__thiscall`, no args, plain `ret`; callers at
`0x63243c, 0x632b70, 0x633027, 0x633223, 0x633563, 0x633626, 0x6b7e06, 0x6ba3bb, 0x6c2195`, i.e. on events, **not** every frame):

```
6c6180..6c6192: inc edi; push %esi (player idx); push $0x4; mov (%eax),%ecx (panel[i]); call 0x71bac0   ; player [0x1adc] > 0  -> Wonder
6c6245..6c6253: push %esi; push $0x5; ...; call 0x71bac0     ; player float [0x212c] != 0 -> Relics
6c6302..6c6310: push %esi; push $0x6; ...; call 0x71bac0     ; player float [0x2128] != 0 -> Monument
6c6341..6c635a: remaining panels: push 0; push 0; call 0x71bac0   ; mode 0 = hide
```

`0x71bac0` = `set_mode(panel, mode, player_index)` (`__thiscall`, **`ret 8`**): stores `+0x144` mode, `+0x15c` player,
colour = the player's colour (`+0x134/0x138`, white `0xffffff` if no player), then shows/hides:

```
71bb83: mov %esi,%ecx / mov (%esi),%eax / mov 0x10(%eax),%eax     ; vtable slot 4 = set_visible(bool)
71bb8a: test %edx,%edx / jne 0x71bba0
71bb8e: push %edx (0) / call *%eax    ; mode 0 -> hide
71bba0: push $0x1 / call *%eax        ; else -> show
71bba4..71bba9: mov 0x10(%esi),%ecx / call *0x44(%eax)             ; relayout of the parent
```

The panels are laid out by the parent/container (offset `0x10` of the panel); the visible ones stack. The exact screen
position was not decoded. **[U]**

### 2.2 Smallest safe way to add one line of our own text **[proposal, not run]**

**Plan A (recommended): reuse the last countdown panel and replace only its text.**

1. Detour `0x71b580` (builder). Prologue is `55 8b ec 6a ff` = `push %ebp; mov %esp,%ebp; push $-1` (5 bytes, 3 whole
   instructions, no relative branches) **[V: 71b580..71b585]**; stolen bytes `55 8b ec 6a ff`.
   Replacement: `char *__fastcall hook(void *self, void *edx)`:
   `if (self == panel[9] && g_wl_counter_text[0]) return g_wl_counter_text; else return orig(self);`
   where `panel[9] = *(void**)((char*)GameScreen + 0xbfc + 9*4)`. The result must stay valid after return (static buffer).
2. In the per-frame GameScreen hook (0x6c3310 slot 17) keep that panel visible:
   `if (((int*)panel9)[0x144/4] == 0) ((void(__thiscall*)(void*,int,int))0x71bac0)(panel9, 4, 0);`
   (mode 4 with player index 0: `0x71b950` then sets `+0x15c = NULL` and the value 0 (`71baf8..71bb15`, `71ba72`), our text
   hook overrides the string anyway; colour white.) When we want it gone, call `0x71bac0(panel9, 0, 0)`.
   The game's own `0x6c60f0` re-hides unused panels (`push 0; push 0; call 0x71bac0` at `6c6356`), so doing this every
   frame (only when mode is 0) is required, it is cheap.
3. Limits **[U]**: if the game itself needs all 10 panels (10 simultaneous timers), our panel is used by the game: unlikely.
   Text width must fit the panel rect (the draw uses `+0xb8..+0xc4` rect); a long line may be clipped.
   Mode 4 is processed per frame by `0x71b950` slot 17 with a NULL player: reads nothing (`71b9dd: test eax / je 0x71ba72`).

**Plan B (own text anywhere):** `0x4a16f0` is the game's text draw (`__thiscall`, ECX = a 0x20-byte style block
`{+0..0x10 colour/flags, 3, 1}` built at `71b488..71b4b5`, 5 stack args, `ret $0x14`). It needs a draw context (`0x49e3c0`)
built from the panel's surface; I did not decode the argument meaning, so building it outside a panel's draw is
unsafe without more work. **[U]** Hooking the draw slot 15 (`0x71b410`) of panel 9 would give a valid context and rect but
that is just Plan A with more code.

### 2.3 Confidence

- Sound play `0x62bce0`/`0x62b580`, array of 51, `[0xaf2c50]`, call sites, wav names: **high** (disassembly read).
- Sound id = 8th arg (`0x24`) of `0x664840`; `display_message` passes -1: **high**. Id 2 through `0x6bf910`: **medium**.
- Countdown panel class, modes 4/5/6 = Wonder/Relics/Monument, `0x71bac0`, `0x71b580`, `0xaf4178`: **high**;
  the `0x1adc / 0x212c / 0x2128` player-field meanings are only inferred from the strings (11301..11303).
- Plan A actually working in game and the on-screen position: **untested / unknown**.
- "wololo" sound id: **not found**.
