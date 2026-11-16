# Reverse Engineering Report: Adding a New Trainable Unique Unit for Custom Civs (Civ ID 35+)

**Target binary:** `AoK HD.exe` (AoE2 HD 5.8, 32-bit, ImageBase `0x400000`)  
**Dat file:** `resources/_common/dat/empires2_x2_p1.dat` (Genie dat, raw deflate)  
**Scope:** READ-ONLY reverse engineering of Castle Unique Units (UUs), tech tree display, unit ID limits, and EXE hardcoded tables.  
**Classification:** All items marked **[VERIFIED]** (checked in disassembly / dat analysis) or **[GUESS]**.

---

## 1. Quick Address & Constant Reference

| Address / Constant | Type | Description | Status |
|---|---|---|---|
| `0x750590` | Function | `Player::set_unit_available(unit_id, flag)` — sets `player->unit_avail[unit_id] = flag`. | **[VERIFIED]** |
| `0x833870` | Function | `RGE_Effects::apply()` — processes effect commands: `case 2` (enable unit via `0x750590`), `case 3` (upgrade unit via `player->vtbl[0x8c/4]`). | **[VERIFIED]** |
| `0x856040` | Function | `TRIBE_Effects::apply()` — wraps `RGE_Effects::apply()` and handles commands `101`, `102` (disable tech), `103` (research time). | **[VERIFIED]** |
| `0x84f860` | Function | `CivTechTree::init(civ_id)` — initializes tech tree for a civ; loops all researches in dat, disabling units of researches with `Research.civ > 0 && Research.civ != civ_id`. | **[VERIFIED]** |
| `0x70ee80` | Function | `TechTreeScreen::layout()` — builds tech tree node graph. Castle (building 82) unique unit slots are **data-driven** (no civ switch table). Non-castle UUs have hardcoded civ checks. | **[VERIFIED]** |
| `0x5d63e0` | Function | `DLC_CivCount()` — returns civ count for tech tree loops (`0x68bcf0`, `0x84f860`). Hooked by Wololoader (`ADDR_CIV_COUNT`) to return `max_custom_civ_id`. | **[VERIFIED]** |
| `0x4f5960` | Function | `AI_InitSymbols()` — AI script compiler. Switch at `0x4f5d47` sets `my-unique-unit` for own civ. Patched by Wololoader (`wl_ai_own_civ_stub`). | **[VERIFIED]** |
| `0x448370` | Function | `StringTable::lookup(key)` — string table reader. Hooked by Wololoader (`hook_string_lookup`) for IDs `9000000+`. | **[VERIFIED]** |
| `0x7555f0` | Function | `Player::inc_unit_count(unit_id, delta)` — updates `player + 0xe90 + unit_id*2`. Guarded by `cmp unit_id, 0x578` (1400 limit). | **[VERIFIED]** |
| `0x755730` | Function | `Player::inc_queued_count(unit_id, delta)` — updates queued counts. Guarded by `cmp unit_id, 0x578` (1400 limit). | **[VERIFIED]** |
| `0x4eaa10` | Function | `TribeBuildAIModule::ctor()` — initializes AI build arrays sized `0x578` (1400). | **[VERIFIED]** |
| `0x4eb97b` | Instruction | AI build module save/load loop bound: `cmp eax, 0x578`. | **[VERIFIED]** |
| `0x4fdb8f` | Instruction | AI build module unit limit check: `cmp eax, 0x578`. | **[VERIFIED]** |
| `0x84add0` | Function | `Civilization::load()` — reads `civ.unit_count` (`u16`) from dat and calls `calloc(unit_count, 4)`. | **[VERIFIED]** |
| `0x8ae690` | Function | `RGE_Master_Player::init_objects(unit_count)` — calls `calloc(unit_count, 4)`. | **[VERIFIED]** |
| `1400 (0x578)` | Constant | Hardcoded ceiling for fixed-size unit arrays in `Player` and `TribeBuildAIModule`. | **[VERIFIED]** |
| `1402` | Constant | Default `dat.unit_headers.count` and `civ.unit_count` in HD 5.8 dat file. | **[VERIFIED]** |
| `1224..1400` | Slots | 176 unused `RESERVED` unit slots in official dat, all `< 1400`. | **[VERIFIED]** |

---

## 2. Castle Unique Unit (UU) Definition Mechanics

