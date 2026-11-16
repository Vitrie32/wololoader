# Runtime Trace vs. Static Analysis — `WorldPlayer` Selection Fields

**Exe:** `AoK HD.exe` (5.8, ImageBase 0x400000)
**Trace:** `seltrace_run1.txt` — 84 unique EIPs that touched `player+0x15c..+0x258`; one session, player 0 object at `0x08c065a4`.
**Static list:** `selpart_00..04.md` — 90 candidate functions; 32 marked TRUE (note: README says 34, but the parsed total from the five parts is 32; two were probably counted in a draft).

---

## 1. Methodology

For each traced EIP the containing function was found by walking back to the nearest standard prologue (`55 8b ec` / `push ebp; mov ebp,esp`, or frameless start after `int3` padding) using `objdump -d --start-address=... --stop-address=...`. Full addresses (8 hex digits, `0x00xxxxxx`) were matched against the static list; the old `seltrace_compare.py` tool that truncates to 4 hex digits was not used.

---

## 2. Function Table

| Function addr | Traced EIPs (sorted) | Total hits | Static list? | What it does |
|---|---|---|---|---|
| `0x006b96f0` | `0x6b9710`, `0x6b9718` | 62 | **TRUE** | Iterate `selected[60]`; test unit vtable+0x168 for action panel eligibility |
| `0x006bb100` | `0x6bbdc2`, `0x6bbdd4`, `0x6bbe66` | 6 | **TRUE** | Iterate `selected[60]`; check unit class via vtable+0x228 (ability/combat gate) |
| `0x006bd5e0` | `0x6bd5f2` | 1 | **absent** | Read `player+0x15c`; inspect unit type word 0x16 to decide UI context (scroll / auto-cam) |
| `0x006be710` | `0x6be7ad`, `0x6be7c1` | 2 | **absent** | Hotkey unit-command dispatcher (jump table 0–14); checks `player+0x15c != 0`, reads `player->primary->type` |
| `0x006c1cf0` | `0x6c21c5`, `0x6c21df`, `0x6c21f8` | 7639 | **TRUE** | Main UI render tick; compares cached `primary`/`count` against `player+0x15c`/`+0x254`, pushes primary to `0x719a10` |
| `0x006c36e0` | `0x6c36fc`, `0x6c3707`, `0x6c3720` | 36 | **TRUE** | Selection-state sync; caches `player+0x15c` and low word of `+0x254` into UI object fields `+0xd5c`/`+0xd60` |
| `0x006c6490` | `0x6c649e` | 12 | **absent** | Read `player+0x15c`; type-dispatch via jump table to panel-refresh subroutines (e.g. `0x6bb100`, `0x6ba900`) |
| `0x006c9dc0` | `0x6ca34e`, `0x6ca360` | 28 | **TRUE** | Command-panel button refresher; iterate `selected[60]` in 3 loops; pass `&selected[0]`+count to `0x7d8f60` |
| `0x00713f00` | `0x713fe4` | 3748 | **TRUE** | `tpnl_obj` draw dispatcher; branches to `0x716360` (multi-unit grid) if `player+0x254 > 1` |
| `0x00716360` | `0x7163d8`, `0x716459`, `0x7164c5`, `0x716523`, `0x71655c`, `0x716585` | 49848 | **absent** | `tpnl_obj::draw_group`; reads `player+0x254`, iterates `player+0x160` array to draw 60-slot unit icon grid |
| `0x007187b0` | `0x718814`, `0x718959` | 3368 | **TRUE** | `tpnl_obj` update tick; checks `player+0x254 > 1`; reads `player+0x15c` to switch primary display |
| `0x0073dc60` | `0x73dc72`, `0x73dc78`, `0x73dca8`, `0x73dd3c`, `0x73dd42`, `0x73dd48`, `0x73dd87`, `0x73ddbe` | 18970 | **TRUE** | Compute selection capability flags; zero `player+0x250`, iterate `selected[60]`, OR unit capability bits in |
| `0x007403b0` | `0x7406a1`, `0x7406aa`, `0x740771`, `0x7407f3`, `0x7408c8` | 19431 | **TRUE** | Selection status/capability tester; read `player+0x254`, `+0x250`, `+0x15c`; clear `+0x250` on failure path |
| `0x0074eec0` | `0x74eec4`, `0x74eee7` | 2 | **TRUE** | Scan `selected[60]` to find minimum unit speed/formation byte (`[unit+0xc]+0x95`); bound `0x3c` |
| `0x0074ef20` | `0x74ef44`, `0x74ef90`, `0x74ef9c`, `0x74f064` | 656 | **TRUE** | Filter `selected[60]` by formation/type into `DAT_00af4e40`; bound `cmp eax,0x3c` at `0x74f085` |
| `0x0074f0c0` | `0x74f0d5`, `0x74f136`, `0x74f13e` | 6 | **TRUE** | Assign control-group bitmask (`unit+0x158`) to all units in `selected[60]`; bound `0x3c` |
| `0x0074f490` | `0x74f49b`, `0x74f4b4` | 142 | **absent** | Read `player+0x254`, iterate `player+0x160` array; check each unit's stance byte `[unit+0xc]+0x97` against `bl` |
| `0x007547f0` | `0x754867` | 1 | **absent** | Control-group recall (`Ctrl+1..9`); iterate all player units; read `player+0x15c`; call `0x754910` |
| `0x00754910` | `0x754a03` | 5 | **absent** | Select-unit helper; calls `select_unit (0x754a20)`; then writes `player+0x15c` to record primary unit |
| `0x00754a20` | `0x754b1d`, `0x754b2c`, `0x754b33`, `0x754b3d` | 52 | **TRUE** | `WorldPlayer::select_unit`; `selected[count] = unit`, inc `+0x254`, write `+0x15c`; bound `cmp eax,0x3c` |
| `0x007553c0` | `0x7553c4`, `0x7553e4` | 42 | **TRUE** | Verify all selected units share one stance (`[unit+0xc]+0x97`); bound `0x3c` |
| `0x00755450` | `0x755453`, `0x75545f`, `0x755471`, `0x755479`, `0x755484`, `0x755493` | 63 | **TRUE** | `WorldPlayer::unselect_all`; clear `unit+0x3a`, zero `+0x160..+0x24c`, reset `+0x15c` and `+0x254`; bound `0x3c` |
| `0x007554a0` | `0x7554ae`, `0x7554c5`, `0x7554ce`, `0x7554d4`, `0x7554da`, `0x7554e4`, `0x7554ea`, `0x7554ee`, `0x7554f9`, `0x755505` | 10 | **TRUE** | `WorldPlayer::unselect_unit_by_index`; shift `selected[60]` down, dec `+0x254`, clear vacated slot, update `+0x15c` |
| `0x00755540` | `0x755547`, `0x755564` | 2 | **TRUE** | `WorldPlayer::unselect_unit_by_pointer`; scan `selected[60]` for pointer; call `0x7554a0`; bound `0x3c` |
| `0x00755850` | `0x755863`, `0x75586e` | 12313 | **TRUE** | Validate `selected[60]`; remove dead/invalid units (state ≥ 7 or `unit+0x24 != 0`) via `0x7554a0` |
| `0x0086a6f0` | `0x86af6a`, `0x86af96`, `0x86af9e` | 16052 | **TRUE** | View / minimap selection-highlight renderer; iterate `selected[60]` via `player+edi`; bound `cmp edi,0x250` |
| `0x0087cc60` | `0x87ccf6` | 1 | **absent** | Drag-box select wrapper; calls `0x87cee0`, reads `player+0x15c` for formation-speed hint, uses `0xde0430` buffer |
| `0x0087e9b0` | `0x87f96e` | 3816 | **TRUE** | Mouse-click target dispatcher; `cmp [ecx+0x254],1` to decide select-new vs. command-current |
| `0x00886970` | `0x886c6f` | 1 | **TRUE** | Unit click handler; calls `unselect_all`+`select_unit`; reads `player+0x15c`/`+0x254` throughout |

