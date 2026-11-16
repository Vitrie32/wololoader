# Overflow selection, stage 1 (dry run)

Code: `wololoader/src/overflow.c` (`wl_install_overflow()`, called from `main.c` after `wl_install_popcap`,
so not in Vanilla Mode). Design: `overflow_design.md`. Stage 1 sends nothing, writes no game memory, changes
no behaviour. It only (a) tracks units the game refuses at 60, (b) logs what stage 2 would clone.
Everything below was read from `~/.cache/wl-test/exe.s`; original bytes were dumped from the exe file
(PE section map) and are checked again at runtime before each hook (mismatch = log + skip).

## Hooks

| Address | Function | Convention | Orig bytes (stolen) | Evidence |
|---|---|---|---|---|
| 0x754a20 | select_unit | `__thiscall(player, unit, flag)`, `ret 8`, eax 1/0 | `55 8b ec 56 57` (5) | prologue 0x754a20..0x754a24, `mov 8(%ebp),%edi` at 0x754a25; returns 0 via 0x754c15 (null unit 0x754a2c, class/visibility 0x754a8a..a8, **limit 0x754b26**), 1 via 0x754c0a / 0x754b13. No jump lands in the stolen bytes. |
| 0x755450 | unselect_all | `__thiscall(player)`, plain `ret` (0x75549f) | `57 8b f9 83 bf 54 02 00 00 00` (10) | `push edi; mov edi,ecx; cmpl $0,0x254(%edi)`; no rel jumps; internal jumps target 0x75549e/0x755493/0x755471 only. (Design guessed `89 cf`; real bytes are `8b f9`.) |
| 0x755540 | unselect by pointer | `__thiscall(player, unit)`, `ret 4` | `55 8b ec 53 8b d9 57` (7) | loop over selected[] compares `0x8(%ebp)` at 0x75556a; calls 0x7554a0; `ret 4` at 0x75557d/0x75558c. Jump targets 0x755560..0x755589 are outside the stolen range. |
| 0x80ef70 | command send | `__thiscall(cmd, packet, size, player_id)`, `ret 0xc`, al = not-duplicate | `55 8b ec 51 53` (5) | args at `0x8(%ebp)` (packet, edi), `0xc(%ebp)` (size, ebx), `0x10(%ebp)` (player); `ret $0xc` at 0x80eff8; caller 0x812fb1 pushes packet, size, player-id. |

Not hooked: `0x7554a0` (unselect by index; indexes selected[] only, overflow units are never there, so
nothing to remove; its `ret 4` + prologue `55 8b ec 0f bf 55 08` were verified anyway), `0x755850` (per-frame
prune, 5 bytes `53 56 57 33 f6`, hookable later), `0x74ef20` (getter, stage 2).
Also checked at install: `0x733bf0` bytes `55 8b ec 8b 45` (object-by-id, called, not hooked).

### Catch rule (select_unit)
Entry hook: call the original; if it returned 0 **and** player+0x254 == 60 **and** the unit is own
(`[unit+0x10] == player`, same compare as 0x754ae6), not selected (`[unit+0x3a]` bit0 clear, 0x754ab1),
alive by the game's own test (`[unit+0x4c] < 7 && [unit+0x24] == 0`, 0x755879..0x755883) and not already
listed, it is stored as `{unit pointer, id = [unit+8]}` (id offset verified at 0x812f37). Max 40.
Entry hook gives the same information as a stub at 0x754b23 except that it cannot tell the limit refusal
from the other return-0 paths (0x754a8a..0x754aa8: visibility/class checks made for foreign units); the
own+alive+count==60 filter makes a false catch unlikely but not impossible (UNVERIFIED, the log prints the
unit type so odd catches are visible). The unit's selected flag is NOT set in stage 1 (no ring/visual change).

### Validation (how stale units are avoided)
Each stored unit is re-resolved with the game's `0x733bf0(world, id)` (`world = [[0xaf2c58]+0xc8]`, as in
0x80e610) and kept only if the returned pointer equals the stored one, owner is still the player, the game's
alive test passes and its selected flag is still clear (a unit that entered selected[] normally is dropped
from the overflow list). The unit memory is only dereferenced after that pointer comparison. Pruning runs on
every catch and every logged order.

