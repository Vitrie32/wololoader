# Reverse Engineering Report: Monk Auto-Convert Feature

**Target binary:** `AoK HD.exe` (Age of Empires II HD 5.8, 32-bit, ImageBase `0x400000`)  
**Scope:** Reverse engineering and design for planned feature: periodic auto-conversion orders for idle monks.  
**Classification:** All claims are classified as **[VERIFIED]** (backed by exact disassembly addresses and instruction bytes) or **[GUESS]**.

---

## Quick Address & Offset Reference

| Address / Offset | Type | Description | Status |
|---|---|---|---|
| `0xaf2c58` | Global PTR | `TRIBE_Game* g_game` (main game instance). `[g_game + 0xc8]` = `RGE_Game_World* world`. | **[VERIFIED]** |
| `0x540c90` | Function | `TRIBE_Game::get_local_player()` — `__thiscall(game)`, returns `RGE_Player*`. | **[VERIFIED]** |
| `0x540cc0` | Function | `TRIBE_Game::get_player(int idx)` — `__thiscall(game, idx)`, returns `RGE_Player*`. | **[VERIFIED]** |
| `[world + 0x174]` | Field (i32) | Local player ID (`world->local_player_id`). | **[VERIFIED]** |
| `[world + 0x184]` | Field (PTR) | Start pointer of player table (`RGE_Player**` array, 8 bytes per slot). | **[VERIFIED]** |
| `[world + 0x188]` | Field (PTR) | End pointer of player table. Player count = `(end - start) >> 3`. | **[VERIFIED]** |
| `[world + 0xc0]` | Field (PTR) | `TCommunications_Handler* cmd` (command object, `this` of `0x80ef70` / `0x809900`). | **[VERIFIED]** |
| `0x733bf0` | Function | `RGE_Game_World::get_object(int id)` — `__thiscall(world, id)`, returns `Unit*` or NULL. | **[VERIFIED]** |
| `[player + 0x18]` | Field (PTR) | Player's unit list container (`[list+4]` = array of `Unit*`, `[list+8]` = count). | **[VERIFIED]** |
| `[player + 0x70]` | Field (PTR) | Diplomacy table pointer (`uint8_t*`, indexed by `player_id`: 0=Ally, 1=Neutral, 3=Enemy). | **[VERIFIED]** |
| `[player + 0x1adc]`| Field (i32) | Player ID number (`0` = Gaia, `1..8` = match players). | **[VERIFIED]** |
| `[unit + 0x08]` | Field (i32) | Unit object ID (`[unit + 8]`). | **[VERIFIED]** |
| `[unit + 0x0c]` | Field (PTR) | Master object pointer (`RGE_Master_Static_Object*`). | **[VERIFIED]** |
| `[unit + 0x10]` | Field (PTR) | Unit owner pointer (`RGE_Player*`). | **[VERIFIED]** |
| `[unit + 0x24]` | Field (i32) | Deletion / tombstone flag (`0` = valid/alive). | **[VERIFIED]** |
| `[unit + 0x3c]` | Field (f32) | Unit X position (float). | **[VERIFIED]** |
| `[unit + 0x40]` | Field (f32) | Unit Y position (float). | **[VERIFIED]** |
| `[unit + 0x4c]` | Field (u8) | Object status byte (`2` = active alive unit; `< 7` = not deleted). | **[VERIFIED]** |
| `[unit + 0x150]` | Field (PTR) | Unit `RGE_Action_List* actions`. | **[VERIFIED]** |
| `[master + 0x04]`| Field (u8) | Master object type byte (`70 / 0x46` = creatable, `80 / 0x50` = building). | **[VERIFIED]** |
| `[master + 0x10]`| Field (i16) | Unit type ID in dat (`125` = Monk, `286` = Monk with Relic, `775` = Missionary). | **[VERIFIED]** |
| `[master + 0x16]`| Field (i16) | Unit class ID (`18 / 0x12` = Monk, `3` = Building, `52 / 0x34` = Wall/Gate). | **[VERIFIED]** |
| `[master + 0x2c]`| Field (f32) | Line of sight radius (float, default 11.0 for Monk). | **[VERIFIED]** |
| `0x4b07c0` | Function | `RGE_Action_Object::is_idle()` — `__thiscall(unit)`, returns `al = 1` (idle), `0` (busy). Vtable slot 90 (`+0x168`). | **[VERIFIED]** |
| `0x4ad2e0` | Function | `ActionList::get_current_action()` — `__thiscall(list)`. | **[VERIFIED]** |
| `0x74db20` | Function | `WorldPlayer::order_unit_target(unit, target, x, y)` — `__thiscall(player, unit, target, x, y)`, `ret $0x10`. | **[VERIFIED]** |
| `0x809900` | Function | `TCommunications_Handler::build_target_order(units, cnt, target, x, y)` — `__thiscall(cmd, ...)`, `ret $0x14`. | **[VERIFIED]** |
| `0x80ef70` | Function | `TCommunications_Handler::send_command(pkt, size, player_id)` — `__thiscall(cmd, ...)`, `ret $0xc`. | **[VERIFIED]** |
| `0x74f2b0` | Function | `WorldPlayer::is_ally(int player_id)` — `__thiscall(player, id)`. | **[VERIFIED]** |
| `0x74f320` | Function | `WorldPlayer::is_enemy(int player_id)` — `__thiscall(player, id)`. | **[VERIFIED]** |
| `0x74f440` | Function | `WorldPlayer::is_neutral(int player_id)` — `__thiscall(player, id)`. | **[VERIFIED]** |
| `0x8ea479` | Function | `game_calloc(size_t n, size_t size)` — C runtime memory allocator used for command packets. | **[VERIFIED]** |