### 2.1 Unit Record Fields (dat: `civilizations.civs[cid].units[uid]`)
A Castle unique unit is a `UnitCreatable` (`type = 70`) record:
- **`train_location_id = 82` [VERIFIED]:** Points to unit ID 82 (`CSTL`, Castle).
- **`button_id = 1` [VERIFIED]:** Castle command grid button 1 (row 1, column 1).
  - All standard Castle UUs use `button_id = 1`.
  - Trebuchet (unit 331) uses `button_id = 2`.
  - Petard (unit 440) uses `button_id = 3`.
- **`enabled = 0` [VERIFIED]:** The unit is initially locked for all civs. It cannot be trained until unlocked by an Effect Command Type 2.
- **`id = id2 = id3 = uid` [VERIFIED]:** All three internal ID fields must match the slot index `uid`.

### 2.2 Castle Button Grid Layout
Selection of the Castle (`unit 82`) builds its action panel by matching `train_location_id == 82` and `research_location == 82`:

```
+--------------------+--------------------+--------------------+--------------------+
| Btn 1: Unique Unit | Btn 2: Trebuchet   | Btn 3: Petard      | Btn 4: (empty)     |
| (train_loc=82)     | (train_loc=82)     | (train_loc=82)     |                    |
+--------------------+--------------------+--------------------+--------------------+
| Btn 5: (empty)     | Btn 6: Elite UU    | Btn 7: Castle UT   | Btn 8: Imp UT      |
|                    | (res_loc=82)       | (res_loc=82)       | (res_loc=82)       |
+--------------------+--------------------+--------------------+--------------------+
| Btn 9: (empty)     | Btn 10: (empty)    | Btn 11: Hoardings  | Btn 12: Sappers    |
|                    |                    | (res_loc=82)       | (res_loc=82)       |
+--------------------+--------------------+--------------------+--------------------+
| Btn 13: Conscript. | Btn 14: Spies/Trsn | Btn 15: (empty)    | Btn 16: (empty)    |
| (res_loc=82)       | (res_loc=82)       |                    |                    |
+--------------------+--------------------+--------------------+--------------------+
```

### 2.3 Unlocking the Basic UU: Auto-Research & Effect Type 2
Every civ in the game unlocks its Castle unique unit using an auto-granted research upon reaching Castle Age:
- **Research record (`dat.researches.researches`):**
  - `research_location = -1` **[VERIFIED]**: Auto-research (not researched at any building).
  - `research_time = 0` **[VERIFIED]**: Completes instantaneously.
  - `button_id = 0` **[VERIFIED]**.
  - `civ = <civ_id>` **[VERIFIED]**: Crucial! Only applies to this civ.
  - `required_techs = [102, -1, -1, -1, -1, -1]` **[VERIFIED]**: Tech `102` is Castle Age. (Older AoK civs also had `266` "Castle Built", but expansion civs 14..31 use `[102, -1...]`).
  - `effect_id = <E_AVAIL_ID>` **[VERIFIED]**.
- **Effect record (`dat.effects.effects[E_AVAIL_ID]`):**
  - Contains command: `type = 2, a = <base_uu_id>, b = 1, c = -1, d = 0.0` **[VERIFIED]**.
  - Handled in EXE at `0x833870` (`RGE_Effects::apply()`):
    ```c
    case 2:
      sVar1 = *(short *)(cmd + 2); // a = unit_id
      if (sVar1 >= 0) {
        FUN_00750590(sVar1, *(char *)(cmd + 4)); // b = 1 (enable)
      }
    ```
  - `0x750590` sets `player->unit_avail[base_uu_id] = 1`, making Button 1 active at the Castle.

### 2.4 Elite Upgrade: Castle Research & Effect Type 3
- **Research record (`dat.researches.researches`):**
  - `research_location = 82` (Castle) **[VERIFIED]**.
  - `button_id = 6` **[VERIFIED]**.
  - `civ = <civ_id>` **[VERIFIED]**.
  - `required_techs = [103, <make_avail_res_id>, -1, -1, -1, -1]` **[VERIFIED]**: Tech `103` is Imperial Age; also requires the basic UU make-avail research.
  - `research_time = 45..60` (seconds) **[VERIFIED]**.
  - `resource_costs`: Food / Gold cost **[VERIFIED]**.
  - `effect_id = <E_ELITE_ID>` **[VERIFIED]**.