### Clearing
`unselect_all` hook drops the list (if the player is the list's player). Pointer-unselect removes that unit.
Index-unselect is covered lazily by validation.

### Send hook (log only)
If the list is non-empty and the packet type is one of the unit-list orders (types 0x01 0x12 0x14 0x17 0x13
0x16 0x21 0x22 0x15 0x6b 0x66 0x69 0x6e 0x78 0x74 0x6d 0x75 0x6f; K per type in `g_types`; 0x02/0x10/0x03/0x00
excluded, layout unverified) and `size == K + 4*packet[1]`, every id at `packet+K` is looked up in the
player's selected[] ids. All match = "order for the current selection". One line per such order:

    overflow: order type=0x78 units_in_packet=60 selected=60 overflow=23 -> would send 1 extra packet(s) (DRY RUN, nothing sent)

No match = one line "not a selection order, ignored". Packet is only read, before `o_send` is called.
Other log lines: first caught unit then every 10th (`caught unit id=.. type=.. (overflow list N, would show
60+N selected)` = the count display, log only), `selection cleared, dropping N`, `list full`, `stage 1 dry run, N/4 hooks installed`.

## In-game test
1. Start the game (not Vanilla Mode), start a sandbox/skirmish with 80+ units of one kind on screen
   (cheat or scenario editor; e.g. 100 militia/villagers).
2. Select 60 units (box). Then **double-click a unit of that type** (select all of type on screen,
   0x86f3f0 loops and ignores refusals) or shift-click further units one by one. Box select is capped
   inside 0x87cee0 and stops at the first refused unit, so a second shift-box catches at most one unit.
   Control-group recall (key 1..9 of a group holding >60) also loops (0x7547f0) but groups are built from
   selected[] so they hold at most 60.
3. Right-click on the ground (move), then also attack/garrison once.
4. Open `<game>\wololoader\wololoader.log`, search `overflow:`. Expect: `stage 1 dry run, 4/4 hooks installed`
   at start; `caught unit ...` lines; `order type=0x78 units_in_packet=60 selected=60 overflow=N -> would send ...`
   after the right-click; after clicking empty ground / new selection: `selection cleared, dropping N`.
5. The game must behave exactly as without the DLL change (still 60 selected, only 60 move).
Report any `unexpected bytes` line, or catches with an odd `type=`.

## Risks
- Entry hook cannot separate limit-refusals from the other refuse paths (see above).
- Single game thread assumed (no locking); `selected[]` read directly in `in_selection`.
- Box select yields at most one overflow unit per box (loop breaks); double-click is the real test.
- `g_player` is the last player that hit the limit; a different player pointer resets the list.
- Hook on 0x80ef70 runs on every command but returns immediately while the list is empty.
- `0x733bf0` / `[0xaf2c58]+0xc8` is used as the game does in 0x80e610; never called before the game world exists
  (world() returns NULL -> entries invalid).

---

# Stage 1b (still DRY RUN: no packet is modified or resent)

Code: `wololoader/src/overflow.c` (now 5 hooks: the 4 above + a CALL patch at 0x87cd2d). All addresses read in
`~/.cache/wl-test/exe.s`.

## Test result being explained
`caught unit id=2388` once; `order type=0x66 ids=1, only 0 in selected[]` x5; no line for the right-click move.

## (b) Why the move never matched: wrong packet types / layout, not another send path
Every order builder ends in `0x80ef70` (builders 0x809900, 0x8097d0, 0x8095b0, 0x809e50 all `call 0x80ef70`
at 0x80990a / 0x8098db / 0x8096a4 / 0x809f0e). The right-click order is built by the types that stage 1
**excluded** as "layout unverified": 0x00 (0x809900), 0x03 (0x8097d0), 0x10 (0x8095b0). Their layout:

| type | builder | count byte | ids at | size | evidence |
|---|---|---|---|---|---|
| 0x00 | 0x809900 | packet[8] | +0x14 | 0x14 + 4n | `movb $0,(%esi)` 0x80999e, `mov %bl,0x8(%esi)` 0x8099e0, ids loop `lea 0x14(%esi)` 0x80995a |
| 0x03 | 0x8097d0 | packet[8] | +0x14 | 0x14 + 4n | `movb $3,(%esi)` 0x80986a, `mov %al,0x8(%esi)` 0x8098ae, `lea 0x14(%esi)` 0x80982d |
| 0x10 | 0x8095b0 | packet[2] | +0x08 | 0x08 + 4n | `movb $0x10,(%esi)` 0x809635, `mov %bl,0x2(%esi)` 0x80967c, `lea 0x8(%esi)` 0x80960a |
| 0x02 | 0x809e50 | packet[8] | +0x14 | 0x14 + 4n | `mov %bl,0x8(%edx)` 0x809ee0 (plain, no cache) |

(My stage 1 assumed the count is always packet[1]. For these types packet[1] is a different value from
`0x5836e0`, and the count would not have matched `size`, so they were skipped anyway.)

**The size formula has a second form (the real surprise).** These three builders first call `0x80ebc0(units,
count, &countbyte)`, which compares the unit list with the one of the previous order stored in the command
object (`cmd+0x28` short count, `cmd+0x2c` flag, `cmd+0x30..` ids; loop 0x80ec14..0x80ec35). If equal it
**writes 0xff into the count byte and returns 0**: the packet is then only `K` bytes (0x14 / 8) with **no
ids** and the executor reuses the previous list. Otherwise it caches the new list (0x80ec4d..0x80ec95) and
returns 4*count. So the 2nd, 3rd... right-click on the same selection carries no unit ids at all.
`log_order` now handles both forms (cached form: ids read from `cmd+0x28/0x30`, which the hook receives as
`this`). The cache holds at most 60 ids (0x30..0x120); our clones must never go through the builders, only
through `0x80ef70` directly.

**Consequence for stage 2:** the clone for the overflow units must be a normal, explicit-ids packet (count
byte = m <= 60, size K + 4m) even when the original was the 0xff form, AND the cached form means the
executor on every client reuses "last list" state: the clone, sent after the original, replaces that state
with the overflow list, so the next original order (0xff again) would then act on the overflow units.
Open question for stage 2: after sending a clone, either resend the original's explicit list too or avoid the
executor cache (read `0x80a450`-like executor for types 0/3 before deciding). Not done here.

## Diagnostics (`#define OVERFLOW_DIAG 1`)
While the overflow list is non-empty, every packet through 0x80ef70 gives (max 20 per second):

    overflow: diag packet type=0x00 size=20 bytes[00 01 ... ] selected_ids_in_packet=N | cmd cache count=C flag=F in_selection=M | overflow=K

`selected_ids_in_packet` counts selected[] ids found at any byte offset (so a layout we do not know still
shows up); `cmd cache` is the previous-list cache of the command object (`this` of 0x80ef70) and
`in_selection` how many of those cached ids are in selected[] (60 expected after an order on 60 units).
Set the define to 0 to remove it.

## (a) Double-click does NOT stop at the first refusal; the old log could not show how many were caught
`0x86f3f0` (double-click select-all-of-type) re-read in full: the loop 0x86f4d5..0x86f538 calls
`select_unit` at 0x86f52c and continues (`inc %edi; cmp -0x2c(%ebp),%edi; jl 0x86f4d5`) regardless of the
result; there is no break. Candidates must pass the type test (0x86f4e2..0x86f50f) and
`0x8781b0(view, id, 0)` (0x86f513..0x86f521), which asks the view's on-screen object list (`view+0x38c`,
`0x877fb0`) - only units **currently on screen** are candidates. The stage 1 log printed only the 1st and
every 10th catch, so "1 caught" meant 1..9 caught; also the first call (0x86f48b) is the clicked unit itself.
Fix (log only): the first 5 catches are logged, plus a **burst summary** line when a burst ends (next select
call >300 ms later, next order, next clear):

    overflow: select burst: refused@60=R caught=C already_listed=D not_own_or_dead=O list_full=F (list now N)

If the user's test had few other villagers on screen, `caught=1` is the correct answer.

## Box select (0x87cc60)
Verified in full: candidates come from `0x87cee0` into the static buffer `0xde0430` (max 60 records, see
design 1.3), then the loop 0x87ccb2..0x87cd49 calls select_unit at **0x87cd2d** and
`87cd32 test %eax,%eax; 87cd34 je 0x87cd4f` leaves the loop at the first refusal. So (1) a box can never
produce more than 60 candidates, a plain box (selection cleared first, 0x87cd1b) never overflows; (2) a
shift-box with a full selection can; there the loop breaks at the first refusal.
**Change:** the CALL at 0x87cd2d (bytes `e8 ee 7c ed ff`, target 0x754a20, checked at install) is replaced
with `box_select_call`, same convention (`__fastcall(player, edx, unit, flag)`, ret 8). It calls the normal
select path and returns 1 instead of 0 when the refused unit went into the overflow list, so the loop goes
on. This is the only behaviour change of stage 1b (box loop continues; also sets the loop's "something
selected" byte at -2(%ebp), so the fall-through at 0x87cd4f/0x87cd53 that runs when nothing was selected is
skipped). Packets are untouched. Revert = remove the `wl_patch_call` line. Verified 0x86f48b/0x86f52c/
0x86f546 (double-click) and 0x87cebb need no change.

## Next in-game test
1. 100+ villagers, 60 selected by box; double-click a villager with many others on screen. Expect the
   `select burst` line with `refused@60` large and `caught` = number of villagers on screen beyond 60
   (max 40).
2. Right-click ground (move) 3 times in a row, then attack/garrison once. Expect `diag packet` lines
   (type 0x00 or 0x03 first with size 0x14+4*60=260, the next ones size 20 with byte[8]=ff) and
   `order type=0x00 ... would send` lines. Send the `overflow:` lines.
3. Shift+box a crowd of villagers with 60 selected: expect `select burst` with caught up to 40 and no
   change except that the box now continues. Selection must still show 60 and move 60.

---

# Stage 2: the clone is sent (code: `wololoader/src/overflow.c`, `send_clone`)

All addresses below read in `~/.cache/wl-test/exe.s` (objdump), 2026-11-15. Code comments carry the same evidence.

## Behaviour
In the `0x80ef70` hook: the original packet is sent first (trampoline). If it returned non-zero (not dropped as a
duplicate), the overflow list is non-empty and the packet is a selection order (all its ids are in `selected[]`, explicit
form or the 0xff short form read from `cmd+0x30..`), ONE more packet of the same type is built: header (K bytes) copied
from the original, count byte = m, ids = overflow units (validated by `prune()`: same object, own, alive, not selected;
max 40 = `OVF_MAX`), sent through the trampoline (no recursion; `busy` guard too). Log line per clone:
`overflow: sent clone type=0x03 units=25 (order had 60 ids, short form, selected=60, overflow=25)`.
Filter: if the order's id count equals the selection count nothing was filtered by the game's getter, so every overflow
unit is included; otherwise only overflow units whose unit type (`[[unit+0xc]+0x10]`) occurs among the order's units.