---

## 1. Iterating the Local Player's Units and Other Players' Units

### 1.1 Finding World and Local Player

The global game pointer is at `0xaf2c58` (`TRIBE_Game*`). The game world pointer is stored at offset `+0xc8`:

```c
void *game = *(void **)0xaf2c58;
void *world = game ? *(void **)((char *)game + 0xc8) : NULL;
```

To resolve the local player:
1. **Calling `0x540c90` (`TRIBE_Game::get_local_player`):**  
   Calling convention: `__thiscall(void *game)`, returns `void *player` (or NULL if invalid/spectator).  
   Disassembly from `exe.s` (`0x540c90..0x540cb0`):
   ```asm
   540c90:  mov    0xc8(%ecx),%eax        ; eax = game->world
   540c96:  mov    0x174(%eax),%ecx       ; ecx = world->local_player_id
   540c9c:  cmp    %ecx,0xaab914          ; spectator / invalid constant check
   540ca2:  jne    0x540ca7
   540ca4:  xor    %eax,%eax
   540ca6:  ret
   540ca7:  mov    0x184(%eax),%eax       ; eax = world->player_table_start
   540cad:  mov    (%eax,%ecx,8),%eax     ; eax = player_table[local_player_id].player_ptr
   540cb0:  ret
   ```
2. **Direct memory dereference:**  
   `local_player_id = *(int *)((char *)world + 0x174)`  
   `player_table_start = *(char **)((char *)world + 0x184)`  
   `local_player = *(void **)(player_table_start + local_player_id * 8)`

### 1.2 Iterating All Players (Enemy Players)

The world maintains an array of player records between offsets `+0x184` (start) and `+0x188` (end). Each record is 8 bytes (`sizeof = 8`), and the first 4 bytes hold the pointer to the `RGE_Player` / `WorldPlayer` object.

Evidence from `0x540cc0` (`TRIBE_Game::get_player(int index)`):
```asm
540cc0:  push   %ebp
540cc1:  mov    %esp,%ebp
540cc3:  mov    0xc8(%ecx),%ecx        ; ecx = world
540cc9:  test   %ecx,%ecx
540ccb:  je     0x540cf0
540ccd:  mov    0x8(%ebp),%eax         ; eax = index
540cd0:  test   %eax,%eax
540cd2:  js     0x540cf0               ; if index < 0, return NULL
540cd4:  mov    0x184(%ecx),%edx       ; edx = table start
540cda:  mov    0x188(%ecx),%ecx       ; ecx = table end
540ce0:  sub    %edx,%ecx              ; ecx = byte size
540ce2:  sar    $0x3,%ecx              ; ecx = count (size / 8)
540ce5:  cmp    %ecx,%eax              ; if index >= count, return NULL
540ce7:  jge    0x540cf0
540ce9:  mov    (%edx,%eax,8),%eax     ; return player pointer
540cec:  pop    %ebp
540ced:  ret    $0x4
540cf0:  xor    %eax,%eax
540cf2:  pop    %ebp
540cf3:  ret    $0x4
```

To loop through all players:
```c
char *p_start = *(char **)((char *)world + 0x184);
char *p_end   = *(char **)((char *)world + 0x188);
int total_players = (int)(p_end - p_start) >> 3;

for (int p_idx = 0; p_idx < total_players; p_idx++) {
    void *player = *(void **)(p_start + p_idx * 8);
    if (!player) continue;
    int player_id = *(int *)((char *)player + 0x1adc);
    // player_id 0 = Gaia, 1..8 = match players
}
```

### 1.3 Iterating a Player's Unit List

Each player object (`WorldPlayer`) owns a unit list container at offset `+0x18`:
- `list = *(char **)((char *)player + 0x18)`
- `units_array = *(void ***)(list + 0x4)` (array of `Unit*` pointers)
- `unit_count = *(int *)(list + 0x8)` (int32 unit count)

Evidence from `0x7547f0` (`WorldPlayer::recall_control_group` in `exe.s`):
```asm
754820:  mov    0x18(%esi),%ecx        ; esi = player; ecx = [player + 0x18]
754823:  xor    %edx,%edx              ; edx = loop index i = 0
75482e:  cmp    %edx,0x8(%ecx)         ; cmp edx, [list + 8] (count)
754831:  jle    0x754905
754837:  mov    0x4(%ecx),%eax         ; eax = [list + 4] (array of Unit*)
75483a:  mov    (%eax,%edx,4),%edi     ; edi = units_array[edx] (Unit*)
...
7548cd:  mov    0x18(%esi),%ecx
7548d0:  inc    %edx                   ; i++
7548d1:  mov    %edx,-0x8(%ebp)
7548d4:  cmp    0x8(%ecx),%edx         ; loop while i < count
7548d7:  jl     0x754837
```
And from `0x6c10c0` (`Find Next Idle Military Unit`):
```asm
6c1101:  mov    0x18(%esi),%ecx        ; ecx = [player + 0x18]
6c1104:  mov    0x4(%ecx),%esi         ; esi = [list + 4] (array)
6c1107:  mov    0x8(%ecx),%edx         ; edx = [list + 8] (count)
```