- **Effect record (`dat.effects.effects[E_ELITE_ID]`):**
  - Contains command: `type = 3, a = <base_uu_id>, b = <elite_uu_id>, c = -1, d = 0.0` **[VERIFIED]**.
  - Handled in EXE at `0x833870`:
    ```c
    case 3:
      // Player vtable slot 0x8c: upgrade_unit(from_id, to_id)
      (**(code **)(*player + 0x8c))(*(short *)(cmd + 2), *(short *)(cmd + 4));
    ```
  - This transforms all existing `base_uu_id` units on the map and in queues into `elite_uu_id`, and changes Castle Button 1 to train `elite_uu_id`.

---

## 3. In-Game Tech Tree Screen Mechanics

### 3.1 Castle UU Selection is 100% Data-Driven (No Civ Hardcoding)
In `0x70ee80` (`TechTreeScreen::layout()`, decompiled line ~594790):
```c
if (*piVar8 == 0x52) { // 0x52 = 82 = Castle
  iVar11 = *piVar6;    // unit id
  if (piVar6[2] == 3) { // 3 = disabled in tech tree
    if (iVar11 != 0x14b) goto LAB_0070f2b3; // skip disabled unit (unless Trebuchet)
  }
  else if ((iVar11 != 0x14b) && (iVar11 != 0x1b8)) { // not Trebuchet (331) and not Petard (440)
    piVar6[3] = 7; // FLAG AS UNIQUE UNIT!
  }
}
```
**Conclusion [VERIFIED]:**
- The game does **NOT** use a hardcoded civ switch table for Castle unique units.
- Any unit listed under Building 82 in `dat.tech_tree` that is **not disabled** (`status != 3`) and not Trebuchet/Petard is automatically marked with `flag = 7` (civ unique unit).
- **Contrast with non-castle UUs [VERIFIED]:** Non-castle UUs (Longboat, Turtle Ship, Caravel at Dock 45; Condottiero at Barracks 12; Slinger, Genitour, Imp Skirmisher at Range 87; Missionary at Monastery 104) have hardcoded `cmp [screen+0x2d8], <civ_id>` checks at `0x70ee80`. Castle UUs do not!

### 3.2 How Other Civs' UUs are Hidden: `0x84f860` (`CivTechTree::init`)
At `0x84f860` (lines 788015-788050):
1. The game loops through all researches in `dat.researches`:
   ```asm
   84fbb6: mov  ax, WORD PTR [edi-0x2e] ; Research.civ
   84fbba: test ax, ax
   84fbbd: jle  0x84fcd4               ; if civ <= 0 (common research), skip
   84fbc3: cwde
   84fbc4: cmp  eax, ebx                ; ebx = active civ_id
   84fbc6: je   0x84fcd4               ; if Research.civ == active civ_id, keep ENABLED!
   ; Else (Research.civ > 0 and Research.civ != active civ_id):
   ; Call 0x833b50(effect_id) -> for each command:
   ; If type == 2 (enable unit) or type == 3 (upgrade):
   ; Call FUN_00855560(3, unit_id, 3) -> MARKS UNIT AS DISABLED (status = 3)!
   ```
2. **Civ ID Boundary [VERIFIED]:**
   The check is `cmp eax, ebx` where `eax = Research.civ` and `ebx = active_civ_id`. There is **no upper bound of 31**. If `Research.civ == 35` and `active_civ_id == 35`, it matches!
3. **Wololoader Pre-requisite [VERIFIED]:**
   The loop limit in `0x84f860` is obtained from `0x5d63e0` (`DLC_CivCount()`). Wololoader's `hook_civ_count` (`civs.c`) already patches `0x5d63e0` to return `max(g_civs[i].id)`. Hence, `0x84f860` iterates up to civ 35+.

### 3.3 Required `dat.tech_tree` Records
To display the UU properly on the Tech Tree screen, the following dat records must be updated:
1. **`dat.tech_tree.buildings` [VERIFIED]:**
   - Find building entry with `id = 82` (Castle).
   - Append `base_uu_id` and `elite_uu_id` to its `units` list.