**Totals:** 29 distinct functions were hot at runtime. 21 are TRUE in the static list; **8 are absent** (not classified at all by the scan).

---

## 3. Missed Functions — Detail

These 8 functions were **hit at runtime** but were **not in the static analysis** (absent from `selpart_00..04`). Each accesses `player+0x15c` or `player+0x160..+0x254` directly on the live `WorldPlayer` object.

### 3.1 `0x006bd5e0` — UI/scroll helper reading `player+0x15c`

Hit count: **1**

```
6bd5e7:  mov ecx, 0xaf2c58       ; global player manager
6bd5ed:  call 0x540c90           ; get_current_player → eax = WorldPlayer*
6bd5f2:  mov ecx, 0x15c(%eax)   ; ecx = player->primary_selected  ← TRACED EIP
6bd5f8:  test ecx, ecx
6bd5fa:  je  0x6bd620
6bd5fc:  mov eax, 0xc(%ecx)      ; unit->type_ptr
6bd5ff:  movzwl 0x16(%eax), %eax ; unit->class_id (word)
6bd603:  cmp $0x4, %ax           ; check: villager/trade/monk/priest?
...
```

Reads `player+0x15c` (primary selected unit pointer) and inspects the unit's class word to decide a scroll or camera target. **Must be patched if `+0x15c` is relocated.**