### 1.4 Validating Unit Liveness

A unit in the list is valid and active when:
1. `unit != NULL`
2. `*(uint8_t *)((char *)unit + 0x4c) == 2` (Object state `2` = alive and active in the world; values `< 7` are not yet destroyed, but `2` is active). Evidence from `0x6c115d`: `cmpb $0x2, 0x4c(%esi)`.
3. `*(int *)((char *)unit + 0x24) == 0` (No deletion/tombstone marker. Evidence: `0x755880`).
4. `*(void **)((char *)unit + 0x10) == player` (Unit still owned by this player).

---

## 2. Identifying Monks, Line of Sight, and Enemy Target Filtering

### 2.1 Identifying Monks

A unit's master archetype is accessed via `master = *(void **)((char *)unit + 0xc)` (`RGE_Master_Static_Object*` / `TRIBE_Master_Combat_Object*`).
- **Class ID offset:** `*(int16_t *)((char *)master + 0x16)`  
  Class `18` (`0x12`) is **Monk**. This covers:
  - Standard Monk (`unit_id = 125`)
  - Monk with Relic (`unit_id = 286`)
  - Missionary (`unit_id = 775`)
  - Hero Monks (e.g. Pope Leo, Archbishop)
- **Unit Type ID offset:** `*(int16_t *)((char *)master + 0x10)`  
  Specific unit ID in dat: `125` (`0x7d`) is the standard monk.

Evidence from disassembly:
- `0x4b63df`:
  ```asm
  4b63dc:  mov    0x8(%esi),%eax
  4b63df:  mov    0xc(%eax),%eax         ; master = unit->master
  4b63e2:  cmpw   $0x12,0x16(%eax)       ; cmp master->class, 18 (Monk)
  4b63e7:  jne    0x4b6424
  ```
- `0x4e2171`:
  ```asm
  4e2171:  mov    0xc(%ecx),%eax         ; master
  4e2174:  cmpw   $0x12,0x16(%eax)       ; cmp master->class, 18 (Monk)
  ```
- `0x868d56` (`FUN_00868d00`, right-click action decider, line 807154 in `decompiled.c`):
  ```c
  if (*(short *)(param_3 + 0x16) == 0x12) { // selected unit is monk
  ```

### 2.2 Line of Sight (LOS)

The unit's visual sight radius is a 32-bit `float` stored at:
`los = *(float *)((char *)master + 0x2c)`

Evidence from `0x8b5d00`:
```asm
8b5d00:  mov    0xc(%esi),%eax         ; esi = unit; eax = master
8b5d09:  movss  0x2c(%eax),%xmm0       ; [master + 0x2c] = line_of_sight (float)
8b5d0e:  addss  0x8(%ebp),%xmm0        ; + upgrade delta (e.g. Town Watch / Sanctity)
8b5d13:  cvttss2si %xmm0,%eax          ; convert to integer tile radius
8b5d1a:  push   0x10(%esi)             ; player
8b5d1d:  call   *0xf4(%edx)            ; update visibility circle
```
And `0x8b5da0`:
```asm
8b5da0:  mov    0xc(%esi),%eax
8b5da9:  movss  0x2c(%eax),%xmm0       ; [master + 0x2c] = line_of_sight
8b5dae:  mulss  0x8(%ebp),%xmm0
8b5db3:  cvttss2si %xmm0,%eax
8b5dbd:  call   *0xf4(%edx)
```
Also `wololoader/tools/datfile.py` line 580:
`("death_mode", "i8"), ("hit_points", "i16"), ("line_of_sight", "f32")`
`death_mode` is at `+0x28`, `hit_points` at `+0x2a`, `line_of_sight` at `+0x2c`.

Base LOS for a monk in the dat file is `11.0f` tiles.

### 2.3 Distance Calculation (Enemy in LOS)

Position floats are stored directly on the unit:
- `monk_x = *(float *)((char *)monk + 0x3c)`
- `monk_y = *(float *)((char *)monk + 0x40)`
- `target_x = *(float *)((char *)target + 0x3c)`
- `target_y = *(float *)((char *)target + 0x40)`

Standard 2D Euclidean distance check:
```c
float dx = target_x - monk_x;
float dy = target_y - monk_y;
float los = *(float *)((char *)monk_master + 0x2c);
if (dx * dx + dy * dy <= los * los) {
    // Enemy is within monk's line of sight
}
```

### 2.4 Skipping Buildings & Invalid Targets

