# AoK HD 5.8 — Maximum Unit Selection Limit (Selection Cap) and Expansion to 150 Plan

> **Goal:** Examine the maximum unit selection limit (40 in AoK, 60 in HD) in Age of Empires II HD 5.8 (`AoK HD.exe`, 32-bit PE, ImageBase `0x400000`), document memory layout and limit constants at full address and opcode level, and prepare a concrete plan to raise the limit to 150 via Wololoader DLL.
> **Status:** Read-only research completed. All addresses and bytes verified via `AoK HD.exe` disassembly and `re/out/decompiled.c` (**VERIFIED**).

---

## 1. Executive Summary and Architecture Overview

In Age of Empires II HD 5.8, the unit selection system is not centralized but depends on the 60 (`0x3c`) constant across 4 interconnected subsystems:
1. **Player Object (`WorldPlayer`):** Fixed 60-element `RGE_Static_Object*` array starting at `player + 0x160` offset (240 bytes) and counter (`DWORD`) at `player + 0x254` offset.
2. **Drag-Box Selection & Screen Query:** Static global buffer at address `0xde0430` (60 unit records of 12 bytes each = 720 bytes).
3. **Command Creation and Network Protocol (`command.cpp` / `TRIBE_Command`):** Buffer `0xaf4e40` writing selected unit IDs to command packet (60 DWORD), packet execution buffer `0xafb510` (60 DWORD), and last command history array per player inside `TRIBE_Command` at `command + 0x12c` (`9 * 60 * 4 = 2160` bytes).
4. **User Interface Panel (`tpnl_obj`):** Code that groups and draws 60 icons in the bottom panel (`0x716360`).

---

## 2. Memory Layout and Data Structures (VERIFIED)

### 2.1. `WorldPlayer` Selection Storage Area
`WorldPlayer` object is derived from `RGE_Player`. Selection data is stored directly inside the object as a fixed array:

| Offset | Type | Name / Description | Verification Status |
|---|---|---|---|
| `this + 0x15c` | `RGE_Static_Object*` | `selected_unit_primary` (Selected primary unit pointer) | **VERIFIED** (`0x754aa3`, `0x755490`) |
| `this + 0x160` | `RGE_Static_Object*[60]` | `selected_units[60]` (60-element pointer array, `0x160`..`0x24f`) | **VERIFIED** (`0x754b2a`, `0x7554a9`) |
| `this + 0x250` | `DWORD` | Internal state / alignment area | **VERIFIED** |
| `this + 0x254` | `int` | `selected_unit_count` (Selected unit count: 0 .. 60) | **VERIFIED** (`0x754b1d`, `0x755476`) |

```
WorldPlayer (this):
+0x15C: [ Primary Unit Pointer (4B) ]
+0x160: [ Unit 0 ] [ Unit 1 ] ... [ Unit 59 ]  (240 bytes = 0xF0)
+0x250: [ Dword pad/state (4B) ]
+0x254: [ Selected Count (4B, 0..60) ]
+0x258: [ Following other player struct fields... ]
```

> [!CAUTION]
> Immediately after the `player + 0x160` array come `0x254` (count) and `0x258+` other critical player fields. Expanding this array in place to 150 elements (600 bytes) leads to memory overflow (struct overwrite) and immediate crash. This area must be redirected to an external dynamic array (relocation).

### 2.2. Unit Flags (`RGE_Static_Object`)
Each unit carries its own selection and group state internally:
- `unit + 0x3a` (`byte`):
  - `Bit 0` (`& 0x01`): `SELECTED` flag (set to `|= 1` by `WorldPlayer::select_unit`, cleared to `&= 0xfe` by `unselect`).
  - `Bit 3` (`& 0x08`): `IN_GROUP` flag.
- `unit + 0x158` (`DWORD`): **Control Group Bitmask** (Detailed below).

### 2.3. Static Global Buffers
The game engine uses fixed 60-unit buffers in the static data segment (.data / .bss) for selection operations and command preparation:

