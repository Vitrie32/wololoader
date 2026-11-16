# "Overflow selection" design: keep selected[60], add our own list

Status: feasibility study (2026-11-15). Nothing was patched or run. Addresses below come from
`~/.cache/wl-test/exe.s` (objdump of `AoK HD.exe` 5.8). Each item is marked
**VERIFIED** (I read the disassembly) or **UNVERIFIED** (inferred / not read).

Idea: the game keeps `player+0x160..+0x24c selected[60]`, `+0x254 count`, `+0x15c primary` exactly as
it is. Units that would be the 61st+ go into a DLL list instead. Orders the game builds for the
selection are repeated for the overflow units.

Short answer: this works, needs about 6 core hooks (7 with the box-select buffer
work, ~13 functions touched at most), and its failure mode is "overflow units ignore an order",
not memory corruption. The most important finding is that **one hook on the command send function
`0x80ef70` covers all order types** (see section 2.3), and **one hook on the selection getter
`0x74ef20` gives us the exact same filtering for overflow units for free** (see section 2.2).

---

## 1. Where the 60 limit is enforced when ADDING

### 1.1 `WorldPlayer::select_unit` 0x754a20 (the funnel) - VERIFIED

`__thiscall(player=ecx, unit, flag)`, `ret 8`. Prologue `push ebp; mov ebp,esp; push esi; push edi`,
`mov esi,ecx`, `mov edi,[ebp+8]`. Direct callers (E8 rel32 scan, 40 sites): 0x4a93c9, 0x6665d0..0x6667b2,
0x668705, 0x6687a2, 0x67ecbf..0x67f692, 0x7192cb, 0x74fd64, 0x754894, 0x7548ad, 0x7548ea, 0x754936,
0x7549e4, 0x86f48b, 0x86f52c, 0x86f546, 0x87c9e3, 0x87cd2d, 0x87cebb, 0x8863d0, 0x8865fe, 0x886c4e,
0x886cc7, 0x886ddb, 0x887098, 0x8870f1, 0x8871b7, 0x887332 (plus 0x754910 helper callers).

The add path (quoted):

```
754aae: mov    0x3a(%edi),%al
754ab1: test   $0x1,%al            ; already selected?
754ab3: je     0x754b1d            ; not selected -> add path
754b1d: mov    0x254(%esi),%eax    ; count
754b23: cmp    $0x3c,%eax          ; <-- LIMIT #1
754b26: jge    0x754c15            ; -> pop edi; xor eax,eax; pop esi; pop ebp; ret 8   (returns 0)
754b2c: mov    %edi,0x160(%esi,%eax,4)
754b33: incl   0x254(%esi)
754b39: cmpl   $0x0,0xc(%ebp)
754b3d: mov    %edi,0x15c(%esi)    ; primary = this unit
754b43: movb   $0x1,0x3a(%edi)     ; unit->flags |= SELECTED
... (sound via 0x604790 when flag arg != 0)
754bb0: cmpl   $0x3c,0x254(%esi)   ; <-- LIMIT #2, inside the "add same-group units" loop
754bb7: jge    0x754c09
754be9: mov    %ebx,0x160(%esi,%eax,4) ; group member added
754bf0: incl   0x254(%esi)
754bf6: movb   $0x1,0x3a(%ebx)
```

Return value: 0 at the limit (`0x754c15`), 1 on success (`0x754c0a`: `pop edi; mov eax,1; pop esi; pop ebp; ret 8`).
So **every add except box select reaches `0x754b23` with the candidate unit in `edi`**; that is the
catch point. Candidates that are *already selected* never reach it (flag bit0 test at 0x754ab1), so
overflow units must also get `0x3a |= 1` (we set it) to keep the dedupe working.

### 1.2 Callers and what they do on failure (0 returned)