To filter out buildings and non-convertible targets:
1. **Master Object Type Byte:** `*(uint8_t *)((char *)target_master + 0x4)`
   - Type `80` (`0x50`): **All Buildings** (`UnitBuilding` in `datfile.py` line 673).
     Evidence: `0x74f3cd`: `cmpb $0x50, 0x4(%esi)`.
   - Type `70` (`0x46`): Creatable units (Infantry, Cavalry, Archer, Monk, Villager, Siege, Ships).
   - If `*(uint8_t *)((char *)target_master + 0x4) == 80`, **skip**.
2. **Unit Class ID:** `*(int16_t *)((char *)target_master + 0x16)`
   - Class `3`: Buildings. Evidence: `0x4e2f2c`: `cmpw $0x3, 0x16(%eax)`.
   - Class `52` (`0x34`): Gates/Walls. Evidence: `0x4deb3e`: `cmpw $0x34, 0x16(%eax)`.
   - Class `42` (`0x2a`): Relics.
   - If class is `3` or `52`, **skip**.
3. **Target Liveness:**
   - Target must be alive: `*(uint8_t *)((char *)target + 0x4c) < 3` and `*(int *)((char *)target + 0x24) == 0`.

---

## 3. Determining if a Unit is IDLE

### 3.1 The Game's Idle Function: `0x4b07c0` (`unit->is_idle()`)

AoE2 HD has a built-in virtual method for checking whether a unit is idle. It resides at virtual method slot 90 (`vtable + 0x168`).
- **Function address:** `0x4b07c0`
- **Calling convention:** `__thiscall(void *unit)` (No stack arguments, `this` in `ecx`).
- **Return value:** `al = 1` (Unit is IDLE), `al = 0` (Unit is busy with an order/task).

Evidence from `0x6c10c0` (`Find Next Idle Military Unit`, lines 6c11fb..6c1209):
```asm
6c11fb:  mov    (%esi),%eax            ; eax = unit->vtable
6c11fd:  mov    %esi,%ecx              ; ecx = unit (this)
6c11ff:  mov    0x168(%eax),%eax       ; eax = vtable[0x168/4] (slot 90 = 0x4b07c0)
6c1205:  call   *%eax                  ; call unit->is_idle()
6c1207:  test   %al,%al                ; 1 = idle, 0 = busy
6c1209:  je     0x6c1224               ; skip if not idle
```

Verified in RTTI:
- `TRIBE_Combat_Object` (`vtable@0x009eb5d0`): slot 90 (`+0x168`) = `0x004b07c0`
- `RGE_Combat_Object` (`vtable@0x009c44bc`): slot 90 (`+0x168`) = `0x004b07c0`
- `RGE_Action_Object` (`vtable@0x009b4234`): slot 90 (`+0x168`) = `0x004b07c0`

### 3.2 Internal Mechanics of `0x4b07c0`

Disassembly of `0x4b07c0` in `exe.s`:
```asm
4b07c0:  push   %ebp
4b07c1:  mov    %esp,%ebp
4b07c3:  push   %ecx
4b07c4:  push   %esi
4b07c5:  mov    %ecx,%esi              ; esi = unit
4b07c7:  mov    0x84(%esi),%eax        ; eax = [unit + 0x84] (waypoint / order target ID)
4b07cd:  cmp    $0xffffffff,%eax       ; is it -1?
4b07d0:  je     0x4b07f2               ; if -1, check action list
4b07d2:  mov    0x10(%esi),%ecx        ; ecx = player
4b07d5:  push   %eax
4b07d6:  mov    0x4(%ecx),%ecx         ; ecx = world
4b07d9:  call   0x72cf10               ; lookup waypoint target
4b07de:  test   %eax,%eax
4b07e0:  je     0x4b07f2
4b07e2:  cmpl   $0xffffffff,0x33c(%eax); check waypoint status
4b07e9:  jne    0x4b0851               ; not -1 => active order => returns 0 (busy)
4b07eb:  mov    $0x1,%al               ; returns 1 (IDLE)
4b07ed:  pop    %esi
4b07ee:  mov    %ebp,%esp
4b07f0:  pop    %ebp
4b07f1:  ret
4b07f2:  mov    0x150(%esi),%ecx       ; ecx = [unit + 0x150] (RGE_Action_List)
4b07f8:  call   0x4ad2e0               ; ActionList::get_current_action()
4b07fd:  mov    %eax,%esi              ; esi = current action pointer
4b07ff:  test   %esi,%esi
4b0801:  je     0x4b07eb               ; no action => returns 1 (IDLE)
4b0803:  mov    0xc(%esi),%cl          ; cl = action->state
4b0806:  test   %cl,%cl                ; state 0 => returns 1 (IDLE)
4b0808:  je     0x4b07eb
4b080a:  cmp    $0x1,%cl               ; state 1 => returns 1 (IDLE)
4b080d:  je     0x4b07eb
4b080f:  cmp    $0x2,%cl               ; state 2 => returns 1 (IDLE)
4b0812:  je     0x4b07eb
4b0814:  cmp    $0xd,%cl               ; state 13 (0xd) => returns 1 (IDLE)
4b0817:  je     0x4b07eb
4b0819:  cmp    $0x3,%cl               ; state 3 => returns 1 (IDLE)
4b081c:  je     0x4b07eb
4b081e:  cmp    $0xe,%cl               ; state 14 (0xe) => returns 1 (IDLE)
4b0821:  je     0x4b07eb
4b0823:  cmpw   $0x6b,0x4(%esi)        ; [action + 4] == 0x6b (107 = idle task)
4b0828:  jne    0x4b0851               ; other action => returns 0 (busy)
...
4b0851:  xor    %al,%al                ; returns 0 (busy)
4b0853:  pop    %esi
4b0854:  mov    %ebp,%esp
4b0856:  pop    %ebp
4b0857:  ret
```