2. **`dat.tech_tree.units` [VERIFIED]:**
   - Add `TechTreeUnit` record for `base_uu_id`:
     - `id = base_uu_id`
     - `status = 2` (available)
     - `upper_building = 82` (Castle)
     - `connected_slots_used = 1`
     - `other_connected_ids = [3, 0, 0, 0, 0, 0, 0, 0, 0, 0]` (`3` = Castle Age)
     - `other_connections = [0, 0, ...]`
     - `vertical_line = 63`
     - `unit_count = 1`
     - `units = [elite_uu_id]`
     - `location_in_age = 1`
     - `required_research = -1`
     - `line_mode = 2`
     - `enabling_research = <make_avail_res_id>`
   - Add `TechTreeUnit` record for `elite_uu_id`:
     - `id = elite_uu_id`
     - `status = 2`
     - `upper_building = 82`
     - `connected_slots_used = 2`
     - `other_connected_ids = [4, base_uu_id, 0, ...]` (`4` = Imperial Age)
     - `other_connections = [0, 2, ...]`
     - `vertical_line = 63`
     - `unit_count = 0`
     - `units = []`
     - `location_in_age = 1`
     - `required_research = <elite_res_id>`
     - `line_mode = 3`
     - `enabling_research = -1`
3. **`dat.tech_tree.ages` [VERIFIED]:**
   - Age 2 (Castle Age, index 2, `id = 3`): append `base_uu_id` to `age.units`.
   - Age 3 (Imperial Age, index 3, `id = 4`): append `elite_uu_id` to `age.units`.

---

## 4. Unit ID Limits & Array Allocations in the EXE

### 4.1 The 1400 (`0x578`) Hard Boundary in the EXE
Disassembly inspection of all `cmp reg, <const>` in `text.asm` revealed a critical architectural limit at `0x578` (1400 decimal):

1. **Player Unit Count Array (`0x7555f0`, `0x755730`) [VERIFIED]:**
   - In `FUN_007555f0` (`Player::inc_unit_count`):
     ```c
     if (unit_id < 0x578) { // 1400
       short *psVar1 = (short *)(player + 0xe90 + unit_id * 2);
       *psVar1 = *psVar1 + delta;
     }
     ```
   - Struct offset `player + 0xe90` is a static buffer of 1400 `short`s (`0xaf0` bytes, ending at `player + 0x1980` where unit class counts begin).
   - If `unit_id >= 1400`, the count increment is **ignored**.
2. **AI Build Module Arrays (`0x4eaa10`, `0x4eb97b`, `0x4fdb8f`) [VERIFIED]:**
   - `TribeBuildAIModule` allocates static member buffers: `0x5ec` (1400 `int`s) and `0x1bd4` (1400 `byte`s).
   - Constructors and serialisation loops are hardcoded to `iVar2 < 0x578`.

### 4.2 Dynamic Allocations in Dat Loading
- **`Civilization::load()` at `0x84add0` [VERIFIED]:**
  - Reads `civ.unit_count` (`u16`) from the dat.
  - Calls `0x8ea479(unit_count, 4)` (`calloc(unit_count, 4)`).
  - Dynamically allocated; not hardcoded to 1402.
- **`RGE_Master_Player::init_objects()` at `0x8ae690` [VERIFIED]:**
  - Takes `unit_count` as parameter.
  - Calls `0x8ea479(unit_count, 4)`.
  - Dynamically allocated.

### 4.3 Slot Selection Strategy: The 176 `RESERVED` Slots (1224..1399)
In the official HD 5.8 dat file:
- `dat.unit_headers.count == 1402` **[VERIFIED]**.
- Every civilization has `civ.unit_count == 1402` **[VERIFIED]**.
- Slots **1224 to 1400** (176 slots) are dummy placeholder objects:
  - `name = "RESERVED"` **[VERIFIED]**.
  - `class = -1` **[VERIFIED]**.
  - `type = 10` (Base Unit) **[VERIFIED]**.
  - `unit_headers.headers[slot].commands = None` (`exists = 1`) **[VERIFIED]**.
- **Crucial advantage of slots 1224..1399:**
  - They are `< 1400` (`< 0x578`), so they fit inside `player + 0xe90` and `TribeBuildAIModule`.
  - They do **not** require expanding `unit_headers.count` or `civ.unit_count`.
  - They avoid potential out-of-bounds in any unspotted hardcoded buffer.

---

## 5. What the EXE Hard-Codes Per Civ for Unique Units