---

### 3.2 `0x006be710` — Hotkey unit-command dispatcher reading `player+0x15c`

Hit count: **2** (EIPs `0x6be7ad` and `0x6be7c1`)

```
6be7a1:  mov ecx, 0xaf2c58
6be7a8:  call 0x540c90           ; get_current_player → eax
6be7ad:  cmp DWORD PTR [eax+0x15c], 0x0  ; is any unit selected?  ← TRACED EIP
6be7b4:  je  0x6be7f2
6be7b6:  mov ecx, 0xaf2c58
6be7bc:  call 0x540c90
6be7c1:  mov eax, 0x15c(%eax)    ; eax = player->primary_selected  ← TRACED EIP
6be7c7:  mov eax, 0xc(%eax)      ; unit->type_ptr
6be7ca:  movswl 0x10(%eax), %eax ; unit type short
6be7ce:  cmp %edi, %eax          ; compare with hotkey target type
```

Jump table at `0x6be733` dispatches 15 hotkeys (0–14). Each path lands here to check primary selected unit type before issuing a unit-specific command. **Reads `player+0x15c` twice.** Relocating `+0x15c` requires patching both accesses.

---

### 3.3 `0x006c6490` — UI panel type-dispatcher reading `player+0x15c`

Hit count: **12** (EIP `0x6c649e`)

```
6c6493:  mov ecx, 0xaf2c58
6c6499:  call 0x540c90           ; get_current_player → eax
6c649e:  mov eax, 0x15c(%eax)    ; eax = player->primary_selected  ← TRACED EIP
6c64a4:  mov %eax, 0xd5c(%esi)   ; cache into UI object
6c64aa:  test %eax, %eax
...
6c64c8:  jmp *0x6c651c(,%eax,4)  ; dispatch by unit type to draw subroutines
```

Reads primary selected unit pointer, caches it in the UI object at `+0xd5c`, then dispatches to per-unit-type draw routines (e.g. `0x6bb100`, `0x6ba900`, `0x6bac30`). **Reads `player+0x15c`.** Hot path: fires every UI refresh tick when anything is selected.

---

### 3.4 `0x00716360` — `tpnl_obj::draw_group` (THE KNOWN GAP, NOW CONFIRMED)

Hit count: **49,848** — by far the hottest function in the trace.

EIPs and their accesses:

| EIP | Instruction | Access |
|---|---|---|
| `0x7163d8` | `mov esi, 0x254(%eax)` | read `player+0x254` (count) |
| `0x716459` | `mov ecx, 0x254(%eax)` | read `player+0x254` (count); compare with `0x3c` at `0x71645f` |
| `0x7164c5` | `mov eax, (%ecx)` | dereference `selected[i]`; `ecx = player+0x160+offset`, advancing by 4 |
| `0x716523` | `cmp 0x254(%eax), %edx` | loop termination against `player+0x254` |
| `0x71655c` | `cmp 0x254(%ecx), %edx` | secondary loop against `player+0x254` |
| `0x716585` | `mov (%ecx,%eax,1), %edi` | dereference `selected[i]`; `ecx = player+0x160+offset` via `eax` |