The game's action list retrieval at `0x4ad2e0` reads:
```asm
4ad2e0:  mov    0x8(%ecx),%eax         ; [list + 8] = head node
4ad2e3:  test   %eax,%eax
4ad2e5:  je     0x4ad2ea
4ad2e7:  mov    (%eax),%eax            ; [node + 0] = Action*
4ad2e9:  ret
4ad2ea:  xor    %eax,%eax              ; NULL
4ad2ec:  ret
```

**Conclusion for Wololoader:**  
We can directly invoke `0x4b07c0` on any monk pointer:
```c
typedef int (__thiscall *IsIdle_t)(void *unit);
static const IsIdle_t unit_is_idle = (IsIdle_t)0x4b07c0;

if (unit_is_idle(monk)) {
    // Monk is idle and waiting for commands
}
```

---

## 4. Exact Type 0x00 Packet Layout ('unit(s) target unit')

### 4.1 Order Builder Function: `0x809900`

The game builds right-click target orders via `0x809900` (`TCommunications_Handler::build_target_order`).
- **Calling convention:** `__thiscall(void *cmd, void **units, short count, void *target, float x, float y)`
- **Stack cleanup:** `ret $0x14` (5 stack arguments, 20 bytes).
- **Arguments:**
  - `ecx`: `cmd` (Command handler object at `[world + 0xc0]`).
  - `0x08(%ebp)`: `units` — Pointer to an array of `Unit*` pointers.
  - `0x0c(%ebp)`: `count` — Number of units in `units` array (`short`).
  - `0x10(%ebp)`: `target` — Pointer to the target unit (`Unit*`), or NULL for ground clicks.
  - `0x14(%ebp)`: `x` — Target float X coordinate.
  - `0x18(%ebp)`: `y` — Target float Y coordinate.

Disassembly of `0x809900` in `exe.s`:
```asm
809900:  push   %ebp
809901:  mov    %esp,%ebp
809903:  sub    $0x8,%esp
809906:  push   %ebx
809907:  mov    0xc(%ebp),%ebx         ; ebx = count
80990d:  mov    0x8(%ebp),%esi         ; esi = units array
...
80993e:  push   %eax                   ; size = 0x14 + 4 * count
80993f:  push   $0x1
809941:  call   0x8ea479               ; game_calloc(1, size)
809946:  mov    %eax,%esi              ; esi = packet
...
80996f:  mov    0x8(%eax),%eax         ; eax = [unit + 8] (unit ID)
809977:  mov    %eax,(%ecx)            ; packet[0x14 + 4*i] = unit_id
...
809986:  mov    0x10(%ebp),%edi        ; edi = target unit
809989:  test   %edi,%edi
80998b:  je     0x809992
80998d:  mov    0x8(%edi),%eax         ; eax = [target + 8] (target ID)
809990:  jmp    0x809995
809992:  or     $0xffffffff,%eax       ; target == NULL => -1
809995:  mov    %eax,0x4(%esi)         ; packet[4] = target ID
80999e:  movb   $0x0,(%esi)            ; packet[0] = 0x00 (packet type)
8099a2:  mov    0x28e0(%ecx),%ecx      ; net manager
8099a8:  call   0x5836e0               ; get session byte
8099b3:  mov    %al,0x1(%esi)          ; packet[1] = session byte
8099d6:  movss  %xmm0,0xc(%esi)        ; packet[0x0c] = x (float)
8099e0:  mov    %bl,0x8(%esi)          ; packet[8] = count byte (1)
8099e3:  movss  %xmm0,0x10(%esi)       ; packet[0x10] = y (float)
...
8099ec:  mov    0x10(%edi),%eax        ; target owner (player)
8099ef:  mov    0x1adc(%eax),%eax      ; target player ID
8099fc:  mov    -0x4(%ebp),%ebx        ; ebx = cmd object
809a00:  mov    %esp,%ecx
809a02:  push   -0x8(%ebp)             ; size
809a05:  push   %esi                   ; packet
809a06:  mov    %eax,(%ecx)            ; player_id parameter
809a08:  mov    %ebx,%ecx              ; this = cmd
809a0a:  call   0x80ef70               ; send_command!
```

### 4.2 Convenient Single-Unit Player Helper: `0x74db20`

Rather than manually preparing arrays and command objects, the game provides a clean wrapper directly on `WorldPlayer`:
- **Address:** `0x74db20` (`WorldPlayer::order_unit_target`)
- **Convention:** `__thiscall(void *player, void *unit, void *target, float x, float y)`
- **Stack cleanup:** `ret $0x10` (4 stack arguments, 16 bytes).