| Path | Function | On select_unit()==0 | Verified |
|---|---|---|---|
| Double-click "select all of type on screen" | `0x86f3f0` (`ret 0x10`) loops the player's own unit list (`0x18(player)`: count `+8`, array `+4`) and calls select_unit at `0x86f52c`, `0x86f48b`, `0x86f546` | ignored, loop continues (`0x86f531: inc edi; cmp -0x2c(%ebp),%edi; jl`) | yes |
| Control group recall (1..9) | `0x7547f0` (`__thiscall(player, group)`, `ret 4`) loops the player's unit list, calls `0x754a20` at `0x754894/0x7548ad/0x7548ea`; counts successes in `ebx` | ignored, loop continues | yes |
| Select helper | `0x754910` (`ret 8`) calls select_unit at `0x754936`, then at `0x754a03` writes `mov %ebx,0x15c(%eax)` (primary = the unit, even if it was only added to overflow) | returns early (`0x754a16`) | yes |
| Idle villager / select-all-military hotkeys | callers of `0x755450`+`0x754910`: `0x6bd518/0x6bd52d`, `0x6bd645/0x6bd65a`, `0x6be899/0x6be8ae`, `0x80edc8/0x80edd4` | via 0x754910 | partly (call sites listed, bodies not read) |
| **Drag-box select** | `0x87cc60` (`__thiscall(view, x1,y1,x2,y2 ...)`, `ret 0x14`) | **breaks out of the loop**: `87cd2d call 0x754a20; 87cd34 je 0x87cd4f` | yes |
| Click / shift-click | `0x886970` and `0x887660` | not read | no |

### 1.3 Box select is capped BEFORE select_unit (candidate buffer 0xde0430)

`0x87cee0` (`__thiscall(view, 9 args)`, `ret 0x24`) scans map tiles for objects in the rectangle
and fills a static buffer of 12-byte records `{int id; word flags; word x; word y}`:

```
87cfb7: lea    0xde0434(,%eax,4),%edi      ; &buf[n].flags  (eax = n*3)
87d04f: mov    $0xde0430,%ecx              ; dedupe scan start
87d060: mov    %esi,-0x4(%edi)             ; buf[n].id = id
87d07a: add    $0xc,%edi
87d07d: cmp    $0xde0704,%edi              ; <-- LIMIT #3 (0xde0430 + 60*12 + 4)
87d083: jge    0x87d0d5
87d0d5: ... mov $0x3c,%eax ... ret $0x24   ; returns 60
87d0bb/87d0cb: 0xde0434(,%eax,4) dedupe writes
```

So a box over 100 units yields 60 records; 61+ are silently dropped before select_unit sees them.
Same buffer users (grep of `0xde043x/0xde0704`): `0x86ef4f/0x86efb2`, `0x86f8bf..0x86f8d3` (in
`0x86f880`, cap `cmp $0x3c` at `0x86f8a4`), `0x87cc60` (`87ccad`, `87cdba`), `0x87cee0`,
`0x87d110` (`87d4c2`, cap `87d4fb cmp $0xde0704`, `87d57c mov $0x3c`), `0x887660` (`88774d`,
`8877ae`, `8877bf`, cap at `0x887747`), `0x887cda/0x887ceb`. The buffer is followed directly by
other globals (`0xde0700`, `0xde0704`, `0xde0708`), so it cannot be grown in place; it has to be
moved (every user needs its base/limit immediates changed) or the box has to be handled
differently (section 3, option B).

### 1.4 Which function "receives the candidates beyond 60"

* Double-click, ctrl-group recall, idle/all-military hotkeys, click-add: **`select_unit` 0x754a20**
  (limit at `0x754b23`).
* Box select: **nobody** gets them today (dropped inside `0x87cee0`). To catch them either enlarge
  the buffer (7 functions) or re-gather the rectangle ourselves in a wrapper around `0x87cc60`.

---

## 2. Command creation: where orders are built

### 2.1 Chain

UI / right-click code (`0x86bbb0` right-click order dispatcher, `0x86e030`, `0x6bfc40`, `0x6bdc80`,
`0x6bda80`, `0x6bf150`, `0x6bf5f0`, `0x6becd0`, `0x6cc4a0`, `0x5c5d00`, `0x542f40`) ask the player
for the *filtered selection* through a virtual call, then pass `(unit pointer array, count)` to a
TRIBE_Command builder, which allocates a packet, writes the **unit ids** into it and sends it:

```
86bc26..86bc3d: push 9 args ; mov 0x9c(%edx),%eax   ; call *%eax        ; player vtable slot 0x9c = 0x74ef20
86bc99:         call 0x812ee0   ; (units**, count, target, x, y)         ; command builder, ret 0x14
812fb1:         call 0x80ef70   ; (packet, size, player) - the send function, ret 0xc
```

### 2.2 The selection getter `0x74ef20` - VERIFIED (and plan_agy.md is wrong on one point)

`__thiscall(player, int **out_list, short *out_count, short a, ushort b, ushort c, short d, char e, char f, uint g)`,
`ret 0x24`, returns `al` (bool). It walks `selected[0..59]` (loop `cmp $0x3c,%eax` at `0x74f085` is the
index bound), applies filters (alive: `cmpb $2,0x4c(%esi)`; owner `cmp %ebx,0x10(%esi)` unless arg `0x20` set;
speed class `[type+0x95]`, class `[type+0x16]`, type `[type+0x10]`, stance `[type+0x97]`, can-act bits
`[type+0xa4]`...) and copies **unit pointers (not ids; plan_agy.md says ids)** to the static array
`0xaf4e40` (`74f070: mov %eax,0xaf4e40(,%ecx,4)`), then `*out_list = 0xaf4e40`, `*out_count = n` (word).

It has no direct `call` instruction (E8/E9 scan finds none). Its only references are slot `0x9c` of the player
vtables (6 vtables: `0x9dbc7c, 0x9dc24c, 0x9dc618, 0x9dc8a0, 0x9dcae4, 0x9dcc84`; the RTTI names I printed were
garbage, so I did not name the classes). The 13 virtual call sites (`mov 0x9c(%edx),%eax; call *%eax`
with 9 pushes before it) are in: `0x6bda80`, `0x6bdc80` (x2), `0x6becd0`, `0x6bf150`, `0x6bf5f0`,
`0x6bfc40`, `0x6cc4a0`, `0x86bbb0` (x4: `0x86bc3d, 0x86bcd7, 0x86bdae, 0x86c095`), `0x86e030`.
(Two other `call *0x9c(...)` sites, `0x54369d` and `0x5c61f5`, have 3 pushes: a different class, not this.)
Runtime trace agrees: `0x74ef44/0x74ef90/0x74ef9c/0x74f064` hit hundreds of times (run1: 82/246/164/164).

**Key trick (UNVERIFIED in game, but only uses verified memory):** hook `0x74ef20`. In the hook:
1. call the original normally (main list -> `0xaf4e40`, count), remember the result;
2. if our overflow list is non-empty: temporarily write the overflow unit pointers into
   `player+0x160..` and set `player+0x254 = n_overflow` (n <= 60 per pass), call the original
   *again* with the same 9 arguments, copy its output to our own buffer `ovf_filtered[]`, then restore
   the real `selected[]` and count. The game's own filter logic is reused for the overflow
   units, no re-implementation. (Single-threaded UI/game thread assumed - UNVERIFIED but nothing
   here suggests a worker thread touches `selected[]`.)
3. return the main result untouched.

### 2.3 The command builders and the single send function

All commands end in **`0x80ef70`** (`__thiscall(cmd_obj, packet, size, player)`, `ret 0xc`): 58 builder
functions call it (scan: `0x808c00 ... 0x813390`). It calls dedupe `0x808b60`, queues through
`0x56b450` (type byte, size), stores the packet at `this+8` and **frees the previous one** with
`0x8ea484` (cdecl, `80efd8..80efde: push; call 0x8ea484; add $4,%esp`). Packets are allocated by the
builders with `calloc`-style `0x8ea479(1, size)` (cdecl, `add $8,%esp` after: `812f0f..812f19`).