## Live vs dry-run types
Live: **0x00, 0x02, 0x03, 0x10** only. Dispatcher `0x809fe0` (jump table `0x80a104`): 0x00 -> `0x80c650`,
0x02 -> `0x80d6d0`, 0x03 -> `0x80bdd0`, 0x10 -> `0x80a064` -> `0x80ba00`. Each executor calls `0x80dad0(player=pkt[1],
&count, pkt, K)` (call sites 0x80c66f, 0x80d6f9, 0x80bdf1, 0x80ba30) with K = 0x14/0x14/0x14/0x08 and count at
pkt+8 / local copy of pkt[8] / pkt+8 / pkt+2; the ids are read at `pkt+K` (0x80dbe8 `add %ebx,%edi`). That is exactly
the (K, count offset) of `g_types`, so building the clone is verified. Every other type in `g_types` keeps the dry-run
log (`clone NOT sent (type not verified, dry run)`): executor/semantics not read. Per-type packet layout
(header K, count byte, id offset) is therefore the table row; max per packet 40 (<= 60 = executor cache size and the
vanilla maximum).

## 1. Command cache / "same list as last order"
* Sender cache lives in the command object (`this` of 0x80ef70): `cmd+0x28` short count, `cmd+0x2c` flag, `cmd+0x30..`
  ids. 0x80ebc0 writes it while the builder runs (0x80ec4d..0x80ec95), before the send. The builder sets `cmd+0x2c=1`
  only after `0x80ef70` returned non-zero (0x8096ad, 0x8098e4, 0x809a13). Short form is chosen only if `cmd+0x28>0`,
  flag != 0 and count + ids equal (0x80ebfc..0x80ec39).
* **The 0xff form is resolved at EXECUTION time from a per-player cache on every machine**: 0x80dad0 keeps
  `this+0x120+player` (count) and `this+0x12c+player*0xf0` (60 ids); explicit packet -> store (0x80dbe8..0x80dc23),
  0xff -> load (0x80db51..0x80dbe3). So an explicit clone changes what a later 0xff means for this player on ALL machines.
* Chosen design: the clone is sent AFTER the original (a 0xff original needs the cache left by the previous order),
  then `cmd+0x28 = 0` (sender cache invalid). The next order of the player is then built explicit by 0x80ebc0 (count
  <= 0 -> "different list"), re-caching the real selection on all executors. Clearing the count (not the flag) is
  needed because the builder sets the flag to 1 after we return. Since the next order is explicit it also gets its own
  clone, so with a non-empty overflow list every order is explicit: original + clone, all machines consistent, unmodded
  peers only see vanilla explicit packets with <= 60 ids. (Cost: one explicit 260-byte packet per order instead of 20.)

## 2. Duplicate filter 0x808b60
Compares with the previous packet at `cmd+8` (size `cmd+0xc`, tick `cmd+0x10`): dup only if same size, same type byte,
< 0x1f4 ms and, for types 0/2/3 (table 0x808be4 -> 0x80da00; type 1 -> 0x80da80; others never), 0x80da00 says
equal: pkt[8], x/y as int16, target pkt[4] and all ids. Clone vs original: size differs (K+4m vs K or K+4n) or ids
differ -> never dropped; the clone result is still logged (`[clone dropped by the duplicate filter!]`). If the
original itself was dropped (return 0) no clone is sent. Side effect: the clone becomes the "previous packet", so a
repeated identical order within 500 ms is no longer suppressed (executed twice, idempotent). Memory: 0x80ef70 frees the
previous packet on the next send (0x8ea484) -> the clone must come from `0x8ea479` (calloc(1,size) cdecl, as in the
builders) and `pkt` must not be touched after the clone send (code complies: everything is read/copied before).

## Unverified / risks
* Not run in game yet. Whether the queue (0x57e300) copies the packet is inferred (vanilla frees the previous packet
  at every send, also for back-to-back packets), not read.
* Overflow units get the order even if the game would have filtered them for a mixed-type overflow when the order
  covered the whole selection; executors validate per unit (not read).
* Type 0x02 executor also writes the per-player cache (0x80d6d0 -> 0x80dad0) though its builder does not touch the
  sender cache; vanilla quirk, our invalidation after each clone covers it.
* Multiplayer: clones are ordinary lockstep packets (same send path, ids in the packet), applied in the same order on
  all machines; needs a 2-player test with an unmodded peer. Single player AI-script packets use player index != ours
  and are ignored (ids not in selected[]).

## Test plan
1. Build `make`, `OVERFLOW_DIAG` is 0. 100 villagers: box 60, shift+double-click others -> overflow 25..40.
2. Right-click ground once: log `sent clone type=0x03 units=N`; ALL villagers (also the overflow ones) walk.
3. Right-click again (was 0xff short form): log should now show an explicit original + clone each time; again all move.
4. Click a different group (select 10 villagers, order), then re-select 60+ overflow: orders must not move the old overflow units.
5. Select 60 (no overflow) after a clone: right-click must move exactly the 60 (no stray overflow-list order).
6. Attack/garrison/gather with mixed selection; check `no overflow unit passes the order filter` lines.
7. Two-player game vs unmodded peer: no desync warning after ~10 orders.

---

# Stage 3 (250): 190 overflow units, clones in chunks of 60

Code: `wololoader/src/overflow.c`. Motivation: in-game double-click burst `refused@60=142 caught=40 list_full=102`.

## Changes
* `OVF_MAX` 40 -> 190 (60 + 190 = 250). Dedup is a linear pointer scan (n <= 190, ~36k compares per big burst).
  `prune()` (one `0x733bf0` lookup per entry) now runs once per select burst (first refusal after >300 ms / order / clear)
  and once per order, not once per catch, so no O(n^2) get_obj work per frame.
* `send_clone`: `ceil(m/60)` packets, each with <= 60 ids, built from a copy of the original header (the original `pkt`
  is freed by the first clone send, so the header is copied before it), all via the send trampoline. After the last chunk
  `cmd+0x28 = 0` as in stage 2. One log line per order:
  `overflow: sent N clone(s) type=0x03 units=142 (order had 60 ids, ...)`.
* `int wl_overflow_count(void)` (declared in `wololoader.h`): valid overflow units, pruned at most every 100 ms.

## Evidence (objdump, `~/.cache/wl-test/exe.s`)
* 60 is the per-packet maximum for 0x00/0x02/0x03/0x10: all executors call `0x80dad0`, which stores an explicit list in
  a per-player slot of `0xf0` bytes (`imul $0xf0` at 0x80dbd4 / 0x80dbfc, base `this+0x12c`) with a byte count at
  `this+0x120+player` and an unclamped copy loop (0x80dc10..0x80dc21). 60 ids exactly fill the slot; 61 would overwrite
  the next player's slot. The count is a byte, so 60 is fine.
* Dup filter `0x808b60 -> 0x80da00` (read in full): compares count byte, x, y (as int16), target pkt[4], then every id
  (0x80da51..0x80da5e, returns 0 at the first differing id). Chunks have disjoint ids, so consecutive clones of the same
  type and size are never dropped; chunk 1 vs the original differs too. Type 0x10 (> 3) is never compared.
* Executor cache after the last chunk holds the LAST chunk as the player's "last list". Harmless: the sender cache reset
  (`cmd+0x28 = 0`, 0x80ec00 `jle 0x80ec4d`) forces the next order to be explicit, which re-caches the real selection on
  every machine; a 0xff is never sent while the executor cache holds an overflow chunk.

## Risks / unverified
* Not run in game. 190 units = up to 4 extra 260-byte packets per order (explicit form every time), more traffic in MP.
* Box select can still add at most 60 per shift-box (candidate buffer 0xde0430); double-click is limited by on-screen units.
* The UI/count display of 250 is not implemented (`wl_overflow_count` is only the data source).

# Selection count label