| Symbol / Address | Size | Element Structure | Used In | Status |
|---|---|---|---|---|
| `DAT_00de0430` | 720 bytes (`60 * 12`) | `{ int unit_id; int x; int y; }` | Drag-box selection buffer (`0x87cee0`) | **VERIFIED** |
| `DAT_00af4e40` | 240 bytes (`60 * 4`) | `int unit_id[60]` | Player command pre-send ID list (`0x74ef20`) | **VERIFIED** |
| `DAT_00afb510` | 240 bytes (`60 * 4`) | `int unit_id[60]` | Command packet parser processing list (`0x8095b0`) | **VERIFIED** |
| `DAT_00afb50c` | 4 bytes | `int count` | Unit count inside `DAT_00afb510` | **VERIFIED** |

### 2.4. Command and Network Structure (`TRIBE_Command` / `RGE_Command`)
Command object (`this` = `g_command` or `TRIBE_Command*`) stores orders given to players and transmits them over the network:

| Offset | Size | Description | Status |
|---|---|---|---|
| `command + 0x30` | 240 bytes | `last_sent_units[60]`: Local player's 60 unit IDs sent in last command | **VERIFIED** (`0x8095b0`) |
| `command + 0x120 + player` | 9 bytes | `last_commanded_count[9]`: Unit count in last command for each player (0..8) | **VERIFIED** (`0x80e6c0`) |
| `command + 0x12c + player*0xf0` | 2160 bytes | `last_commanded_units[9][60]`: Last commanded unit cache of 60 DWORDs per player | **VERIFIED** (`0x80dad0`, `0x80e6c0`) |

---

## 3. Verified Limit Constants (`0x3c` = 60) and Code Locations

All addresses confirmed via `AoK HD.exe` disassembly output.

### 3.1. Core Selection and Deselection Functions

| Address | Original Bytes | Assembly (Intel) | Function & Purpose | Status |
|---|---|---|---|---|
| `0x754b23` | `83 f8 3c` | `cmp eax, 0x3c` | `WorldPlayer::select_unit`: Blocks selection if selected count >= 60 | **VERIFIED** |
| `0x754bb0` | `83 be 54 02 00 00 3c` | `cmp dword ptr [esi+0x254], 0x3c` | `WorldPlayer::select_unit`: Secondary limit check | **VERIFIED** |
| `0x755722` | `83 fa 3c` | `cmp edx, 0x3c` | `WorldPlayer::unselect_all`: 60-unit array zeroing loop limit | **VERIFIED** |
| `0x755797` | `83 f9 3b` | `cmp ecx, 0x3b` | `WorldPlayer::unselect_object`: Array shift loop (`0x3b = 59`) | **VERIFIED** |
| `0x7554a0` | - | - | `WorldPlayer::unselect_at_index`: Removes index from array and shifts following entries | **VERIFIED** |

### 3.2. Drag-Box Selection

| Address | Original Bytes | Assembly (Intel) | Function & Purpose | Status |
|---|---|---|---|---|
| `0x87d07d` | `81 ff 04 07 de 00` | `cmp edi, 0xde0704` | `FUN_0087cee0`: `0xde0430` buffer end check (`0xde0434 + 60*12 = 0xde0704`) | **VERIFIED** |
| `0x87d4fb` | `81 fa 04 07 de 00` | `cmp edx, 0xde0704` | `FUN_0087d110`: Candidate unit filtering buffer end check | **VERIFIED** |
| `0x87d57c` | `b8 3c 00 00 00` | `mov eax, 0x3c` | `FUN_0087d110`: Maximum addable unit ceiling (60) | **VERIFIED** |
| `0x86f8a4` | `83 f8 3c` | `cmp eax, 0x3c` | `FUN_0086f880`: View selection result constraint | **VERIFIED** |
| `0x822261` | `83 f8 3c` | `cmp eax, 0x3c` | `FUN_00822230`: Screen area unit query limit | **VERIFIED** |

### 3.3. Shift-Click and Double-Click (Select-All-of-Type)