Builders that take a unit list (found by pattern: id loop `mov 0x8(%eax),%eax` / `or $-1`;
`byte[0]` = command type, `byte[1]` = count; ids = `[unit+8]`, -1 for null). Type, header size K
(ids start at `+K`, packet size = `K + 4*count`), `ret N`:

| Builder | type | K | ret | Verified how |
|---|---|---|---|---|
| 0x809cb0 | 0x01 | 0x02 | 0x08 | lea 0x2(,%esi,4) |
| 0x809d50 | 0x12 | 0x03 | 0x0c | lea 0x3(,%esi,4) |
| 0x809280 | 0x14 | 0x08 | 0x0c | lea |
| 0x809340 | 0x17 | 0x08 | 0x10 | lea |
| 0x8096c0 | 0x13 | 0x08 | 0x0c | lea |
| 0x809bf0 | 0x16 | 0x0c | 0x10 | lea |
| 0x809e50 | 0x02 | 0x14 | 0x14 | lea (count byte store not matched: UNVERIFIED) |
| 0x808df0 | 0x21 | 0x54 | 0x14 | lea 0x54(,%esi,4) (formation move; matches executor `0x80a450`: ids at `cmd+0x54`) |
| 0x808f60 | 0x22 | 0x5c | 0x18 | lea |
| 0x809a30 | 0x15 | 0x54 | 0x14 | lea |
| 0x812150 | 0x6b | 0x0c | 0x10 | lea |
| 0x812210 | 0x66 | 0x18 | 0x1c | lea |
| 0x8123e0 | 0x69 | 0x14 | 0x1c | lea |
| 0x812cb0 | 0x6e | 0x08 | 0x0c | lea |
| **0x812ee0** | **0x78** | **0x14** | **0x14** | read in full: `(units**, short count, int target, float x, float y)` = the right-click move/target order |
| 0x813030 | 0x74 | 0x0c | 0x10 | lea |
| 0x813200 | 0x6d | 0x08 | 0x0c | lea |
| 0x8132b0 | 0x75 | 0x18 | 0x20 | lea |
| 0x813390 | 0x6f | 0x14 | 0x18 | lea |
| 0x8095b0 | 0x10 | ? | 0x10 | ids offset not found by pattern: UNVERIFIED |
| 0x8097d0 | 0x03 | ? | 0x14 | UNVERIFIED |
| 0x809900 | 0x00 | ? | 0x14 | UNVERIFIED |

What each type means (move, attack, garrison, stance, stop, delete, ...) I did **not** map; the table is
by packet layout only. Several of them are wrapped by `WorldPlayer` order methods in `0x73e660..0x73f340`
and `0x74d510..0x74df60` (builder callers listed by scan), which are what the UI calls.

Do builders take an explicit (ids, count) list we could call again, or read `player+0x160`? **All 22 take an
explicit unit-pointer array + count** (verified for 0x812ee0, 0x813390, 0x8132b0, 0x809cb0, 0x809d50,
0x808df0; others from the same pattern). They do not read the player's `selected[]` themselves. The
array comes from the getter's `0xaf4e40` in the 13 sites above; other callers (e.g. the
`0x7d8f60` call in `0x6c9dc0`, which passes `&selected[0]` + count) were not traced.

### 2.4 Two ways to repeat the order for the overflow units

* **Option A: wrap each of the 22 builders** (generic asm stub per `ret N`): call the original,
  then call it again with `(ovf_list, n, other args copied)`. 22 hooks + arg copying. Not recommended.
* **Option B (recommended): hook the send function `0x80ef70` once.** In the hook, after the original
  send, check: (1) packet type is in the table above; (2) the packet's ids at `+K` equal the ids of
  the list that the getter hook just produced for the main selection (memcmp of `n` ids, `n = packet[1]`);
  (3) our `ovf_filtered[]` for that same getter call is non-empty. If all hold, build clones: for each
  chunk of <= 60 overflow units (`ceil(n_ovf/60)` clones) `calloc` (0x8ea479) a packet of `K + 4*m`,
  `memcpy` the first `K` bytes, set `byte[1] = m`, write the ids `[unit+8]`, and call the *original*
  `0x80ef70(cmd_obj, clone, size, player)` for each (it queues and keeps/frees packets itself).
  Conditions (1)-(3) make the clone strictly "the same command, same targets, same filter, other units".
  A command whose ids do not match the getter output (e.g. build/train commands) is never cloned.