Code: `wololoader/src/selpanel.c` (`wl_install_selpanel`), data from `wl_overflow_count()` / `wl_overflow_owns(player)`.
Design and disassembly evidence: `re/selection/panel_count_ui.md`.
* `wl_patch_call(0x713fef, draw_group_hook, 5)` (only call of draw_group 0x716360, bytes `e8 6c 23 00 00` checked first; the
  four text helpers' `55 8b ec` prologues are checked too, otherwise the label is disabled and logged).
* After the original draw_group returns, if `[panel+0xbf0]` is the overflow list's player, the panel is visible (+0x1c),
  has a draw area (+0xc) and extra > 0, it draws only `+M` (gold, outline mode 3) centered (flags 0x25 = DT_CENTER |
  DT_VCENTER | DT_SINGLELINE) in the next empty icon slot, i.e. slot index = count = `player+0x254`, drawn over the last
  icon if the grid is full (slot/cols >= 3).
* Grid geometry (from draw_group 0x716360, all addresses in the code comments): cols0 = (w-8)/40; if count > 3*cols0:
  c = (int)(count/3.0f + 0.5f), cols = 3c >= count ? c : c+1, spacing = (cols0-1)*41.0f/(cols-1); else spacing 41.0f.
  Icon k: row k/cols, col k%cols; x = panel+0xb8 + (int)(col*spacing + 8.0f), y = panel+0xbc + row*0x2a + 4; size 0x28 x 0x28
  (constants read from .rdata: 0x9d9708 = 41.0f, 0x9b0234 = 3.0f, 0x9afd68 = 0.5f, 0x9b483c = 8.0f; 0x7197c0 adds the panel origin).
* Slot rect checked against the user log `rect 325 928 1204 150 count 60 cols 29 slot 60 -> 415,1016`: cols0 = (1204-8)/40 = 29,
  60 icons = 29 + 29 + 2, slot 60 = row 2 col 2: x = 325 + (int)(2*41+8) = 415, y = 928 + 2*42 + 4 = 1016; icon 59 (col 1) ends at 414,
  so the slot is correct (rows are 29 wide, not 30). The old text was just small (Georgia 11 B, ~28 px wide) and had no box.
* Box and text are drawn with GDI on the panel's DC: `0x49e7b0(ctx)` (`push esi; mov ecx,esi; call 0x49e6c0; mov 0xc(%esi),%eax; pop esi; ret`,
  plain ret) returns the hdc that `0x4a07a0` passes to `DrawTextW`; `0x49e6c0` acquires it (`0x44eb40`), installs the clip region
  (`SelectClipRgn`) and SetBkMode TRANSPARENT; `0x49e4b0` -> `0x49e8f0` releases it. Unit icon frames are SLP frames 0x24/0x25 of the
  shape `[panel+0x144]` drawn by `0x7197c0` -> `0x45a560` (thiscall shape, ret not used by us), no plain fill-rect function on that path,
  so the 40x40 box = `FillRect` light grey (200,200,190) 1 px frame + black fill.
* Font: the game table (parsed from the static list 0x410dc1.. and key-value-strings): biggest bold readable = Georgia 11 B (0x21);
  larger ones are Lucida Blackletter 17/21/28/38 B (decorative). So the number uses `CreateFontA("Georgia", bold)` at the largest
  height <= 26 px whose text width <= 34 px (`GetTextExtentPoint32A`), outline black x8 offsets then gold (255,215,0), DT_CENTER|DT_VCENTER|DT_SINGLELINE.
* Logs `selpanel: rect x y w h count N cols C spacing S slot I -> box x,y,40,40 font Hpx dc ok` whenever any of these change: check placement from it.
* Unverified: on-screen result in game (never run), exact font index mapping, behaviour when count > 60 (game skips icons).

# Stage 3b: clones not executed (user test: 60 selected + 144 overflow, only the 60 walk)

Status: **root cause NOT found by static reading**. Every hypothesis in the task was checked against
`~/.cache/wl-test/exe.s`; all were refuted. Stage 3b adds run-time diagnostics (below) that will show where the
clones die. No behaviour change otherwise.

## Pipeline, fully traced (all addresses read in the disassembly)
* `0x80ef70` (send): `0x80f000` flush of batched type-0x0a packets; dup filter `0x808b60`; if not dup,
  `0x57e300(net, pkt, size, 0, player, 0)` (0x80efa9..0x80efaf); then frees the PREVIOUS packet at `cmd+8`
  (0x80efd1..0x80efe1, `0x8ea484`) and stores the new one. Returns `!dup`.
* `0x57e300` **copies** the packet: `memcpy` (0x8d6a40, at 0x57e4da/0x57e4f9) into a message vector, tags it
  tick = `net+0xac0`+2, sequence `net+0xac4`, then `incl 0xac4; incl 0xac8` (0x57e448/0x57e44e) and broadcasts via
  transport `0x5ba840`, whose self-delivery (`0x5bc5a0` -> `call *0x44(%esi)` = 0x5bb1f0) runs synchronously and
  reaches the registered handler of message 0x3e = `0x5849a0` (registration at 0x57beb5) -> `0x57d4c0` ->
  `0x5ab180`: ring buffer of 2000 entries of 0x24 bytes, payload copied again (`0x5ab790`, calloc+memcpy).
  => pointer lifetime / free of the previous packet is NOT the problem (two copies are made before the free).
* Turn execution: `0x80d9d0` loops `while ((p = 0x583760(net)) != 0) proc->vtable[3](p)` (`call *0xc(%edx)`, vtable
  0x9e3b70 slot 3 = `0x813460`), **no per-turn command limit**. `0x583760` -> `0x5ab5d0(queue, turn)` picks every
  unconsumed entry with tick == turn, lowest key (player+1) then lowest sequence number, marks it consumed. Several
  commands of one player in one turn are normal. The turn-done message (type 0x4e, handler 0x5851a0 ->
  0x5975c0 keeps the MAX count per tick) is re-sent when `net+0xac8` changes (0x588540), the readiness test
  0x583d10 compares the announced count with the queued entries (0x5ab8b0); clones are counted by the same
  counters, so they cannot make the counts disagree.
* `0x813460` logs and dispatches by type (types < 4 fall to `0x809fe0`); jump table 0x80a104 read from the exe:
  type 0 -> 0x80c650, 2 -> 0x80d6d0, 3 -> 0x80a01d -> `0x80bdd0`, 0x10 -> 0x80a064.
* Executor `0x80bdd0` (type 3): `0x80dad0` (cache/explicit ids, no selection test, `ret 0x10`), `0x80df80` (target),
  `0x80e610` (clears timers), `0x7d61e0(list 0xaf7ae0, ids, count, target, x, y, 1)`, then per-unit orders
  (`call *0xac(%eax)` on the unit, 0x80c3ae / 0x80c562). `0x7d61e0` resets the list first (`0x7d6fa0`), caps
  `min(count,60)` (0x7d6259..0x7d626e), resolves every id with `0x733bf0` (plain array lookup by id), puts villagers
  (class `[[unit+0xc]+0x16]` == 4 and a few others) into the flat list `0x7d4af0` (cap 0x3c). I found **no read of
  selected[], of the selected flag `[unit+0x3a]`, or of the owner's selection** anywhere in 0x80dad0, 0x80bdd0,
  0x7d61e0, 0x80e610, 0x7db770/0x7db790, 0x7d7f50 (other `[unit+0x3a]` readers are in render/kill code
  0x8b14cb.., 0x8b1aef.., not on this path).
* Dup filter `0x808b60 -> 0x80da00`: re-read; chunk vs original and chunk vs chunk differ in the first id, size or
  count, so none is dropped; the log counts `sent` only when `0x80ef70` returned non-zero, and it says 3.