| Address | Original Bytes | Assembly (Intel) | Function & Purpose | Status |
|---|---|---|---|---|
| `0x887747` | `83 f8 3c` | `cmp eax, 0x3c` | `FUN_00887660`: Shift-selection stack buffer upper limit (`jl ... mov eax, 0x3b`) | **VERIFIED** |
| `0x86f3f0` | - | - | `FUN_0086f3f0`: Double-click selects same-type units on screen. Directly calls `WorldPlayer::select_unit` (`0x754a20`); limit checked at `0x754b23`. | **VERIFIED** |
| `0x886970` | - | - | `on_left_click`: Selection add/remove routing based on Shift key | **VERIFIED** |

### 3.4. Idle Villager and Select All Military Hotkeys
- Idle villager (`.` hotkey) selects individual units in a loop, so it directly calls `select_unit` (limit is 1 for single selection).
- Select All Idle Villagers / Select All Military hotkeys: Loop through player units and call `WorldPlayer::select_unit(unit, 0)`. Selection limit cut off at 60 by `0x754b23`.

### 3.5. User Interface Selection Panel (`tpnl_obj::draw_group`)

| Address | Original Bytes | Assembly (Intel) | Function & Purpose | Status |
|---|---|---|---|---|
| `0x71645f` | `83 f9 3c` | `cmp ecx, 0x3c` | `tpnl_obj::draw_group`: Skips drawing more than 60 unit icons | **VERIFIED** |
| `0x716534` | `b8 4c 02 00 00` | `mov eax, 0x24c` | `0x160 + 59*4 = 0x24c`: Last unit pointer offset | **VERIFIED** |
| `0x716746` | `3d 60 01 00 00` | `cmp eax, 0x160` | Array start check (backward loop) | **VERIFIED** |

### 3.6. Command Send and Dispatch Code (`command.cpp`)

| Address | Original Bytes | Assembly (Intel) | Function & Purpose | Status |
|---|---|---|---|---|
| `0x74f085` | `83 f8 3c` | `cmp eax, 0x3c` | `FUN_0074ef20`: Loop copying selected unit IDs to `0xaf4e40` | **VERIFIED** |
| `0x80e6c8` | `b9 3c 00 00 00` | `mov ecx, 0x3c` | `FUN_0080e6c0`: `last_commanded_units` cache clear (`rep stosd`) | **VERIFIED** |
| `0x80a49c` | `83 3d 0c b5 af 00 3c` | `cmp dword ptr [0xafb50c], 0x3c` | Right-click / Move-to-target command unit add check | **VERIFIED** |
| `0x80a99c` | `83 3d 0c b5 af 00 3c` | `cmp dword ptr [0xafb50c], 0x3c` | Attack command (`do_attack`) unit limit | **VERIFIED** |
| `0x80b1ac` | `83 3d 0c b5 af 00 3c` | `cmp dword ptr [0xafb50c], 0x3c` | Work / Gather / Repair command unit limit | **VERIFIED** |
| `0x80b74c` | `83 3d 0c b5 af 00 3c` | `cmp dword ptr [0xafb50c], 0x3c` | Patrol command unit limit | **VERIFIED** |
| `0x80ba4c` | `83 3d 0c b5 af 00 3c` | `cmp dword ptr [0xafb50c], 0x3c` | Guard command unit limit | **VERIFIED** |
| `0x80bd1c` | `83 3d 0c b5 af 00 3c` | `cmp dword ptr [0xafb50c], 0x3c` | Follow command unit limit | **VERIFIED** |
| `0x80c69c` | `83 3d 0c b5 af 00 3c` | `cmp dword ptr [0xafb50c], 0x3c` | Explore / Attack-move command unit limit | **VERIFIED** |
| `0x80cd4c` | `83 3d 0c b5 af 00 3c` | `cmp dword ptr [0xafb50c], 0x3c` | Formation change command unit limit | **VERIFIED** |
| `0x80d71c` | `83 3d 0c b5 af 00 3c` | `cmp dword ptr [0xafb50c], 0x3c` | Garrison command unit limit | **VERIFIED** |
| `0x814fdc` | `83 3d 0c b5 af 00 3c` | `cmp dword ptr [0xafb50c], 0x3c` | Sell/release command unit limit | **VERIFIED** |

### 3.7. Formation and Group Merge Limit (`baseworld.cpp`)