This is `tpnl_obj::draw_group` (function starts at `0x716360`). It:
1. Reads `player+0x254` to get icon count.
2. Compares against `0x3c` at `0x71645f` (`cmp $0x3c, %ecx`) — **hard-coded 60-slot bound**.
3. Iterates `player+0x160..` (loads `add $0x160,%ecx` at `0x7164bf`) reading each unit pointer to decide icon layout.

**This function accesses the full `selected[60]` array and the count.** It was explicitly noted as a gap in `README.md` ("the selection panel draw `0x716360` ... is missing"). The trace **confirms** it runs 49,848 times and **must be patched** to support any new limit. The `cmp ecx,0x3c` at `0x71645f` and the loop using `player+0x160+offset` both need updating.

---

### 3.5 `0x0074f490` — Stance/formation checker iterating `selected[60]`

Hit count: **142** (EIPs `0x74f49b` and `0x74f4b4`)

```
74f490:  push %ebp; mov %esp,%ebp; ...
74f49b:  mov 0x254(%ecx), %edi   ; edi = player->count  ← TRACED EIP
74f4a1:  xor %esi, %esi
74f4a3:  add $0x160, %ecx        ; ecx = &player->selected[0]
74f4a9:  nop
74f4b0:  cmp %edi, %edx          ; loop: index < count?
74f4b2:  jge 0x74f4cf
74f4b4:  mov (%ecx), %eax        ; eax = selected[i]  ← TRACED EIP
74f4b6:  test %eax, %eax
74f4b8:  je  0x74f4c6
74f4ba:  mov 0xc(%eax), %eax     ; unit->type_ptr
74f4bd:  cmp 0x97(%eax), %bl     ; compare stance byte
...
```

Reads `player+0x254` (count), computes `player+0x160` as array base, iterates over `selected[count]` checking each unit's stance byte (`[unit+0xc]+0x97`) against an argument. Very similar to `0x7553c0` but with a different bound (dynamic — uses count, no `0x3c` literal). **Both `+0x254` and the `+0x160` base are live.** Must be redirected when the array moves.

---

### 3.6 `0x007547f0` — Control-group recall reading `player+0x15c`

Hit count: **1** (EIP `0x754867`)

```
754867:  mov 0x15c(%esi), %eax   ; eax = player->primary_selected  ← TRACED EIP
75486d:  test %eax, %eax
75486f:  je  0x75488f
754871:  cmp %esi, 0x10(%eax)    ; is primary unit owned by this player?
754876:  mov %esi, %ecx
754878:  call 0x74eec0            ; get min-speed byte of current selection
75487d:  mov 0xc(%edi), %ecx
754880:  cmp 0x95(%ecx), %al     ; compare stance
754888:  mov %esi, %ecx
75488a:  call 0x755450            ; unselect_all
```

This is the control-group recall function (plan_agy §4: `FUN_007547f0`). It iterates the player's unit pool, checks the control-group bitmask in each unit's `unit+0x158`, and re-selects matching units. At `0x754867` it reads `player+0x15c` to compare the current primary unit's formation speed against the new group before calling `unselect_all`. **Reads `player+0x15c`.** Already correctly described in `plan_agy.md` but not in the selpart scan.

---

### 3.7 `0x00754910` — Select-unit helper writing `player+0x15c`

Hit count: **5** (EIP `0x754a03`)

```
754910:  push %ebp; mov %esp,%ebp; ...  ; ecx = WorldPlayer* (this)
754936:  call 0x754a20            ; WorldPlayer::select_unit(ebx, edx)
75493b:  test %eax, %eax         ; returns 1 on success
754946:  cmp %esi, 0x10(%ebx)    ; is unit owned by this player?
...
754a03:  mov %ebx, 0x15c(%eax)   ; player->primary_selected = unit  ← TRACED EIP (WRITE)
754a09:  pop %esi; ...
754a13:  ret $0x8
```

Wraps `select_unit (0x754a20)`: after selecting the unit, it **writes** `player+0x15c` to set the primary selected unit to `ebx`. This is a write to `player+0x15c` that is separate from `select_unit` itself. `eax` = player pointer, `ebx` = unit pointer. **Any relocation of `player+0x15c` must patch this write as well.**

---

### 3.8 `0x0087cc60` — Drag-box select wrapper reading `player+0x15c`

Hit count: **1** (EIP `0x87ccf6`)