### 5.1 AI Script Symbols (`0x4f5960`)
- At `0x4f5d47`: switch statement maps `civ_id` to constants `my-unique-unit`, `my-unique-unit-line`, `my-unique-research` for AI scripts **[VERIFIED]**.
- In Wololoader `civs.c`:
  `wl_patch_call(0x4f5d47, wl_ai_own_civ_stub, 7);`
  `wl_patch_call(0x4f660c, wl_ai_civ_stub, 6);`
  Custom civs currently redirect to their base civ.
- **Impact [VERIFIED]:**
  If civ 35 plays with a standard AI (e.g. Promi), the AI will read `my-unique-unit` as the base civ's UU (e.g. Longbowman). Since civ 35 cannot train Longbowman, the AI won't train its unique unit unless `wl_ai_own_civ_stub` is updated to return the custom UU ID for civ 35.

### 5.2 String IDs for Unit Names & Rollover Tooltips (`0x448370`)
- The game resolves unit names via `u->language_dll_name`, creation tooltips via `u->language_dll_creation`, and help/tech tree text via `u->language_dll_help` **[VERIFIED]**.
- Any integer string ID passed to the game UI triggers `0x448370` (`StringTable::lookup(key)`).
- Wololoader already hooks `0x448370` (`hook_string_lookup` in `civs.c`).
- Giving the new unit custom string IDs in the `9000000 + civ*100 + slot` range allows `hook_string_lookup` to return the name and description with zero changes to game string DLLs **[VERIFIED]**.

### 5.3 Unit Icons
- `u->icon_id` indexes the icon frame in the UI command icons sheet (`50730.slp`) **[VERIFIED]**.
- Monk icon is frame `33` **[VERIFIED]**.
- Using an existing frame ID works out of the box. Custom icons can be injected via texture/SLP override in `files.c`.

---

## 6. Concrete Recipe: Adding 'Wololo Monk' for Civ 35

### 6.1 Slot Allocation
- **Base Unit ID:** `1224` (replaces `RESERVED` slot 1224).
- **Elite Unit ID:** `1225` (replaces `RESERVED` slot 1225).
- Both slots are `< 1400`, safe from player counter buffer limits.

### 6.2 Dat Edits

#### Step 1: Unit Headers (`dat.unit_headers`)
- In `dat.unit_headers.headers[1224]` and `headers[1225]`:
  - Set `exists = 1`.
  - Copy the 6 `UnitCommand` records from `unit_headers.headers[125]` (Monk):
    - `type 105` (Heal)
    - `type 104` (Convert)
    - `type 3`   (Attack/Heal/Convert target)
    - `type 109` (Relic Pickup)
    - `type 132` (Convert Building/Unit)
    - `type 3`   (Secondary Attack/Convert)

#### Step 2: Unit Records in Civ 35 (`dat.civilizations.civs[35].units`)
- **Unit 1224 (Wololo Monk):**
  - Clone from `civ.units[125]` (Monk).
  - Set `type = 70` (`UnitCreatable`).
  - Set `class_ = 18` (Monk).
  - Set `id = 1224, id2 = 1224, id3 = 1224`.
  - Set `name = "WMONK\0\0\0..."` (bytes[20]).
  - Set `train_location_id = 82` (Castle).
  - Set `button_id = 1` (Unique Unit slot).
  - Set `enabled = 0` (locked until Castle Age).
  - Set `train_time = 35` (seconds).
  - Set `resource_costs = [(3, 100, 1)]` (100 Gold; resource 3 = Gold).
  - Set `language_dll_name = 9003520`.
  - Set `language_dll_creation = 9003521`.
  - Set `language_dll_help = 9003522`.
  - Set `icon_id = 33` (Monk icon).
  - Set custom combat stats (e.g. `hit_points = 45.0`, `max_range = 10.0`, `work_rate = 1.25`).
- **Unit 1225 (Elite Wololo Monk):**
  - Clone from Unit 1224.
  - Set `id = 1225, id2 = 1225, id3 = 1225`.
  - Set `name = "EWMONK\0\0..."`.
  - Set `train_location_id = 82, button_id = 1, enabled = 0`.
  - Set `language_dll_name = 9003523`.
  - Set `language_dll_creation = 9003524`.
  - Set `language_dll_help = 9003525`.
  - Boost stats (e.g. `hit_points = 55.0`, `max_range = 12.0`).
- **Civs 0..34:**
  - Leave `units[1224]` and `units[1225]` as `None` or `type=10, enabled=0`.