---

## 3. Verdict details

### 3.1 Hook list (minimal)

| # | Hook | Where | How |
|---|---|---|---|
| 1 | overflow add | `0x754b23` (9 bytes `cmp $0x3c,%eax; jge 0x754c15` - contains a relative jge, so do NOT use plain `wl_hook`; write a manual `jmp stub` + nops and re-create the jump back to `0x754b2c`, jge target `0x754c15`) | if count >= 60: if unit not already in list and list < MAX: store `unit+8` (id), `unit->0x3a |= 1`, return 1 via `0x754c0a`; else fall to `0x754c15` (0). Also handle `edi==0`. |
| 2 | clear | `unselect_all 0x755450` (plain `ret`, `ecx=player`; first bytes `57 89 cf 83 bf 54 02 00 00 00` stolen = 10 bytes, no relative jumps - bytes UNVERIFIED, check) | clear `0x3a` bit0 of every overflow unit that still exists, empty list; call original |
| 3 | single unselect | `unselect_unit_by_pointer 0x755540` (and by index `0x7554a0`, `ret 4`) | if the unit is in overflow: remove id, clear flag |
| 4 | prune | `0x755850` (validate `selected[]`, per frame: 12313 hits in trace; plain `ret`, ecx=player) | after original: drop ids whose unit is gone/dead/not ours. Validity check as the game: `[unit+0x4c] < 7 && [unit+0x24]==0` (0x755879/0x75587f) plus owner `[unit+0x10]==player`. Lookup by id: `0x733bf0` (`__thiscall(world,id) ret 4`, world = `[0xaf2c58+0xc8]`, used this way in `0x80e610`) - ids are looked up each use, so no dangling pointers. |
| 5 | getter | `0x74ef20` (`ret 0x24`) | the swap-and-recall trick of 2.2 |
| 6 | send | `0x80ef70` (`ret 0xc`) | clone chunks as in 2.4 option B |
| 7 (optional) | box > 60 | `0x87cee0`, `0x87cc60`, `0x87d110`, `0x86f880`, `0x887660`, `0x86ef4f` area, `0x887cda` area | relocate the 0xde0430 buffer (about 20 immediates), or *skip*: leave box capped at 60 and let the user press shift/ctrl+box again (then the extra units hit hook 1) |

So **6 hooks for double-click / group recall / hotkey / shift-add behaviour, 7 if one box must
exceed 60** (that one is the expensive one: ~7 functions, immediates only, but every user of the
buffer must change together or box select breaks). Optional UI: count label (`wl.counter`-style overlay)
because the selection panel `0x716360` draws only `selected[]` and `count`.

### 3.2 Risks

* **Multiplayer sync: low.** Commands carry unit ids (int32 `[unit+8]`), verified in `0x812ee0`
  (`mov 0x8(%eax),%eax ... or $0xffffffff`). Our clones are ordinary vanilla commands of the same
  type, each with <= 60 ids, so executors on all clients (even without the mod) run them
  in the same order of the lockstep queue; the receiving side never sees more than the vanilla
  maximum, so `last_commanded_units[9][60]` (command+0x12c, plan_agy section 2.4), `0xafb510[60]` /
  `0xafb50c` (`cmp $0x3c` in the executors, e.g. `0x80a584`) are untouched. This is the main advantage over
  enlarging a single command to 100+ ids (count is a **byte** at packet+1, so max 255 anyway; the
  executors above are unproven for >60).