* Our code: header copy, count byte, ids at +0x14, `calloc(1,size)` (builders do the same), trampoline call with
  the original's `player`: re-read, no defect found. Packet bytes of a real order (overflow_test1.txt):
  `03 01 00 00 ff ff ff ff 3c 00 00 00 <x> <y> ids...` = what the clone reproduces.

## Consequence
Statically the clones are queued, announced, popped and executed exactly like the original. So either (a) something
at run time differs that the code does not show, or (b) the executor does run and the units are ordered but do not
walk. The task's alternative ideas (swap selected[], later frames) are NOT justified by any evidence found: the
executors do not look at the selection, and nothing depends on the frame. They were not implemented.

## Diagnostics added (`OVERFLOW_EXEC_DIAG 1` in overflow.c, 6th hook = executor 0x813460, 8 stolen bytes
`55 8b ec a1 b8 ef ae 00`, checked at install)
After each order with clones, for 5 s:
* `overflow: net manager turn=T seq A -> B, commands this turn C -> D (expected +N ...)`: proves that the clones
  passed 0x57e300 (B-A and D-C must equal the number of clones).
* `overflow: EXEC type=0x03 turn=T player=1 count=60 ids_resolved=60 overflow_ids=60 ai_target_changed=60 first_id=..`
  one line per executed command: reaches the executor? ids resolve? are they overflow units? did the executor change
  `[[unit+0x78]+0x3c/0x40]` (the AI target written by 0x4e1b20) for them?
* `overflow: MOVE CHECK 2.5 s after the order: selected moved a/60, overflow moved b/144`: numeric result, replaces
  screenshots.
Reading the result: no EXEC line for the clones -> lost in the queue (look at the net manager line); EXEC lines with
`ai_target_changed=0` -> executor skipped the units (then the unit-state/formation path needs reading: start at
0x80c200 / 0x80c493 gating `cmpb $0x1e,0x4(type)` and `comiss 0xe4(type)`); `ai_target_changed=60` but `moved 0` ->
orders applied and cancelled afterwards.

## Risks
Diagnostic hook runs for every command (cheap early exit unless an order is watched). Not run in game.

# Stage 4: plain box select up to 250 (code: `install_box_buffer` in `wololoader/src/overflow.c`)

Problem: a plain box over 200 villagers selected 60. Cause: the box collector `0x87cee0` stops at 60 records in a static
buffer; select_unit never sees candidate 61+. (The clear-first behaviour of a plain box, `0x87cd1b call 0x755450`, also
drops our overflow list through the unselect_all hook; that is correct, the box then re-adds everything.)
Fix: move the buffer to our own array `g_boxbuf` (250 records * 12 + 16 bytes) and raise the cap of the collector only.
All addresses below read in `~/.cache/wl-test/exe.s` (objdump) and in the exe bytes, 2026-11-15.

## Buffer layout
Static in `.data/.bss` at `0xde0430`, record = 12 bytes `{int id; word a; word b; word x; word y}`
(`0x87d060 mov %esi,-4(%edi)` id; `0x87d064 movl $0x10001,(%edi)` a,b; `0x87d06e/0x87d076` x at +8, y at +10).
Records 0..59 occupy `0xde0430..0xde0700`. `0xde0700/04/08/0c` are four UNRELATED globals (a begin/end pair written
together at 0x873c6e, 0x87b99b, ...; `0xde0708=1, 0xde070c=0xddfbb0` at 0x8778e3); they are never an index base of the buffer.
`0xde0704` in the two compares below is `&buf[60].flags` (the pointer variable walks `buf+4`), i.e. the buffer end + 4.

## Completeness proof
1. Scanned the **whole exe file** (all sections, unaligned, every byte offset) for any dword in `0xde0430..0xde071f`:
   37 hits, all in `.text`, none in `.rdata/.data/.reloc` (so no pointer tables / vtable-like data reference it).
   18 of them are the four unrelated globals above (`0x83b615 0x83b61c 0x873c6f 0x873c7a 0x87438f 0x874399 0x8778e5
   0x8778ef 0x87b99c 0x87b9aa 0x87ba81 0x87ba8c 0x87df77 0x87df82 0x87f5fc 0x87f617 0x87f61d 0x87f623`, each read in
   context: loads/stores of single dwords, `sub` of two of them; none indexes). The other **19** are the buffer (plus the tied immediate `mov $0x3c` at 0x87d0d8 = 20 patched operands).
2. `grep` of the disassembly for every `0xde04xx..0xde07xx` immediate gives the same set (69 lines incl. the scalars
   `0xde0424..0xde042c`, which are plain dword/byte loads before the buffer).
3. Base+index use from a LOWER base: all absolute operands `0xdd.... / 0xde03..` with an index register belong to the
   31-record (0xa4 bytes) table `0xddef80..0xde0364` (loops end at `cmp $0xde0360` / `cmp $0xde03e0`, 0x877695/0x875246) or
   to the small tables `0xde0360..0xde0420` (loops 0x877880..0x877895). The table of records is bounded by 31 entries
   (`cmp $0x1f` at 0x877688); `0xddfe1c(%eax,%esi)`/`0xddfd9c(%eax)` (0x87b2c2, 0x878083) use `id*0xa4` and would only reach the
   buffer for id >= 9 (record index 22+id >= 31), which is outside that table. Inferred, not proven by runtime.
4. Pointers to the buffer held in registers/stack only originate from the 20 operands (all `mov $buf,%reg`, `lea`
   or disp32 forms), so they follow the relocation.
Not proven: runtime-computed addresses that equal `0xde0430+k` without any such immediate (none found; would need
`0xde0000`-class constants, the scan above covers `0xdd....`/`0xde03xx` and found none).

## The 20 references (operand address = address of the imm32/disp32, bytes verified at install; all-or-nothing)
| Operand | Function | Instruction | Meaning | New value |
|---|---|---|---|---|
| 0x86ef50 | 0x86ef.. (reads vtable+0xf4 count) | `mov $0xde0430,%ecx` | read ids | old-0xde0430+base |
| 0x86efb6 | same | `movzwl 0xde0434(,%eax,4)` | read word a | rel |
| 0x86f8c2 / 0x86f8cb / 0x86f8d6 | 0x86f880 (calls 0x87d110, appends 1 record at index n when n < 60 `cmp $0x3c` 0x86f8a3) | `mov %eax,0xde0430(,%ecx,4)`, `+0xde0438`, `movl $0xa0004,0xde0434(,%ecx,4)` | write record n | rel (cap 60 kept) |
| 0x87ccae | 0x87cc60 box select | `mov $0xde0430,%ebx` | main loop over candidates | rel |
| 0x87cdbb | 0x87cc60 | `mov $0xde0434,%ebx` | fallback (nothing selectable) best-unit loop, count from vtable+0xf4 (<= 60) | rel |
| 0x87cfba | 0x87cee0 collector | `lea 0xde0434(,%eax,4),%edi` | write pointer for record n | rel |
| 0x87d050 | 0x87cee0 | `mov $0xde0430,%ecx` | dedupe scan (O(n^2), 250 is cheap) | rel |
| **0x87d07f** | 0x87cee0 | `cmp $0xde0704,%edi; jge 0x87d0d5` | **cap** | **base+4+250*12** |
| 0x87d0bf / 0x87d0cf | 0x87cee0 | `cmpw $1,0xde0434(,%eax,4)` / `mov %cx,0xde0434(,%eax,4)` | duplicate id: merge flags | rel |
| **0x87d0d8** | 0x87cee0 | `mov $0x3c,%eax` (buffer full return, bytes `b8 3c 00 00 00`) | returned count when full | **250** |
| 0x87d4c5 | 0x87d110 (virtual slot 0xf4 of two view classes, vtables 0x9e6e34/0x9e7208; also called by 0x86f880) | `lea 0xde0434(,%eax,4),%edx` | copy sorted pool to buffer | rel |
| 0x87d4fd | 0x87d110 | `cmp $0xde0704,%edx; jge 0x87d57a` | cap, **kept at 60 records** (rel shift only) | rel |
| 0x88774e, 0x8877b0, 0x8877c1 | 0x887660 | `mov $buf,%ecx`, `lea buf(%eax),%edx`, `mov buf(%ebx),%ecx` | read; count clamped to <= 59 (`cmp $0x3c; mov $0x3b` 0x887737) | rel |
| 0x887cdc, 0x887ced | 0x887c20 | `lea buf(%eax),%edx`, `mov buf(%edi),%ecx` | read; count clamped <= 59 (0x887ca2) | rel |
Not patched on purpose: `0x87d57c mov $0x3c,%eax` (0x87d110 return when full) and 0x86f8a3 (`cmp $0x3c`): 0x87d110/0x86f880
keep the 60 semantics because their consumers (0x887660, 0x887c20, 0x86ef.., 0x87cc60 fallback) clamp or loop on that count.
The per-class pools of 0x87d110 (`cmp $0x3c` at 0x87d427, 0x20-sized, in the view object, 0x334/0x348 offsets) are not the
global buffer and are untouched. `movups` readers (0x8877e2, 0x887d0e) over-read 4 bytes past a record: g_boxbuf has +16.
Both capacity compares are signed (`jge`): install refuses (logs) if the array would straddle 0x80000000.