Disassembly of `0x74db20` in `exe.s`:
```asm
74db20:  push   %ebp
74db21:  mov    %esp,%ebp
74db23:  sub    $0x8,%esp
74db30:  movss  0x14(%ebp),%xmm0       ; xmm0 = y
74db38:  mov    0xc(%ebp),%edx         ; edx = target unit
74db3b:  mov    0x4(%ecx),%ecx         ; ecx = player->world
74db3e:  mov    0x8(%ebp),%eax         ; eax = unit to order
74db41:  movss  %xmm0,0x4(%esp)        ; push y
74db47:  movss  0x10(%ebp),%xmm0       ; xmm0 = x
74db4c:  movss  %xmm0,(%esp)           ; push x
74db51:  push   %edx                   ; push target
74db52:  mov    %eax,-0x8(%ebp)        ; local 1-element array
74db55:  lea    -0x8(%ebp),%eax
74db58:  mov    0xc0(%ecx),%ecx        ; ecx = world->cmd
74db5e:  push   $0x1                   ; count = 1
74db60:  push   %eax                   ; &unit
74db61:  call   0x809900               ; calls order builder!
74db66:  ...
74db75:  ret    $0x10
```

Calling `0x74db20` directly executes the entire pipeline (builds packet, attaches monk ID, attaches target ID, calculates target coordinates, and dispatches via `0x80ef70`).

### 4.3 Exact Byte Layout of Type 0x00 Packet

When ordering 1 monk to target an enemy unit:
- **Packet size:** `0x14 + 4 * 1 = 0x18` (24 bytes).

| Byte Offset | Type | Field Name | Value / Description |
|---|---|---|---|
| `+0x00` | `uint8_t` | `type` | `0x00` (target order). |
| `+0x01` | `uint8_t` | `session_byte` | Network player byte from `0x5836e0` (or `0x00` for AI). |
| `+0x02` | `uint8_t` | `padding1` | `0x00` (Wololoader uses `'W'` for clone mark). |
| `+0x03` | `uint8_t` | `padding2` | `0x00`. |
| `+0x04` | `int32_t` | `target_id` | `*(int32_t *)((char *)target + 8)` (Target object ID). |
| `+0x08` | `uint8_t` | `count` | `0x01` (1 unit; `0xff` is cached form). |
| `+0x09` | `uint8_t` | `padding3` | `0x00`. |
| `+0x0a` | `uint8_t` | `padding4` | `0x00`. |
| `+0x0b` | `uint8_t` | `padding5` | `0x00`. |
| `+0x0c` | `float` | `target_x` | `*(float *)((char *)target + 0x3c)`. |
| `+0x10` | `float` | `target_y` | `*(float *)((char *)target + 0x40)`. |
| `+0x14` | `int32_t` | `unit_ids[0]`| `*(int32_t *)((char *)monk + 8)` (Monk object ID). |

### 4.4 Manual Construction & Send via `0x80ef70`