* **Duplicate suppression (UNVERIFIED consequence):** `0x808b60` compares the new packet with the previous
  one stored at `this+8` (same size, same type byte, `now - t < 0x1f4` = 500 ms) and for types 0..3
  asks `0x80da00/0x80da80` whether it is a repeat. Chunk clones have the same type and often the same
  size as the original; for types 0x00-0x03 (builders `0x809900`, `0x809cb0`, `0x809e50`, `0x8097d0`) read
  `0x80da00/0x80da80` before shipping, or send clones with a different size (not possible) or skip clones
  for those types. Other types fall through to "not duplicate" (jump table only covers 0..3, `ja 0x808bdd`).
* **Formations:** each packet forms its own formation group; with 100 units two groups, same target,
  overlapping spots. Acceptable; spreads less nicely than one big formation.
* **UI shows only 60** in the panel (`0x716360` draws selected[]; count shown = 60). Needs our own label.
  Selection rings: flag `0x3a bit0` is set for overflow units, and `0x86ac08 testb $0x1,0x3a(%edx)`
  is in the view renderer, so rings probably draw (UNVERIFIED; `0x86a6f0` also iterates `selected[]`
  separately for some highlight).
* **Primary:** `0x754910` writes `player+0x15c = unit` after a successful select_unit, so the primary
  can become an overflow unit that is not in `selected[]`. Probably harmless (unselect_all resets it,
  `0x7554a0` compares it), UNVERIFIED; can be fixed by also hooking the write at `0x754a03` or by
  re-pointing primary to `selected[0]` in hook 4.
* **Commands that bypass the getter** (anything iterating `selected[]` itself: hotkeys `0x6be710`,
  `0x7d8f60` user, delete key) will not include overflow units (the clone check fails -> ignored,
  safe). Which hotkeys these are was not traced.
* **Death/conversion:** we store ids and re-resolve; no stale pointers. A converted unit has
  `[unit+0x10] != player`, dropped by hook 4. The `0x3a` flag stays set on a dead unit until freed;
  harmless (game does the same for `selected[]`).
* Wrong memcmp/chunking bug => one order not duplicated; no memory writes into game structures other than
  the temporary swap in hook 5 (restore in the same call, guard with try-once).
* The temporary `selected[]` swap in hook 5 is the only place we write the player object; if an exception
  escaped between swap and restore, selection would be corrupted: keep it straight-line.

### 3.3 Comparison with relocation

| | Overflow list | Relocate selected[] to 100/150 |
|---|---|---|
| Functions to change | 6 core (+7 for box) | 32-34 TRUE + 8 runtime-missing + unknown more (10274 instructions with disp 0x160..0x24f; scan known incomplete) + buffers `0xaf4e40`, `0xafb510`, `0xde0430`, command cache `+0x12c` + panel `0x716360` + executors |
| Failure mode | an order skips overflow units | wrong orders / memory corruption / crash |
| Multiplayer | vanilla-legal packets, works with unmodded peers | needs larger packets and executor changes; probably desync with unmodded peers |
| Cost of a miss | cosmetic | catastrophic |
| UI | panel still 60 (add label) | panel needs redraw logic for >60 |
| Quality | second formation group, double dedupe check risk | one group, feels native |

## 4. Verdict

Feasible and much safer than relocation. Do it in this order:
1. Hooks 1-4 (add/clear/unselect/prune) with a log line and an on-screen count; test double-click
   select-all and ctrl-group recall with 61+ units (box select stays capped at 60 at first).
2. Hook 5 + 6 (getter swap + send clone) for move/attack/gather/garrison; first read `0x80da00/0x80da80`
   and the `0x808b60` dedupe, and verify in game that the extra units move.
3. Only then consider the box buffer relocation (hook 7) and a UI count label.

Unverified items to check before coding: stolen bytes of `0x755450`/`0x755540`/`0x755850`/`0x74ef20`/`0x80ef70`
prologues (read with xxd, they were not byte-dumped here), the ids offset of builders `0x8095b0`,
`0x8097d0`, `0x809900`, the meaning of each packet type, and the single-thread assumption for
the swap trick. Expected effort: about 400 lines of C, 1-2 test rounds in game. Relocation:
many days with unbounded risk.