## Does the box loop feed everything to select_unit?
`0x87cc60`: count = return of 0x87cee0 (-8(%ebp)), loop 0x87ccb2..0x87cd49 steps 12 bytes per candidate, no 60/0x3c bound
(the only constants are 0x32 = 50 px and the call args). `edx` (count) is reloaded from the stack after the call (0x87cd39).
Candidates are NOT sorted or type-filtered in this path (filters are in the collector: alive `+0x4c==2`, owner/visibility,
min class `0x95(type)`); order = view row/list scan order. The first 60 go to selected[], the rest return 0 from select_unit,
`box_select_call` turns that into 1 when they entered our list (190 max, then the loop ends at the first refusal as before).
Sorting by priority exists only in 0x87d110 (click path, per-class pools of 60), not in the box.

## Double-click and control groups (no change)
* Plain double-click: first click goes through the normal click path (clears the selection), the second runs
  `0x86f3f0` whose loop (0x86f4d5..0x86f538) continues after refusals; candidates are limited to units on screen (`0x8781b0`).
  Works with the existing select hook (stage 1b). It does not use the 0xde0430 buffer.
* Control group recall `0x7547f0` loops the player's unit list and ignores refusals, so units that were stored (max 60) are
  recalled. Groups store at most 60 ids; storing the overflow list into a group is out of scope (a recalled group of 60 plus
  the later double-click/box gives the rest).

## Risks / unverified
* Not run in game. All patches are operand rewrites checked against the exe bytes; a mismatch aborts all 20 (plain box then stays at 60).
* A plain box that selects 250 sends 60 + (up to) 4 clone packets per order; cost already accepted in stage 3.
* If another agent's code (or a trace tool) reads `0xde0430` directly it will now see stale data.
* ASLR: operands are patched absolutely like all other hooks (image base fixed 0x400000).

# Stage 5: circles and health bars (code: `flag_set/flag_clear/drop_all/prune` and the three hooks in `wololoader/src/overflow.c`)

Goal: units in our overflow list get the selection circle and the health bar. All addresses below were read in
`~/.cache/wl-test/exe.s` (grep of every `0x3a(%reg)` access, then context of each; helper scripts were ad hoc).
Unit object: owner `[u+0x10]`, state `[u+0x4c]`, hp float `[u+0x34]`, pos `[u+0x3c/0x40]`, byte `[u+0x39]` (sprite
frame), **flags byte `[u+0x3a]`**: bit0 selected, bit1/bit2 colour/flash highlights, bit3 (8) "not really selected"
state. Constructor 0x8b602e `movw $0,0x39(%ebx)` zeroes it; loader 0x8b8535 `movb $0,0x3a(%edi)` zeroes it after a load.

## 3. Where the circle and bar come from: the flag, not selected[]

No code draws circles by iterating selected[]. Per-unit render, per frame, for every unit of the view's draw lists:
* `0x8b5ed0` (unit draw, vtable slot, `ret 0xc`): `cmpb $0,0x3a(%esi)` (0x8b5eea) and state `[+0x4c] < 7`; flag non-zero ->
  `call *0x1c(%eax)` (0x8b5f14, circle back half) ... `call *0x18(%eax)` (0x8b5f8e, front half). Zero flag and world mode
  `[[0xaf2c58]+0x64] != 3` -> skipped.
* `0x8b30a0` (unit draw, `cmpb $0,0x3a(%edi)` 0x8b32b4, `call 0x8b1a40` 0x8b32e8) and the list walker `0x836260`
  (`cmpb $0,0x3a(%esi)` 0x8363d9, `call 0x8b1a40` 0x8363ea; iterates the draw list at `[this+0x18]`, calls vtable +0xc):
  overlay `0x8b1a40 -> 0x8b1a90` (`jmp`, `ret 0xc`), which uses the flag in 0x8b1aef/0x8b1b01 (colour: bit0 selected, bit1,
  bit2, bit3 variants), 0x8b1b86, 0x8b1cb0, and draws the **health bar** at 0x8b20b9: `and $9` == 1 (selected, bit3 clear),
  class flag `[[u+0xc]+0xc8]&2 == 0`, hp `[u+0x34] >= 1` (cvttss2si, `jle`), via `0x874920`; second block 0x8b1f17 same test
  plus owner test `0x74f390` (garrison/info icons).
* `0x86a6f0` (view draw): 0x86ac08 `testb $1,0x3a(%edx)`: the unit-outline pass (player colour silhouette via 0x86b800)
  SKIPS flagged units (they get the outline/overlay elsewhere). The first loop (0x86a810) walks the draw lists, not selected[].
  Effect for overflow units: no occlusion silhouette while flagged, same as normally selected units. Cosmetic.

So setting bit 0 gives circle and bar. Nothing else is required (no per-frame call of our own).

## 1. Every reader/writer of the flag (byte at +0x3a on units), and what a flagged unit that is NOT in selected[] does