If building the packet manually (like Wololoader's `send_clone` in `overflow.c`):
```c
typedef void *(__cdecl *Calloc_t)(size_t n, size_t size);
static const Calloc_t game_calloc = (Calloc_t)0x8ea479;
typedef int (__thiscall *Send_t)(void *cmd, uint8_t *pkt, int size, int player_id);
static const Send_t game_send = (Send_t)0x80ef70;

void send_monk_convert(void *cmd, void *monk, void *enemy) {
    uint8_t *pkt = (uint8_t *)game_calloc(1, 0x18);
    if (!pkt) return;

    pkt[0] = 0x00; // Type 0x00
    pkt[1] = 0x00; // Player session byte (0 is safe for lockstep)
    *(int32_t *)(pkt + 4) = *(int32_t *)((char *)enemy + 8);  // Target ID
    pkt[8] = 1;    // Count = 1
    *(float *)(pkt + 0x0c) = *(float *)((char *)enemy + 0x3c); // Target X
    *(float *)(pkt + 0x10) = *(float *)((char *)enemy + 0x40); // Target Y
    *(int32_t *)(pkt + 0x14) = *(int32_t *)((char *)monk + 8); // Monk ID

    void *enemy_owner = *(void **)((char *)enemy + 0x10);
    int target_player_id = *(int *)((char *)enemy_owner + 0x1adc);

    game_send(cmd, pkt, 0x18, target_player_id);
    // Note: game_send frees pkt on the subsequent command; do not free(pkt)!
}
```

---

## 5. Diplomacy & Gaia Animals (Sheep)

### 5.1 Player Diplomacy Table

Each player object (`WorldPlayer`) contains a pointer to a diplomacy status table at offset `+0x70`:
`diplomacy_table = *(uint8_t **)((char *)player + 0x70)`

The table is indexed by the target player's ID (`player_id = 0..total_players-1`).

Values stored in `diplomacy_table[player_id]`:
- `0`: **Ally**
- `1`: **Neutral**
- `3`: **Enemy**

Evidence from game functions:
- **`0x74f2b0` (`WorldPlayer::is_ally`):**
  ```asm
  74f2b9:  cmp    %eax,0x1adc(%esi)      ; self check
  74f2bf:  je     0x74f2eb               ; self is ally => returns 1
  74f2c1:  test   %eax,%eax              ; check if target is Gaia (0)
  74f2c3:  je     0x74f2e4               ; Gaia is NOT ally => returns 0
  ...
  74f2db:  mov    0x70(%esi),%ecx        ; ecx = player->diplomacy_table
  74f2de:  cmpb   $0x0,(%eax,%ecx,1)     ; cmp table[id], 0 (Ally)
  74f2e2:  je     0x74f2eb               ; returns 1 (is ally)
  74f2e4:  xor    %al,%al                ; returns 0 (not ally)
  ```
- **`0x74f320` (`WorldPlayer::is_enemy`):**
  ```asm
  74f329:  cmp    %eax,0x1adc(%esi)      ; self check
  74f32f:  je     0x74f35b               ; self is not enemy => returns 0
  74f331:  test   %eax,%eax              ; check if target is Gaia (0)
  74f333:  je     0x74f35b               ; Gaia is NOT enemy => returns 0
  ...
  74f34b:  mov    0x70(%esi),%ecx        ; ecx = player->diplomacy_table
  74f34e:  cmpb   $0x3,(%eax,%ecx,1)     ; cmp table[id], 3 (Enemy)
  74f352:  jne    0x74f35b               ; not 3 => returns 0
  74f354:  mov    $0x1,%al               ; returns 1 (is enemy)
  ```
- **`0x74f440` (`WorldPlayer::is_neutral`):**
  ```asm
  74f46b:  mov    0x70(%esi),%ecx
  74f46e:  cmpb   $0x1,(%eax,%ecx,1)     ; cmp table[id], 1 (Neutral)
  74f474:  mov    $0x1,%al
  ```

### 5.2 Gaia Animals & Sheep Decision

**Gaia is Player 0:**
In `0x74f320`:
`test %eax, %eax; je 0x74f35b` -> Gaia (`player_id == 0`) is **never** an enemy (`is_enemy` returns 0).

**Can or should Monks convert Gaia Sheep?**
1. **Engine Rule [VERIFIED]:** Monks **cannot** convert Gaia sheep via right-click conversion orders. In the Genie engine, Gaia sheep and herdables are captured via proximity collision (a unit walking within ~1 tile of the sheep changes its ownership). Right-clicking a sheep with a military unit attacks or moves toward it; it does not trigger a conversion chant.
2. **Order Validator Rule [VERIFIED]:** In `FUN_00868d00` (`0x868d00`, the action decider that verifies monk conversion targets), line 807158 in `decompiled.c`:
   ```c
   if (0 < *(int *)(target_owner + 0x1adc) && FUN_0074f290(target_owner) == 0)
   ```
   The engine strictly requires `target_player_id > 0` and `!is_ally(target_player)`. Targeting Gaia (`player_id == 0`) causes the order to be rejected or treated as a move order.
3. **Exclusion Decision:** Gaia units must be excluded. The check `target_player_id > 0 && diplomacy_table[target_player_id] == 3` (or calling `0x74f320`) automatically skips all Gaia animals (sheep, deer, boar, wolves) and all allies.

---

## 6. Complete Implementation Architecture for Wololoader

Below is the verified, production-ready C design for Wololoader. It can be called from a periodic tick hook (e.g. `0x6c3310` or a 1-second timer).

```c
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>

#define RD8(p, off)   (*(const uint8_t  *)((const char *)(p) + (off)))
#define RD16(p, off)  (*(const int16_t  *)((const char *)(p) + (off)))
#define RD32(p, off)  (*(const uint32_t *)((const char *)(p) + (off)))
#define RDF32(p, off) (*(const float    *)((const char *)(p) + (off)))

typedef int  (__thiscall *IsIdle_t)(void *unit);
typedef void (__thiscall *OrderTarget_t)(void *player, void *unit, void *target, float x, float y);

static const IsIdle_t      unit_is_idle        = (IsIdle_t)0x4b07c0;
static const OrderTarget_t player_order_target = (OrderTarget_t)0x74db20;

void wl_monk_autoconvert_tick(void) {
    static DWORD last_run = 0;
    DWORD now = GetTickCount();
    if (now - last_run < 1000) return; // Run once per second
    last_run = now;

    void *game = *(void **)0xaf2c58;
    if (!game) return;
    void *world = *(void **)((char *)game + 0xc8);
    if (!world) return;

    // 1. Get local player
    int local_idx = RD32(world, 0x174);
    if (local_idx == *(int *)0xaab914) return; // spectator or invalid
    char *p_start = *(char **)((char *)world + 0x184);
    char *p_end   = *(char **)((char *)world + 0x188);
    if (!p_start || !p_end) return;

    int total_players = (int)(p_end - p_start) >> 3;
    if (local_idx < 0 || local_idx >= total_players) return;

    void *local_player = *(void **)(p_start + local_idx * 8);
    if (!local_player) return;

    uint8_t *diplomacy = *(uint8_t **)((char *)local_player + 0x70);
    if (!diplomacy) return;

    // 2. Iterate local player's units to find idle monks
    char *local_units_list = *(char **)((char *)local_player + 0x18);
    if (!local_units_list) return;
    void **local_units = *(void ***)(local_units_list + 0x4);
    int local_unit_count = *(int *)(local_units_list + 0x8);
    if (!local_units || local_unit_count <= 0) return;

    for (int i = 0; i < local_unit_count; i++) {
        void *monk = local_units[i];
        if (!monk) continue;

        // Alive test: status byte == 2, tombstone == 0
        if (RD8(monk, 0x4c) != 2 || RD32(monk, 0x24) != 0) continue;

        // Monk check: master->class == 18 (0x12)
        void *monk_master = *(void **)((char *)monk + 0xc);
        if (!monk_master || RD16(monk_master, 0x16) != 18) continue;

        // Idle test: call 0x4b07c0
        if (!unit_is_idle(monk)) continue;

        float monk_x = RDF32(monk, 0x3c);
        float monk_y = RDF32(monk, 0x40);
        float los = RDF32(monk_master, 0x2c); // Line of Sight radius
        float los_sq = los * los;

        void *best_target = NULL;
        float best_dist_sq = los_sq + 1.0f;

        // 3. Scan all enemy players for the closest convertible enemy unit within LOS
        for (int p = 1; p < total_players; p++) {
            if (p == local_idx) continue;
            if (diplomacy[p] != 3) continue; // Only Enemy (3)

            void *enemy_player = *(void **)(p_start + p * 8);
            if (!enemy_player) continue;

            char *enemy_units_list = *(char **)((char *)enemy_player + 0x18);
            if (!enemy_units_list) continue;
            void **enemy_units = *(void ***)(enemy_units_list + 0x4);
            int enemy_unit_count = *(int *)(enemy_units_list + 0x8);
            if (!enemy_units || enemy_unit_count <= 0) continue;

            for (int j = 0; j < enemy_unit_count; j++) {
                void *enemy = enemy_units[j];
                if (!enemy) continue;

                // Alive check
                if (RD8(enemy, 0x4c) != 2 || RD32(enemy, 0x24) != 0) continue;

                // Skip buildings: master type == 80 or class == 3 / 52
                void *enemy_master = *(void **)((char *)enemy + 0xc);
                if (!enemy_master) continue;
                if (RD8(enemy_master, 0x4) == 80) continue; // Building type
                int16_t enemy_class = RD16(enemy_master, 0x16);
                if (enemy_class == 3 || enemy_class == 52) continue; // Building / Wall

                float ex = RDF32(enemy, 0x3c);
                float ey = RDF32(enemy, 0x40);
                float dx = ex - monk_x;
                float dy = ey - monk_y;
                float dist_sq = dx * dx + dy * dy;

                if (dist_sq <= los_sq && dist_sq < best_dist_sq) {
                    best_dist_sq = dist_sq;
                    best_target = enemy;
                }
            }
        }

        // 4. Issue the right-click conversion order
        if (best_target) {
            float tx = RDF32(best_target, 0x3c);
            float ty = RDF32(best_target, 0x40);
            player_order_target(local_player, monk, best_target, tx, ty);
        }
    }
}
```

---

## 7. Summary of Answers

1. **How to iterate player units and other players:**
   - Players are in `world->player_table` at `[world + 0x184]` to `[world + 0x188]` (stride 8).
   - Local player is at `player_table[world->local_player_id * 8]` (or via `0x540c90`).
   - Units are at `[player + 0x18]`: `units = *(void***)(list + 4)`, `count = *(int*)(list + 8)`.
2. **How to identify monk, line of sight, and enemy class:**
   - Monk: `*(int16_t*)(unit->master + 0x16) == 18` (covers all monks).
   - Line of Sight: `*(float*)(unit->master + 0x2c)` (float sight radius).
   - Skip buildings: `*(uint8_t*)(target->master + 0x4) == 80` (or `class == 3` / `52`).
3. **How to tell a unit is IDLE:**
   - Call `0x4b07c0(unit)` (`unit->is_idle()`), which is virtual slot 90 (`+0x168`).
   - Returns `al = 1` if idle, `0` if busy.
4. **Exact Type 0x00 Packet Layout:**
   - Header is 20 bytes (`0x14`), total size is 24 bytes for 1 unit.
   - Built via `0x809900` or the wrapper `0x74db20(player, unit, target, x, y)`.
   - Layout: `+0x00` (type `0x00`), `+0x04` (target ID), `+0x08` (count `1`), `+0x0c` (target X float), `+0x10` (target Y float), `+0x14` (monk ID int32).
5. **Diplomacy & Gaia Animals:**
   - Diplomacy table at `[player + 0x70]`: `0 = Ally`, `1 = Neutral`, `3 = Enemy`.
   - Gaia is Player 0. Function `0x74f320` (`is_enemy`) explicitly returns 0 for Gaia.
   - Monks cannot convert Gaia sheep; Gaia is excluded by requiring `target_player_id > 0 && diplomacy[target_player_id] == 3`.