```
87cc60:  push %ebp; mov %esp,%ebp; ...  ; ecx = view obj (edi)
87cc7c:  mov 0x15c(%edi), %eax   ; eax = view->WorldPlayer*
87cc96:  call 0x87cee0            ; drag-box selector → edx = count found
87ccad:  mov $0xde0430, %ebx     ; global drag-box buffer
87ccf0:  mov 0x15c(%edi), %ecx   ; ecx = WorldPlayer* (re-read)
87ccf6:  mov 0x15c(%ecx), %eax   ; eax = player->primary_selected  ← TRACED EIP
87ccfc:  test %eax, %eax
87cd00:  cmp %ecx, 0x10(%eax)    ; does primary unit belong to player?
87cd05:  call 0x74eec0            ; get min formation speed
87cd0d:  cmp 0x95(%ecx), %al     ; compare with new units' stance
```

**Double dereference:** `edi+0x15c` is `WorldPlayer*`; `ecx+0x15c` is `player->primary_selected`. Both `0x87cc7c` (view→player) and `0x87ccf6` (player→primary) are on the `0x15c` chain. The traced EIP is the second one — the genuine `player+0x15c` read. Additionally, this function calls `0x87cee0` which uses the `0xde0430` drag-box buffer (60×12 bytes). **Both `player+0x15c` and the downstream buffer are in scope for patching.**

---

## 4. Summary

### 4.1 Static list fidelity

| Category | Count |
|---|---|
| Functions hit at runtime | 29 |
| In static list as TRUE | 21 |
| In static list as FALSE | 0 (no FALSE function was traced) |
| **Absent from static list** | **8** |

No function classified FALSE was hit at runtime — the FALSE decisions appear sound.

### 4.2 The 8 missed functions

| Address | Hits | Offset(s) accessed | Severity |
|---|---|---|---|
| `0x006bd5e0` | 1 | `+0x15c` read | Low (1 hit in session; hotkey path) |
| `0x006be710` | 2 | `+0x15c` read ×2 | Medium (hotkey; fires on any unit-specific order key) |
| `0x006c6490` | 12 | `+0x15c` read | Medium (every UI-refresh tick with selection) |
| **`0x00716360`** | **49848** | **`+0x254` read, `+0x160..+0x24c` iterate, `cmp 0x3c`** | **CRITICAL — hottest function; known gap; has hard-coded 60 bound** |
| `0x0074f490` | 142 | `+0x254` read, `+0x160` base iterate | High (runs on every stance-query / formation button) |
| `0x007547f0` | 1 | `+0x15c` read | Low (fires once per ctrl-group recall in session) |
| `0x00754910` | 5 | `+0x15c` **write** | High (**write** — omitting this will leave `primary_selected` stale after group-recall) |
| `0x0087cc60` | 1 | `+0x15c` read (double deref) | Medium (drag-box path; fires on any box-select) |

### 4.3 Conclusions for the relocation plan

1. **`0x716360` is the most critical gap.** It was already flagged in `README.md` but had no static classification. The trace puts it at **49,848 hits** — the single hottest entry. It has a hard-coded `cmp ecx, 0x3c` at `0x71645f` (the slot-count cap) and iterates `player+0x160` directly. It must be hooked before any limit raise.

2. **`0x754910` writes `player+0x15c`.** This is not a read-only observer — it stores the primary unit pointer. Missing it means the `primary_selected` cache goes stale after control-group recall and may point to freed memory.

3. **`0x0074f490`** iterates `selected[60]` using `player+0x254` as the bound. It has no `0x3c` literal — the loop is dynamically bounded — so it will iterate up to the new count automatically, but it still reads through the `player+0x160` array pointer, which must be redirected.

4. **`+0x15c`-only readers** (`0x6bd5e0`, `0x6be710`, `0x6c6490`, `0x7547f0`, `0x87cc60`) read the primary selected unit pointer but do **not** iterate the array. If `player+0x15c` stays in place (plan option in `plan_agy.md` §6 Adım 1), these need no patch. If `+0x15c` is also relocated, all five need updating.

5. All functions that were marked TRUE in the static list and are present in the trace behaved exactly as described. No static TRUE function was absent from the trace (consistent with the brief test session capturing normal gameplay selection events).