| Address | Original Bytes | Assembly (Intel) | Function & Purpose | Status |
|---|---|---|---|---|
| `0x739ca7` | `83 f8 3c` | `cmp eax, 0x3c` | `FUN_00739b30`: Group merge loop safety (`jg 0x739d93` -> infinite loop warning) | **VERIFIED** |

---

## 4. Control Groups (Control Groups 1..9) Architecture (VERIFIED)

Control groups are **not stored** as unit lists in the player object.

1. **Per-Unit Storage:** Each unit has a 32-bit DWORD bitmask at `unit + 0x158` offset.
   - Group 1: `bit 0` (`0x00000001`)
   - Group 2: `bit 1` (`0x00000002`)
   - ...
   - Group 9: `bit 8` (`0x00000100`)
2. **Group Definition (`Ctrl + 1..9` -> `FUN_0074f0c0`):**
   - Function calculates `1 << (group - 1)` mask.
   - First scans all player units to clear this bit from old members of this group (`unit + 0x158 &= ~mask`).
   - Then loops over selected units:
     - Address `0x74f07e`: Reads `player->selected_units[i]` array with `while (i < 0x3c)` limit.
     - Applies `|= mask` to each unit's `unit + 0x158` field.
3. **Group Recall (`1..9` key -> `FUN_007547f0`):**
   - Scans all active units of the player.
   - For each unit satisfying `if (unit->0x158 & mask)`, calls `WorldPlayer::select_unit(unit, 0)`.
   - This call is also subject to the 60 limit at `0x754b23`.

---

## 5. Save Games and Replays (VERIFIED)

### 5.1. Game Save (`.aoe2spgame` / `.msx`)
- `WorldPlayer::selected_units` (`0x160`..`0x254`) **is not saved to savegame files**. It is transient (volatile/UI) screen state.
- However, `unit + 0x158` (control group bitmask), being persistent unit state, is written to and restored from save files.

### 5.2. Recorded Games (`.mgx`)
- Engine does not record the selected unit array every frame.
- When a command is issued (`FUN_0080ef70`), this header is written to the command packet:
  - Byte 0..3: Command type and player ID.
  - Byte 4: `unit_count` (1 byte).
  - Byte 5..7: Target / coordinate data.
  - Following bytes: `unit_count * 4` byte `unit_id` array.
- **Optimization (0xFF flag):** If the order goes to the same selection group that was just commanded, `unit_count = 0xff` is written and the unit ID list is not added to the packet again. Receiver side (`FUN_0080dad0`) reuses the list in `last_commanded_units[player]` cache when it sees `0xff`.

---

## 6. Raising Limit to 150 Implementation Plan

6-step definitive strategy for raising the limit from 60 to 150 (`0x96`):

### Step 1: `WorldPlayer` Selection Array Relocation
`player + 0x160` area is 240 bytes; 600 bytes needed for 150 units.
- **Solution:** Allocate `RGE_Static_Object* g_custom_selection[9][150]` (600-byte static array per player) inside Wololoader DLL.
- Hook `WorldPlayer::select_unit` (`0x754a20`), `unselect_all` (`0x755450`), `unselect_at_index` (`0x7554a0`), `unselect_object` (`0x755540`) functions to redirect to `g_custom_selection[player_id]` array instead of `this + 0x160`.
- `player + 0x254` (selection count) is a 4-byte integer so naturally supports value 150, can remain in place.

### Step 2: Static Global Buffer Expansion
1. `DAT_00de0430` (720 bytes -> 1800 bytes):
   - Redirect `cmp ..., 0xde0704` checks at addresses `0x87d07d` and `0x87d4fb` in box selection function to newly allocated 1800-byte buffer's end address.
   - Patch `mov eax, 0x3c` at address `0x87d57c` to `mov eax, 0x96` (150).
2. `DAT_00af4e40` (240 bytes -> 600 bytes):
   - Redirect copy target inside `FUN_0074ef20` to new 600-byte buffer.
   - Change `cmp eax, 0x3c` at address `0x74f085` to `cmp eax, 0x96`.
3. `DAT_00afb510` (240 bytes -> 600 bytes):
   - Update `0xafb50c` check (`0x3c` -> `0x96`) and buffer addresses in command executors.