| Address (function) | R/W | What it does | Flagged but not in selected[] |
|---|---|---|---|
| 0x754aae/0x754ab1 (select_unit) | R | bit0 set (and bit3 clear) -> `je 0x754c0a` return 1 "already selected", unit NOT added | **Matters**: a flagged unit can never enter selected[] through select_unit. Handled by the promotion step in `select_core` (see below). With bit3 also set: 0x754ac1 `movb $1` and return 1, also not added. |
| 0x754ac1, 0x754b43, 0x754bf6 (select_unit) | W | set flag (`movb $1`) when a unit is really added (also the same-type siblings loop 0x754bb0..0x754c07, bounded by `cmp $0x3c`) | n/a. Siblings loop 0x754bc7 `testb $1` skips flagged units: they stay overflow, no duplicate. |
| 0x75547f (unselect_all), 0x7554ca (unselect by index 0x7554a0) | W | `movb $0` for units found in selected[] | Overflow units are not touched: our hooks clear them (see below). |
| 0x754928, 0x7549ac (0x754910 select-as-primary) | R | flag test before/after select_unit | Reads only; goes through select_unit. |
| 0x74f149 (player fn 0x74f0c0) | R | loop over **selected[]** only, `testb $8` (bit3), writes `[unit+0x158]` | Not reached for overflow units (iterates selected[]). |
| 0x755879 (0x755850 prune of dead units) | - | iterates selected[] only, unselect by index | Not reached; our `prune` covers the overflow list. |
| 0x87e113 / 0x87e123 (0x87e0b0, flash timers) | W | `and ~mask` / `or mask` on `[unit+0x3a]` for timer entries; entries made by 0x8794b0(id, 0x5dc, mask, arg4) | All 7 callers (0x4b2437, 0x5aaba9, 0x6bea59, 0x6beae9, 0x80ee43, 0x86835a, 0x86e724) push mask = 2 (bit1). Bit0 never changed. (Entries from 0x879540 -> 0x87cbd0 not traced; unverified.) |
| 0x8b14cb (unit destructor 0x8b1450), 0x8b2418 (0x8b23f0), 0x8b2dbf and 0x8b2e1d (0x8b2c90 variants), 0x8b4ef1 (0x8b4e20, after vtable +0xec), 0x8b6297 (0x8b61f0), 0x8bab58 (0x8bab40), 0x8c48cd (0x8c47e0), 0x4b808d/0x4b8097 (0x4b8080) | R then `call 0x755540` | "if selected, unselect by pointer": death/destroy/transform/remove paths of the unit classes (vtables 0x9e7330.., 0x9eab38.., 0x9eba4c..) | **Safe and desired.** 0x755540 loops selected[] (bounded `cmp $0x3c`, `ret 4`), not found -> does nothing, no assert, no out-of-bounds. We hook 0x755540 and drop the unit from our list there. Without the flag these paths skipped the call, so setting the flag makes teardown notify us. |
| 0x8b9480 (0x8b9340, unit leaves the map: pos = -1) | calls 0x755540 unconditionally | garrison/hide | same as above |
| 0x666810, 0x7189e8, 0x719222, 0x71928e, 0x7192b3, 0x886c5f | calls 0x755540 | UI paths (selection panel clicks, shift/ctrl click on a flagged unit 0x886c28..0x886c5f) | Shift/ctrl-click on a flagged overflow unit = "unselect by pointer" -> our hook removes it. |
| 0x886c21..0x886cc7, 0x88718b..0x8871f9, 0x887304 (click handler 0x886970) | R | bit0 && !bit3: plain click -> `unselect_all` (0x755450, hooked) then `select_unit` (0x754a20, hooked); shift/ctrl -> unselect by pointer | Plain click on an overflow unit: our unselect_all clears every flag, then select_unit adds this one normally. |
| 0x4a92fe (0x4a9180 action done) | R | remembers the flag; after `call *0x5c` selects the unit again if bit0 and `[player+0x254] == 0` | Count is 60 while overflow exists; no effect. |
| 0x80d640 (0x80d580), 0x81518c, 0x81615f, 0x8166a0, 0x816aca (0x816130/0x816250/0x816a60 command executors) | R | if a unit of the order has bit0: `call 0x6c36e0` (refresh the command panel; reads `[player+0x15c]` and `[player+0x254]`) | Units come from the packet ids via `0x80e0f0`/`0x733bf0`, not selected[]. Harmless panel refresh when an order covers an overflow unit. |
| 0x7d9773 (0x7d9380, group evaluator over ids at `this+0xd8`, count `this+0xd0`) | R | `cmpb $1` -> `[this+0x398] = 1` | By id list. `0x398` is only reset/set in 0x7d7674/0x7d93f1/0x7d9779 in this file range (other readers not traced). Possible tiny behaviour change for formations of an order that contains overflow units. |
| 0x529699 (0x529590 group position average, ids at `this+0x50`) | R | `cmpb $1` -> `orl $4,0x1f0(%edi)` | By id. |
| 0x8b5eea, 0x8b32b4, 0x8363d9, 0x8b1aef.. (render) | R | see above | circle + bar |
| 0x6c36e0 and 0x74f390 | - | not flag readers | - |
| 0x8b602e (ctor), 0x8b8535 and 0x8b85fb (load/clone) | W | zero | A saved/loaded or new unit never inherits our flag. |
| 0x45bfa0.., 0x837efb, 0x84251f, 0x8426df, 0x87487e, 0x8749ab, 0x878747, 0x8787a1, 0x4a.. (word accesses at 0x3a) | - | belong to graphics/sprite classes (16-bit coordinates), not units | not related |

No reader indexes a fixed array by the flag, and nothing found collects units by flag for orders (orders use ids from the
packet). I did not find `rep movs` struct copies of the unit (text grep cannot exclude them); the loaders zero the byte anyway.
Not found in the dump: any save-game writer that reads `[u+0x3a]`.

## Decision: safe, implemented

Flag handling in `overflow.c` (bit 0 only, `|= 1` / `&= ~1`, so bits 1/2/3 are never disturbed):

| Event | Action |
|---|---|
| unit enters the list (`select_core`, also the already-listed path) | `flag_set` |
| `unselect_all` hook (player == list player) | `drop_all`: every entry that is still the live object (`get_obj(world,id) == ptr`) loses the flag, list emptied, then the game's unselect_all runs |
| unselect by pointer hook | found in the list (pointer compare, **no player check**, nothing dereferenced but the unit the game passes) -> removed and flag cleared; covers death, destruction, garrison, shift/ctrl-click, panel clicks |
| `prune` (select burst start, every order, `wl_overflow_count` every 100 ms) | dropped entries (dead, converted, not ours) that still exist and are not in selected[] lose the flag; freed ones are never touched; one that got into selected[] keeps it |
| `entry_ok` | the old "flag clear" test is replaced by a pointer scan of selected[] (the flag is set on purpose now) |
| list player object changes | `drop_all()` for the old list before switching |
| **promotion** (select_unit called for a listed unit while `[player+0x254] < 60`) | unit removed from the list and flag cleared before the original runs, so the game adds it to selected[] for real (otherwise 0x754ab1 would answer "already selected" and never add it) |
| game end / new game | every unit destructor reaches 0x755540 because the flag is set (the table above), so the list empties itself; stale entries are in any case validated by id + pointer before a write |

## Risks / unverified
* Cannot test in game here. Circle/bar rely on the render readers listed above (all read, not run).
* If a unit's owner is changed without any 0x755540 call, the flag stays until the next `prune` (<= 100 ms while the panel is drawn, or the next order/select burst); the unit would briefly show a circle for another player.
* The occlusion-silhouette pass skips flagged units (cosmetic).
* Flash-timer entries from 0x879540 and the other readers of `[this+0x398]` were not traced.
* `unselect_all` with `g_n > 0` assumes the world is valid (same assumption as every existing `prune` call); by the destructor argument `g_n` is 0 at teardown.
* A shift-click toggle on an overflow unit now really unselects it (before it did nothing).

---

# Stage 7: control groups (Ctrl+1..9 store, 1..9 recall)

Code: `wololoader/src/overflow.c` (`set_group_hook` on `0x74f0c0`, `recall_group_hook` on `0x7547f0`).
All addresses below read in `~/.cache/wl-test/exe.s` (objdump) and verified against `AoK HD.exe` 5.8 (PE section `.text`).

## Game storage mechanism (Ctrl+1..9 -> 0x74f0c0)

The game does **not** store control groups in a per-player list of IDs. Instead, it stores a 32-bit bitmask directly on each unit at offset `[unit+0x158]`.

1. **Hotkey Entry**: Pressing Ctrl+1..9/0 triggers `0x6c3f09 call 0x6bd7f0`, which calls `0x74f0c0` (`WorldPlayer::set_control_group`) with `group` (1..10, where 1..9 = Ctrl+1..9 and 10 = Ctrl+0):
   ```assembly
   6c3f05: cmove  %esi,%eax       ; if key was 0, eax = 10 (%esi)
   6c3f08: push   %eax
   6c3f09: call   0x6bd7f0       ; calls 0x74f0c0
   ```