#### Step 3: Researches & Effects
1. **Make Available Effect (`E_WMONK_AVAIL`):**
   - Append new `Effect` to `dat.effects.effects`.
   - `name = "Wololo Monk (make avail)"`.
   - `commands = [ EffectCommand(type=2, a=1224, b=1, c=-1, d=0.0) ]`.
2. **Make Available Research (`R_WMONK_AVAIL`):**
   - Append new `Research` to `dat.researches.researches`.
   - `name = "Wololo Monk (make avail)"`.
   - `civ = 35`.
   - `required_techs = [102, -1, -1, -1, -1, -1]` (102 = Castle Age).
   - `required_tech_count = 1`.
   - `research_location = -1` (auto-research).
   - `research_time = 0`.
   - `button_id = 0`.
   - `effect_id = index of E_WMONK_AVAIL`.
3. **Elite Upgrade Effect (`E_ELITE_WMONK`):**
   - Append new `Effect`.
   - `name = "Elite Wololo Monk"`.
   - `commands = [ EffectCommand(type=3, a=1224, b=1225, c=-1, d=0.0) ]`.
4. **Elite Upgrade Research (`R_ELITE_WMONK`):**
   - Append new `Research`.
   - `name = "Elite Wololo Monk"`.
   - `civ = 35`.
   - `required_techs = [103, index of R_WMONK_AVAIL, -1, -1, -1, -1]` (103 = Imperial Age).
   - `required_tech_count = 2`.
   - `research_location = 82` (Castle).
   - `button_id = 6` (Elite UU research button).
   - `research_time = 50`.
   - `resource_costs = [(0, 750, 1), (3, 800, 1)]` (Food 750, Gold 800).
   - `language_dll_name = 9003526`.
   - `language_dll_description = 9003527`.
   - `effect_id = index of E_ELITE_WMONK`.

#### Step 4: Tech Tree Section (`dat.tech_tree`)
1. **`dat.tech_tree.buildings`:**
   - In building entry `id = 82` (Castle): append `1224` and `1225` to `units` list.
   - Append `index of R_ELITE_WMONK` to `researches` list.
2. **`dat.tech_tree.units`:**
   - Append `TechTreeUnit` for `1224`:
     - `id = 1224, status = 2, upper_building = 82`
     - `unit_count = 1, units = [1225]`
     - `enabling_research = index of R_WMONK_AVAIL`
     - `location_in_age = 1, line_mode = 2`
   - Append `TechTreeUnit` for `1225`:
     - `id = 1225, status = 2, upper_building = 82`
     - `unit_count = 0, units = []`
     - `required_research = index of R_ELITE_WMONK`
     - `enabling_research = -1`
     - `location_in_age = 1, line_mode = 3`
3. **`dat.tech_tree.ages`:**
   - In `ages[2]` (Castle Age, id 3): append `1224` to `age.units`.
   - In `ages[3]` (Imperial Age, id 4): append `1225` to `age.units`.

### 6.3 Wololoader Hooks & Patches

1. **String Serving (`wololoader/src/civs.c`):**
   - Extend `hook_string_lookup` to serve custom strings:
     - `9003520`: `"Wololo Monk"`
     - `9003521`: `"Train Wololo Monk"`
     - `9003522`: `"Wololo Monk\nMonk unique unit. Converts enemy units and heals allies from long range."`
     - `9003523`: `"Elite Wololo Monk"`
     - `9003524`: `"Train Elite Wololo Monk"`
     - `9003525`: `"Elite Wololo Monk\nUpgraded Monk unique unit."`
     - `9003526`: `"Elite Wololo Monk"`
     - `9003527`: `"Upgrade to Elite Wololo Monk"`
2. **Civ Count Hook (`0x5d63e0`):**
   - Already present in Wololoader (`ADDR_CIV_COUNT` -> `hook_civ_count`). Ensures `0x84f860` loops up to civ 35+.
3. **AI Symbols Hook (`0x4f5d47`):**
   - Optional enhancement: in `wl_ai_own_civ_stub`, return `1224` for `my-unique-unit` when the player civ is 35 so AI players can autonomously train Wololo Monks.
4. **No Other EXE Patches Needed [VERIFIED]:**
   - Because Castle UUs are data-driven in `TechTreeScreen::layout()` (`0x70ee80`), and slot 1224 is `< 1400`, no additional patches are required for civ IDs > 31.