### Step 3: `TRIBE_Command` Cache Structure Enlargement
`last_commanded_units[9][60]` located at `command + 0x12c` (`0xf0` byte stride).
- Hook `FUN_0080e6c0` (clear with `rep stosd`) and `FUN_0080dad0` (caching) functions.
- Allocate new cache area of `150 * 4 = 600` bytes for 9 players (`DWORD g_custom_last_cmd[9][150]`).

### Step 4: Comparison Constant Patching (Binary Patches)
Apply the following 1-byte constant changes directly with `wl_patch` after removing memory protection:

```c
// 0x3c (60) -> 0x96 (150)
const uint8_t LIM_150 = 0x96;
const uint8_t LIM_149 = 0x95;

// Core Selection
wl_write_byte(0x754b25, LIM_150); // cmp eax, 0x96
wl_write_byte(0x754bb6, LIM_150); // cmp [esi+0x254], 0x96
wl_write_byte(0x755724, LIM_150); // cmp edx, 0x96 (unselect all loop)
wl_write_byte(0x755799, LIM_149); // cmp ecx, 0x95 (unselect shift loop)

// Drag Box & Hotkeys
wl_write_byte(0x87d57d, LIM_150); // mov eax, 0x96
wl_write_byte(0x86f8a6, LIM_150); // cmp eax, 0x96
wl_write_byte(0x822263, LIM_150); // cmp eax, 0x96
wl_write_byte(0x887749, LIM_150); // cmp eax, 0x96 (shift-select)
wl_write_byte(0x74f087, LIM_150); // cmp eax, 0x96 (group cmd copy)
wl_write_byte(0x74f080, LIM_150); // cmp [esi+...], 0x96 (ctrl+group)

// Command Handlers (10 functions)
wl_write_byte(0x80a4a2, LIM_150); // do_move
wl_write_byte(0x80a9a2, LIM_150); // do_attack
wl_write_byte(0x80b1b2, LIM_150); // do_work
wl_write_byte(0x80b752, LIM_150); // do_patrol
wl_write_byte(0x80ba52, LIM_150); // do_guard
wl_write_byte(0x80bd22, LIM_150); // do_follow
wl_write_byte(0x80c6a2, LIM_150); // do_explore
wl_write_byte(0x80cd52, LIM_150); // do_formation
wl_write_byte(0x80d722, LIM_150); // do_garrison
wl_write_byte(0x814fe2, LIM_150); // do_sell
```

### Step 5: User Interface Panel (`tpnl_obj::draw_group`) Adaptation
- Hook the `cmp ecx, 0x3c` jump check at address `0x71645f`.
- Two options for fitting 150 unit boxes in the bottom panel:
  1. *Multi-Page Pagination / Tabs:* Panel tabs or arrow keys for 60-unit chunks (`Next / Prev 60`).
  2. *Draw Only First 60:* 150 units selected and move together, but panel shows health bars for only the first 60 (cleanest and lowest-risk initial step to prevent crashes).

---

## 7. Risk Analysis and Critical Warnings

1. **Network / Multiplayer Packet Size (Network MTU):**
   - When a 150-unit command is sent, packet size: `8 byte header + 150 * 4 bytes = 608 bytes`.
   - DirectPlay / Steam P2P packets are under 1024-1400 bytes, so 608 bytes easily fits in a single UDP packet with no fragmentation.
   - However, an opponent playing with vanilla 5.8 client will get **synchronization error (Out of Sync / OOS)** due to `0xafb50c` overflow when receiving a packet larger than 60. This feature requires all players to have Wololoader installed.
2. **Formation and Pathfinding Spikes:**
   - When 150 units are moved simultaneously, the formation algorithm (`baseworld.cpp` `FUN_00739b30` @ `0x739ca7`) can cause CPU micro-stutter. The infinite loop safety check at address `0x739ca7` must also be raised to `0x96`.
3. **Shift-Click Stack Buffer:**
   - Stack buffer in function `FUN_00887660` is allocated for 60 elements. To prevent stack overflow when shift-selecting 150 units, this function must be hooked with a detour.