2. **Function 0x74f0c0** (`__thiscall(player, group)`, `ret 4`):
   - Prologue: `55 8b ec 56 8b f1` (6 stolen bytes: `push %ebp; mov %esp,%ebp; push %esi; mov %ecx,%esi`).
   - Bounds and empty-selection check:
     ```assembly
     74f0c6: mov    0x8(%ebp),%ecx     ; group (1..10)
     74f0c9: lea    -0x1(%ecx),%eax
     74f0cc: cmp    $0x9,%eax
     74f0cf: ja     0x74f16b          ; group - 1 > 9 -> return 0
     74f0d5: cmpl   $0x0,0x254(%esi)  ; player->selection_count == 0
     74f0dc: je     0x74f16b          ; return 0
     ```
   - Bitmask computation via `0x8b55b0(group)`:
     ```assembly
     8b55be: mov    $0x1,%eax
     8b55c3: shl    %cl,%eax          ; returns 1 << group (e.g. 1<<1 = 2 for group 1)
     ```
   - Clear bitmask on existing player units: loops `player->0x18` (player's unit array), calls `vtable slot 0x300` (`0x4b0590`: `mov 0x158(%ecx),%eax; ret`), and clears the bit with `xor %ebx,0x158(%eax)`:
     ```assembly
     74f108: call   *0x300(%eax)      ; returns [unit+0x158]
     74f10e: and    %ebx,%eax
     74f110: cmp    %ebx,%eax
     74f112: jne    0x74f123
     74f11d: xor    %ebx,0x158(%eax)  ; clear bitmask on previous group members
     ```
   - Store bitmask onto current selection: loops `player->selected[0..59]` (`player+0x160`):
     ```assembly
     74f12e: lea    0x160(%esi),%edx  ; selected[0]
     74f136: cmp    0x254(%esi),%edi  ; loop up to selection_count
     74f149: testb  $0x8,0x3a(%eax)   ; skip non-primary temporary flags
     74f150: mov    %ebx,0x158(%eax)  ; [unit+0x158] = 1 << group
     74f15a: cmp    $0x3c,%ecx        ; strictly capped at 60 (0x3c)
     74f15d: jl     0x74f136
     ```
   - Returns 1 at `0x74f168` (`ret $0x4`).
3. **Cap**: The loop only visits `player->selected[0..59]`. Because overflow units are never in `selected[]`, the vanilla game stores group numbers for at most 60 units and completely ignores overflow units.

## Game recall mechanism (1..9 -> 0x7547f0)

1. **Hotkey Entry**: Pressing 1..9/0 calls `0x6c3f32 call 0x6be980`:
   ```assembly
   6be992: cmpl   $0x0,0xc(%ebp)     ; clear_first != 0?
   6be996: je     0x6be9aa
   6be9a5: call   0x755450          ; unselect_all(player)
   6be9aa: push   0x8(%ebp)          ; group
   6be9ba: call   0x7547f0          ; WorldPlayer::recall_control_group
   ```
2. **Function 0x7547f0** (`__thiscall(player, group)`, `ret 4`):
   - Prologue: `55 8b ec 83 ec 0c` (6 stolen bytes: `push %ebp; mov %esp,%ebp; sub $0xc,%esp`).
   - Bounds check: `7547fe: lea -1(%ecx),%eax; cmp $9,%eax; ja 0x754905` (return 0).
   - Bitmask: `75481b: call 0x8b55b0` computes `1 << group`.
   - Iterates all player units in `player->0x18`:
     ```assembly
     754841: call   *0x300(%eax)      ; returns [unit+0x158]
     754847: test   %eax,-0xc(%ebp)   ; test (1 << group)
     75484a: je     0x7548c4          ; skip if not in group
     75484c: mov    0x24(%edi),%eax   ; unit+0x24 == 0 (on map)
     754861: cmpl   $0x0,-0x4(%ebp)   ; first unit?
     75488f: push   $0x1
     754891: push   %edi
     754892: mov    %esi,%ecx
     754894: call   0x754a20          ; select_unit(player, unit, 1) for first unit
     ...
     7548a8: push   $0x0
     7548aa: push   %edi
     7548ab: mov    %esi,%ecx
     7548ad: call   0x754a20          ; select_unit(player, unit, 0) for subsequent units
     ```
   - When 60 units have been selected, `select_unit` (`0x754a20`) refuses candidate 61+ (`cmp $0x3c,%eax; jge 0x754c15` at 0x754b23). `0x7547f0` ignores refusals (`7548b5 test %eax,%eax; je 0x7548ca`) and finishes. Returns 1 if any unit was selected, 0 otherwise (`0x754902 ret $0x4`).

## Should units carry a group-number field on overflow units?

**No.** We must **not** set `[unit+0x158]` on overflow units.
- `0x7547f0` iterates `player->0x18` in memory allocation order. If overflow units carried `[unit+0x158] & mask`, `0x7547f0` would invoke `select_unit` on them during its loop, making which 60 units land in `selected[]` depend on unit creation order rather than selection order, and causing collisions with post-recall logic.
- Keeping `[unit+0x158] = 0` on overflow units guarantees isolation from other vanilla readers (`0x718a1b` in UI code, `0x7549c0` in click-select code).
- Furthermore, when an overflow unit is stored, if it had an old bitmask from previously being in `selected[60]` of another group, our store hook clears `[unit+0x158] = 0` so the old group will never recall it.

## Implementation in overflow.c

1. **Storage Table (`PlayerGroups`)**:
   - `g_pgroups[MAX_PGROUPS]` tracks up to 10 groups (indices 0..9 corresponding to groups 1..10) per player object.
   - Each group stores `count` and `ids[OVF_MAX]`.
2. **Hook 0x74f0c0 (`set_group_hook`)**:
   - Stolen bytes: `55 8b ec 56 8b f1` (6 bytes).
   - Calls `o_set_group(player, group)`.
   - If `r != 0`:
     - Prunes the current overflow list.
     - If `g_n > 0`: stores the `g_n` overflow IDs in `pg->g[idx]`, clears `*(DWORD*)(unit + 0x158) = 0` on them, and removes these IDs (and `selected[60]` IDs) from all other groups in `pg`.
     - If `g_n == 0`: sets `pg->g[idx].count = 0` (group was updated with <= 60 units, clearing previous overflow members).
3. **Hook 0x7547f0 (`recall_group_hook`)**:
   - Stolen bytes: `55 8b ec 83 ec 0c` (6 bytes).
   - Calls `o_recall_group(player, group)`.
   - Looks up `pg->g[idx]`. For each stored overflow unit ID:
     - Resolves pointer with `get_obj(world(), id)`. Prunes if NULL, dead (`[u+0x4c] >= 7 || [u+0x24] != 0`), or converted (`[u+0x10] != player`).
     - Passes valid unit to `select_core(player, u, flag, &caught)`:
       - If `selected[60]` has room (e.g. units died), `o_select` adds the unit to `selected[]`.
       - If `selected[60]` is full (60), `o_select` returns 0 and `select_core` catches it into `g_ovf[]`, sets `flag_set(u)` (circle and health bar), and logs the catch.
     - Flushes stats and returns 1 if units were recalled.

## Hooks installed summary
`wl_install_overflow()` now installs **8 hooks**:
- `0x754a20` `select_unit` (5 bytes)
- `0x755450` `unselect_all` (10 bytes)
- `0x755540` `unselect_ptr` (7 bytes)
- `0x80ef70` `send` (5 bytes)
- `0x87cd2d` `box_select_call` (call patch, 5 bytes)
- `0x813460` `exec_hook` (8 bytes)
- `0x74f0c0` `set_group_hook` (6 bytes)
- `0x7547f0` `recall_group_hook` (6 bytes)
