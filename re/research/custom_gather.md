# Custom Gatherable Resources in Age of Empires II HD 5.8 — Research & Implementation Plan

> Research notes for Wololoader (`AoK HD.exe` 5.8, 32-bit, ImageBase `0x400000`, `empires2_x2_p1.dat`).
> Goal: allow mods to define a gatherable Gaia map object (e.g. "Crystal Deposit") holding a custom resource (ID `211+`) that villagers can right-click, gather with animation, carry, and deposit at a Town Center, Mining Camp, or mod building.

---

## Executive Summary

**Villagers in AoE2 HD 5.8 can natively gather, carry, and deposit custom resources (`id >= 211`) with zero crashes and almost zero engine patches.**

- **Dat side**: Adding a gatherable custom resource requires:
  1. One new **Gaia-only** deposit unit (cloned from Gold Mine `66`, `type=10`, `command_attribute=1`, `resource_storages[0]=(res_id, amount, 0)`).
  2. Either **two new task-swap miner units** (male + female, cloned from Gold Miners `579`/`581`, `type=70`, `class_=4`, `villager_mode=1`/`2`, `enabled=0`, custom `drop_site[2]`, custom `work_rate`/`resource_capacity`) **or** reusing `579`/`581` directly.
  3. One `UnitCommand` (`type=5` Gather) in `UnitHeader[miner_id].commands` pointing to the new deposit unit ID, `resource_in=res_id`, `resource_out=-1` (or `res_id`), and `resource_productivity_multiplier=-1` (defaults to `1.0` in engine).
- **Engine side**:
  - **Core gather & deposit (`RGE_Action_Gather`, task type `5`) is 100% compatible with `id >= 211`.** Contrary to intuition, task type `5` does *not* use `TRIBE_Action_Gather` (which is only for task type `122`, Pillaging/Loot); it uses `RGE_Action_Gather`, which reads `master->drop_site[0..1]` and `unit->attribute_type_held` generically with no 0..3 hardcoding.
  - **One major UI gap**: `TRIBE_Panel_Object::draw` (`0x713f00`) has four hardcoded switches on `obj->attribute_type_held (+0x50)` capped at `0x38`/`0x39`. Without a Wololoader overlay/hook, **neither the deposit's remaining amount nor a villager's carried amount will appear in the bottom-left unit info panel** (it does not crash; it just skips drawing that row).
  - **Minor non-crashing quirks**: Garrisoning a carrying villager into a TC (`RGE_Action_Enter`, `0x4a9180`) skips auto-deposit for `id >= 211`; the "Drop Off" button skips the villager's confirmation voice line (`0x73e820`) but still executes the drop-off; cumulative resource stats/score/AI/Diplomacy UI only track resources `0..3`; the network tribute packet stores resource ID as `uint8` (`0..255`, wraps at `>= 256`).
- **RMS side**: Random map scripts **can** spawn any custom Gaia unit ID (including `1224..1400`) via `#const NAME <id>` followed by `create_object NAME`. Raw numeric literals (`create_object 1250`) fail because `create_object` looks up a `#const` BST without an `atoi` fallback.

---

## 1. Dat Side Analysis (`empires2_x2_p1.dat`)

All fields below were dumped directly from `resources/_common/dat/empires2_x2_p1.dat` (32 civs: `0` Gaia, `1..31` playable; `211` resources `0..210`; `1402` unit slots/headers) using `wololoader/tools/datfile.py`.

### 1.1 Map Resource Objects: Gold Mine (`66`) & Stone Mine (`102`)

Resource deposits exist **only in Civ 0 (Gaia)** — `civs[1..31].units[66]` and `[102]` are `None`. (The unused Ore Mine `352` follows the exact same layout with resource `56`.)

| `datfile.py` field | Gold Mine (`66`) | Stone Mine (`102`) | Ore Mine (`352`, unused) | Purpose / Notes |
|---|---|---|---|---|
| `type` | `10` (`Unit`) | `10` (`Unit`) | `10` (`Unit`) | Static eye-candy / resource object (base class). |
| `name` | `b"GOLDM\0"` | `b"STONM\0"` | `b"OREMN\0"` | Internal name. |
| `id` / `id2` / `id3` | `66` / `66` / `66` | `102` / `102` / `102` | `352` / `352` / `352` | Must all match the allocated unit slot ID. |
| `language_dll_name` | `5129` | `5082` | `5352` | Display name string ID (use Wololoader string range `9000000+` for custom deposits). |
| `class_` | `32` (Gold Mine) | `8` (Stone Mine) | `48` (Ore Mine) | Object class. Can reuse `32` (so class-based rules treat it as a mine) since tasks target exact `unit_id`. |
| `standing_graphic` | `[2421, -1]` | `[1592, -1]` | `[...]` | Main visual graphic ID. |
| `hit_points` | `0` | `0` | `0` | Indestructible; depleted only by gathering resource to `0`. |
| `enabled` | `1` | `1` | `1` | Must be `1`. |
| `visible_in_fog` | `1` | `1` | `1` | Keeps mine visible in explored fog of war. |
| `size_radius` | `[0.5, 0.5]` | `[0.5, 0.5]` | `[0.5, 0.5]` | Tile collision footprint (1x1 tile). |
| `selection_radius` | `[0.5, 0.5]` | `[0.5, 0.5]` | `[0.5, 0.5]` | Clickable box. |
| `editor_radius` | `[0.5, 0.5]` | `[0.5, 0.5]` | `[0.5, 0.5]` | Scenario editor footprint. |
| `terrain_restriction` | `8` | `8` | `8` | Land-only placement (matches RMS terrain validation). |
| `placement_mode` | `5` | `5` | `5` | Resource deposit placement. |
| `interaction_mode` | `4` | `4` | `4` | Selectable & targetable. |
| `minimap_mode` | `1` | `1` | `1` | Drawn on minimap. |
| `minimap_color` | `65` (yellow) | `131` (grey) | `...` | Palette index for minimap dot (customizable). |
| `command_attribute` | `1` | `1` | `1` | `interface_kind = 1` (**Resource object** — controls panel layout and Gaia resource handling). |
| `selection_mask` | `1` | `1` | `1` | Selection behavior. |
| `selection_shape_type` | `2` | `2` | `2` | Rectangular tile outline. |
| `selection_shape` | `1` | `1` | `1` | Selection outline style. |
| `selection_effect` | `2` | `2` | `2` | Selection feedback. |
| `selection_sound` | `...` | `260` | `...` | Sound on click. |
| `unknown6` / `unknown7` / `unknown8` | `1` / `1` / `4` | `1` / `1` / `3` | `...` | `unknown8` is `resource_decay` / resource category hint (clone from Gold Mine `66`). |
| `resource_storages[0]` | `(type=3, amount=800.0, store_mode=0)` | `(type=2, amount=350.0, store_mode=0)` | `(type=56, amount=400.0, store_mode=0)` | **Core gather payload!** `store_mode=0` initializes `unit->attribute_type_held (+0x50)` and `unit->attribute_amount_held (+0x48)` on spawn (`0x8b5640`). |
| `resource_storages[1..2]` | `(-1, 0.0, 0)` | `(-1, 0.0, 0)` | `(-1, 0.0, 0)` | Unused slots. |

> **Important**: `UnitHeaders.headers[deposit_id]` must have `exists = 0, commands = None` (matching `66` and `102`).

### 1.2 Villagers (`83` / `293`) vs. Miner Task-Swap Units (`579`/`581`, `124`/`220`)

In AoE2, base villagers (`83` `VMBAS`, `293` `VFBAS`) do **not** hold gather tasks for gold, stone, wood, or food. Instead, all villager variants share a **`villager_mode`** (`task_swap_group`, `uint8` at `RGE_Master_Action_Object + 0x12c`):
- `villager_mode = 1`: Male villager group (`83` base, `579` gold miner, `124` stone miner, `123` lumberjack, `259` farmer, `120` forager, `122` hunter, `592` shepherd, `56` fisherman, `118` builder, `156` repairer).
- `villager_mode = 2`: Female villager group (`293` base, `581` gold miner, `220` stone miner, `218` lumberjack, `214` farmer, `354` forager, `216` hunter, `590` shepherd, `57` fisherman, `212` builder, `222` repairer).

#### Unit Properties (Civ 1; present identically on all playable civs `1..31`, absent on Gaia)

| Field | Base Villager (`83`/`293`) | Gold Miner (`579`/`581`) | Stone Miner (`124`/`220`) |
|---|---|---|---|
| `type` | `70` (`Unit70`) | `70` (`Unit70`) | `70` (`Unit70`) |
| `class_` | `4` (Civilian) | `4` (Civilian) | `4` (Civilian) |
| `enabled` | `1` | `0` | `0` |
| `hide_in_editor` | `0` | `1` | `1` |
| `villager_mode` (`+0x12c`) | `1` / `2` | `1` / `2` | `1` / `2` |
| `command_attribute` | `3` | `3` | `3` |
| `resource_capacity` (`+0x84`) | `10` | `10` | `10` |
| `work_rate` (`+0x124`) | `1.0` | `0.38` | `0.36` |
| `drop_site` (`+0x128`, `+0x12a`) | `[109, 68]` (TC, Mill) | `[109, 584]` (TC, Mining Camp) | `[109, 584]` (TC, Mining Camp) |

#### Task Lists (`dat.unit_headers.headers[id].commands`)

1. **Base Villagers (`83` / `293`)**:
   - Task `type=7` (Attack), `type=3` (Garrison), `type=109` (Unload), `type=122` (Loot/Pillage — unused in normal play), `type=110` (Hunt predator).
   - **No Gold or Stone gather task** — right-clicking a Gold Mine triggers a task-swap lookup across `villager_mode`.

2. **Gold Miner (`579` Male / `581` Female)** — primary gather task (`commands[2]`):
   - `one=1`, `id=2`, `unknown1=0` (`is_default=0`), `type=5` (**Gather**)
   - `class_id=-1`, `unit_id=66` (Gold Mine), `terrain_id=-1`
   - `resource_in=3` (Gold), `resource_productivity_multiplier=47` (Gold Mining Productivity), `resource_out=-1`, `resource=-1`
   - `work_rate_multiplier=1.0`, `execution_radius=0.0`, `extra_range=0.0`
   - `unknown4=1` (`auto_search_targets`), `unknown5=3.0` (`search_wait_time`)
   - `selection_enabler=0`, `unknown7=0`, `unknown8=0`, `unknown9=0`
   - `unknown10=3` (`target_diplomacy`: 3 = Gaia only), `unknown11=1` (`carry_check` / resource-level check), `unknown12=0`
   - `graphics`:
     - Male (`579`): `[-1, 1606, -1, 2210, -1, -1]` (proceeding=-1, working=1606 mining, working2=-1, carrying=2210 gold bag, resource_gather_sound=-1, resource_deposit_sound=-1)
     - Female (`581`): `[-1, 1956, -1, 2211, -1, -1]`
   - (Also has task `type=110` targeting `841` `ROCKGX`, plus standard attack/garrison/unload.)

3. **Stone Miner (`124` Male / `220` Female)** — primary gather task (`commands[0]`):
   - Identical structure to Gold Miner except: `unit_id=102`, `resource_in=2` (Stone), `resource_productivity_multiplier=79` (Stone Mining Productivity), `graphics=[-1, 1606, -1, 1605, -1, -1]` (male) / `[-1, 1956, -1, 1955, -1, -1]` (female).

### 1.3 `UnitCommand` (59 bytes in `.dat`) -> `RGE_Task` (`0x4c` bytes in memory)

Verified against `RGE_Task::load` (`0x849c70` in `decompiled.c`, UNVERIFIED in `objdump`):

| `datfile.py` field | `.dat` type | `RGE_Task` offset | Meaning in engine |
|---|---|---|---|
| `one` | `i16` | (not stored; header tag `1`) | Record type tag (must be `1`). |
| `id` | `i16` | `+0x02` | Task index ID within the unit header. |
| `unknown1` | `i8` | `+0x04` | `is_default` flag (`0` for gather). |
| `type` | `i16` | `+0x06` | `action_type`: **`5` = Gather (`RGE_Action_Gather`)**. |
| `class_id` | `i16` | `+0x08` | Target unit class (`-1` when targeting exact `unit_id`). |
| `unit_id` | `i16` | `+0x0a` | Target unit master ID (e.g., `66`, or new deposit ID `1224+`). |
| `terrain_id` | `i16` | `+0x0e` | Target terrain (`-1` for objects). |
| `resource_in` | `i16` | `+0x14` | Resource ID required on target (`res_id >= 211`). |
| `resource_productivity_multiplier` | `i16` | `+0x16` | Player resource ID multiplying gather rate (`-1` = `1.0f`). |
| `resource_out` | `i16` | `+0x18` | Resource ID credited on deposit (`-1` = use `unit->attribute_type_held`). |
| `resource` | `i16` | `+0x1a` | Unused resource (`-1`). |
| `work_rate_multiplier` | `f32` | `+0x1c` | Multiplies `master->work_rate` (`1.0`). |
| `execution_radius` | `f32` | `+0x20` | Gather range (`0.0` = adjacent). |
| `extra_range` | `f32` | `+0x24` | Extra search range (`0.0`). |
| `unknown4` | `i8` | `+0x28` | `auto_search_targets` (`1` = search next nearby deposit when depleted). |
| `unknown5` | `f32` | `+0x2c` | `search_wait_time` (`3.0`). |
| `selection_enabler` | `i8` | `+0x0c` | `enable_targeting` (`0`). |
| `unknown7` | `i8` | `+0x0d` | `combat_level_flag` (`0`). |
| `unknown8` | `i16` | `+0x30` | `gather_type` (`0`). |
| `unknown9` | `i16` | `+0x32` | `work_flag_2` (`0`). |
| `unknown10` | `i8` | `+0x10` | `target_diplomacy` (`3` = Gaia only, `0` = all, `1` = self, `2` = neutral/enemy). |
| `unknown11` | `i8` | `+0x11` | `carry_check` (`1` = check target has resource > 0). |
| `unknown12` | `i8` | `+0x12` | `track_resource_drain` (`0`). |
| `graphics[0]` | `i16` | `+0x34` (ptr) | `proceeding_graphic` (`-1` = use walking graphic). |
| `graphics[1]` | `i16` | `+0x38` (ptr) | `working_graphic` (`1606` male mining / `1956` female mining). |
| `graphics[2]` | `i16` | `+0x3c` (ptr) | `carrying_graphic_2` / alt working (`-1`). |
| `graphics[3]` | `i16` | `+0x40` (ptr) | `carrying_graphic` (`2210` male / `2211` female carrying). |
| `graphics[4]` | `i16` | `+0x44` (ptr) | `resource_gather_sound` (`-1`). |
| `graphics[5]` | `i16` | `+0x48` (ptr) | `resource_deposit_sound` (`-1`). |

### 1.4 Carry Capacity & Productivity Multipliers

- **Carry capacity is NOT a player resource.** It is `resource_capacity` (`int16`) on the unit master (`RGE_Master_Static_Object + 0x84`, `10` on all villagers/miners).
- Tech effects modify attribute `14` on `class_ = 4`:
  - `Wheelbarrow` (`eff[200]`): `type=4, a=-1, b=4, c=14, d=1.25`
  - `Hand Cart` (`eff[238]`): `type=4, a=-1, b=4, c=14, d=1.5`
  - `Aztecs` (`eff[447]`): `type=0, a=-1, b=4, c=14, d=+5.0`
- Because these effects target **`class_ = 4`** rather than individual unit IDs, **any new task-swap miner unit with `class_ = 4` automatically receives Wheelbarrow, Hand Cart, and Aztec carry bonuses.**
- **Productivity resource (`resource_productivity_multiplier`)**:
  - `civ.resources[47]` = Gold Mining Productivity (`1.0`, Mayans `1.15`); `79` = Stone; `189` = Wood; `190` = Food.
  - Note: `Gold Mining` (`eff[55]`) and `Gold Shaft Mining` (`eff[178]`) multiply attribute `13` (`work_rate`) on unit IDs `579` and `581` directly.
  - Setting `resource_productivity_multiplier = -1` on a custom gather task makes `RGE_Action_Gather` cleanly use `1.0f` (verified in disassembly at `0x4abe23..0x4abe49`), or a mod can point it to `47`/`79` or another custom resource initialized to `1.0`.

### 1.5 Option A (Reuse `579`/`581`) vs. Option B (New Task-Swap Miner Units)

| Aspect | Option A: Reuse Gold Miners (`579`/`581`) | Option B: New Male+Female Task-Swap Units (Recommended) |
|---|---|---|
| Dat slots consumed | 1 RESERVED slot (deposit only) | 3 RESERVED slots (1 Gaia deposit + 2 miner units) |
| `drop_site[2]` | Fixed to `[109, 584]` (TC + Mining Camp) | **Customizable per resource** — e.g. `[109, 584]`, `[109, 68]`, or `[109, <mod_building>]` |
| Work rate / Carry cap | Shares `0.38` + Gold Mining / Shaft Mining tech upgrades | Independent `work_rate`, `resource_capacity`, and upgrade isolation |
| Unit name & sprites | Displays as "Gold Miner" with gold-bag sprite | Custom name (e.g. "Crystal Miner") & custom working/carrying SLPs |
| Wheelbarrow / Aztecs | Automatic (`class_ = 4`) | Automatic (`class_ = 4`) |
| Task-swap compatibility | Native | Native (`0x4b0470` scans all `0..master_count-1` dynamically) |

**Recommendation**: Support **Option B** (new male+female task-swap units cloned from `579`/`581` on all playable civs `1..31`, placeholders `32..34`, and custom civs `35+`), with `drop_site` defaulting to `[109, 584]` (Town Center + Mining Camp) and overridable in `mod.json` (including referencing a mod's custom/unique building!).

### 1.6 Exact Builder Changes Required in `.dat`

For each gatherable custom resource `R` (with resource ID `rid >= 211`):
1. **Allocate 3 free RESERVED unit slots** from the existing `1224..1400` pool (180 free slots in base `.dat`): `dep_id` (deposit), `m_id` (male miner), `f_id` (female miner).
2. **Create Gaia deposit (`civs[0].units[dep_id]`)**:
   - Deep-copy `civs[0].units[66]` (Gold Mine) (or another template if specified, e.g. `102` Stone Mine or `59` Forage Bush).
   - Set `id = id2 = id3 = dep_id`, `name = "WL_DEP_<id>\0"`.
   - Set `language_dll_name = <allocated Wololoader string ID>` (registered in `cache/strings.txt`).
   - Set `resource_storages[0] = ResourceStorage(type=rid, amount=float(deposit_amount), store_mode=0)`, `resource_storages[1..2] = (-1, 0.0, 0)`.
   - Apply optional `standing_graphic` (via `add_graphic`), `minimap_color`, `class_` (default `32`).
   - Leave `civs[1..N].units[dep_id] = None`, and `dat.unit_headers.headers[dep_id]` with `exists=0, commands=None`.
3. **Create Male & Female task-swap miners (`civs[c].units[m_id]` and `[f_id]` for every `c >= 1`, including placeholders and custom civs)**:
   - Deep-copy `civs[c].units[579]` -> `m_id`, `civs[c].units[581]` -> `f_id`.
   - Set `id = id2 = id3 = m_id` (resp. `f_id`), `enabled = 0`, `hide_in_editor = 1`, `class_ = 4`, `type = 70`, `villager_mode = 1` (male) / `2` (female).
   - Set `work_rate`, `resource_capacity`, `drop_site = [ds0, ds1]` (default `[109, 584]`), `language_dll_name = <custom miner string ID>`.
   - Keep `civs[0].units[m_id] = None`, `civs[0].units[f_id] = None`.
4. **Create `UnitHeader` task lists (`dat.unit_headers.headers[m_id]` and `[f_id]`)**:
   - Deep-copy `dat.unit_headers.headers[579]` -> `m_id` and `[581]` -> `f_id`.
   - Remove the existing Gold Mine (`unit_id == 66`) and `ROCKGX` (`unit_id == 841`) gather tasks (keeping standard attack/garrison/unload so the miner still behaves like a villager).
   - Insert one `UnitCommand` with:
     - `one=1, id=0, unknown1=0, type=5`
     - `class_id=-1, unit_id=dep_id, terrain_id=-1`
     - `resource_in=rid, resource_productivity_multiplier=prod_res (default -1), resource_out=-1, resource=-1`
     - `work_rate_multiplier=1.0, execution_radius=0.0, extra_range=0.0`
     - `unknown4=1, unknown5=3.0, selection_enabler=0, unknown7=0, unknown8=0, unknown9=0, unknown10=3, unknown11=1, unknown12=0`
     - `graphics=[-1, work_gfx_m, -1, carry_gfx_m, gather_snd, deposit_snd]` (defaulting to `[1606, 2210]` male / `[1956, 2211]` female).

---

## 2. Engine Side Audit (`AoK HD.exe` 5.8)

Every address below was checked in `AoK HD.exe` using `objdump -d -M intel` unless explicitly marked **`UNVERIFIED`**.

### 2.1 Summary Table

| Subsystem | Function & Address | Verified? | Behavior for `res_id >= 211` | Action Needed? |
|---|---|---|---|---|
| Action dispatch (type 5 vs 122) | `TRIBE_Action_List::create_action` (`0x4bd940`) -> `RGE_Action_List::create_action` (`0x4ace70`) | **VERIFIED** | Type `5` creates **`RGE_Action_Gather`** (`0x4ad132`, size `0x5c`), NOT `TRIBE_Action_Gather` (type `122` only). | None |
| Gather, carry, drop-site, deposit | `RGE_Action_Gather` (`0x4aa7e0`, `0x4aa9e0`, `0x4ab7b0`) | **VERIFIED** | **100% generic.** Uses `master->drop_site[0..1]`, `task->resource_out`/`unit+0x50`, `player->attributes[prod]`. | None |
| Auto-search next deposit on depletion | `RGE_Action_Gather::update` (`0x4ab7b0` case 8 tail -> `0x4db350`, `0x4dc140`) | Decompiled (`UNVERIFIED` disasm) | Searches by target master/class ID; no resource-ID hardcoding. | None |
| Task-swap villager transform | `RGE_Action_Object::get_target_task` (`0x4b0470`) | **VERIFIED** | Dynamically scans `0..player->master_count-1` matching `villager_mode` (`+0x12c`), calls `transform` (`vtable+0x60`). | None |
| Deposit resource init on spawn | `RGE_Static_Object::setup` (`0x8b5640`) | **VERIFIED** | Writes `unit+0x50` (`int16`) & `unit+0x48` (`float`) for any `store_mode == 0, type != -1`. | None |
| Crediting player resource | `WorldPlayer::add_attribute_num` (`0x73d7e0`) -> `WorldPlayerBase::add_attribute_num` (`0x74cfe0`) | **VERIFIED** | Bounds-checked `0 <= id < [player+0x38]`; safe for `211+`. Cumulative stats `0xa6..0xa9` only updated for `0..3`. | None (`resources.c` already hooks `0x74cfe0`) |
| **Bottom-left unit info panel** | `TRIBE_Panel_Object::draw` (`0x713f00`) & `draw_item` (`0x7167c0`) | **VERIFIED** | **4 switches on `[obj+0x50]` bail out for `> 0x38`/`0x39`!** Deposit amount and villager carried amount are **invisible** (no crash). | **YES — Required** (overlay in `GameScreen::paint` or hook `0x713f00`) |
| Garrison villager into TC | `RGE_Action_Enter` (`0x4a9180`) | **VERIFIED** | Only auto-deposits `{0,1,2,3,15,16,17,56}`; for `211+`, villager retains carried resource while garrisoned (no crash). | Optional small patch/hook |
| Finish building drop-site | `TRIBE_Action_Build` (`0x4b3d40` / `0x4b4402..0x4b447d`) | **VERIFIED** | **Works for `211+`** on vanilla TC/Mining Camp (`cmp cx, -1; jle skip`). Mod custom drop-off building IDs not in hardcoded finish list. | None (or optional) |
| Selection flags bitmask | `WorldPlayer::set_selection_flags` (`0x73dc60`) | **VERIFIED** | Switch only sets bits for `0..3`/`15..17`; skips `211+` cleanly. | None |
| Drop-Off command button | `WorldPlayer::command_drop_off` (`0x73e820`) | **VERIFIED** | `cmp eax, 0x11; ja 0x73e950` skips confirmation voice line, still sends drop-off command. | None |
| AI resource & drop-site logic | `TribeResourceAIModule::resource` (`0x5179e0`), AI drop-site (`0x502a30`), gatherer notify (`0x535860` `UNVERIFIED`) | **VERIFIED** (`0x5179e0`, `0x502a30`) | Bounds-checked `< [ecx+0xf4]` (4) -> returns `-1` / `NULL`. AI safely ignores `211+`. | None |
| Score / Statistics / Checksum | `WorldPlayerBase::checksum` (`0x74e230`), `AchievementsScreen` (`0x69a0a0`), `RGE_Victory_Conditions` (`0x871890`) | `UNVERIFIED` disasm (decompiled only) | Checksum & Achievements only read `0..3` / `0xa6..0xa9`. Safe; custom resources don't affect vanilla score unless scripted. | None |
| Idle Villager / Eco Counters | Villager `class_ == 4` action-state checks; HD 5.8 has no per-resource eco counter HUD | `UNVERIFIED` disasm | Gathering villager is active (not idle); no resource-ID array in idle check. | None |
| Tribute | `TRIBE_Command::do_command_give_attribute` (`0x8151c0`), Diplomacy dialog (`0x6a8630` `UNVERIFIED`) | **VERIFIED** (`0x8151c0`) | Wire packet stores resource ID in **`uint8`** (`[edi+0x3]`) -> works for **`211..255`**, **wraps at `>= 256`**. Diplomacy UI only shows 4 buttons (`0..3`). | Document 255 cap if tribute used |

---

### 2.2 Detailed Disassembly Evidence

#### A. Task Type `5` -> `RGE_Action_Gather` (NOT `TRIBE_Action_Gather`)

In `TRIBE_Action_List::create_action` (`0x4bd940`, `__thiscall`), `task->action_type` (`WORD PTR [edi+0x6]`) is dispatched via a jump table at `0x4be250` / `0x4be1f8`:
- `task_type = 5` -> `eax = 5 - 3 = 2` -> `byte[0x4be250 + 2] = 21` -> `jmp 0x4be1b5` -> delegates to `RGE_Action_List::create_action` (`0x4ace70`) at `0x4be1dc`:
  ```asm
  4bd96a: movsx  eax, WORD PTR [edi+0x6]
  4bd96e: add    eax, -3
  4bd971: cmp    eax, 0x92
  4bd976: ja     0x4be1b5
  4bd97c: movzx  eax, BYTE PTR [eax+0x4be250]
  4bd983: jmp    DWORD PTR [eax*4+0x4be1f8]
  ...
  4be1dc: call   0x4ace70   ; RGE_Action_List::create_action
  ```
- `task_type = 122 (0x7a)` -> `byte[0x4be250 + 0x77] = 13` -> `jmp 0x4bdd33` -> allocates `0x44` bytes and constructs `TRIBE_Action_Gather` (`0x4b82a0`, Pillaging/Loot only!).
- Inside `RGE_Action_List::create_action` (`0x4ace70`), `task_type = 5` -> `eax = 5 - 4 = 1` -> `byte[0x4ad26c + 1] = 1` -> `jmp 0x4ad132`, which allocates `0x5c` bytes (`call 0x8d47df`) and calls `RGE_Action_Gather` ctor (`0x4aa190`):
  ```asm
  4ad132: push   0x5c
  4ad134: call   0x8d47df          ; game operator new
  ...
  4ad16c: call   0x4aa190          ; RGE_Action_Gather::RGE_Action_Gather
  ```

#### B. `RGE_Action_Gather::update` (`0x4ab7b0`) — Gather Tick & Deposit

- **Case 7 (Gathering tick, `0x4abe20..0x4abe76`)**:
  Reads `task->resource_productivity_multiplier` (`ax` from `[task+0x16]`). If `ax == -1`, loads `1.0f` (`0x9afd6c`); otherwise reads `player->attributes[prod]` (`[eax + ecx*4]`). Caps carried amount against `master->resource_capacity` (`WORD PTR [eax+0x84]`):
  ```asm
  4abe23: cmp    ax, 0xffff
  4abe27: je     0x4abe42                  ; -1 -> 1.0f
  4abe2f: movsx  ecx, ax
  4abe32: mov    eax, DWORD PTR [edx+0x10] ; player*
  4abe35: mov    eax, DWORD PTR [eax+0x3c] ; float* attributes
  4abe38: movss  xmm4, DWORD PTR [eax+ecx*4]
  ...
  4abe42: movss  xmm4, DWORD PTR ds:0x9afd6c ; 1.0f
  4abe5c: movsx  esi, WORD PTR [eax+0x84]    ; master->resource_capacity
  ```
- **Case 8 (Depositing at drop-site, `0x4ac0bc..0x4ac115`)**:
  Checks `task->resource_out` (`[task+0x18]`); if `-1`, uses `unit->attribute_type_held` (`WORD PTR [edx+0x50]`), then calls `player->vtable[+0x64](res_id, amount, 1)` (`WorldPlayer::add_attribute_num`, `0x73d7e0`) and `player->vtable[+0x44](unit_id, res_id, (int)amount)`:
  ```asm
  4ac0bc: movzx  eax, WORD PTR [eax+0x18]   ; task->resource_out
  4ac0c0: mov    edx, DWORD PTR [edi+0x8]   ; villager*
  4ac0c3: cmp    ax, 0xffff
  4ac0c7: je     0x4ac0d1
  4ac0c9: mov    esi, eax                   ; use resource_out
  4ac0cb: movzx  eax, WORD PTR [edx+0x50]
  4ac0cf: jmp    0x4ac0d7
  4ac0d1: movzx  eax, WORD PTR [edx+0x50]   ; use unit->attribute_type_held
  4ac0d5: mov    esi, eax
  ...
  4ac0f5: mov    ecx, DWORD PTR [edx+0x10]  ; player*
  4ac0f8: movss  xmm0, DWORD PTR [edx+0x48] ; carried amount
  4ac0fd: push   0x1                        ; is_gathered = 1
  4ac0ff: push   ecx
  4ac100: mov    eax, DWORD PTR [ecx]
  4ac102: movss  DWORD PTR [esp], xmm0
  4ac107: push   esi                        ; resource ID (211+ safe!)
  4ac108: call   DWORD PTR [eax+0x64]       ; WorldPlayer::add_attribute_num
  ```
  - Drop-site selection in `first_in_stack` (`0x4aa7e0`), `work` (`0x4aa9e0`), `set_state` (`0x4aaea0`), and `move_to` (`0x4ac4a0`) reads `master->drop_site[0]` (`+0x128`) and `master->drop_site[1]` (`+0x12a`) directly — zero resource-ID hardcoding.

#### C. Task-Swap Group Lookup (`RGE_Action_Object::get_target_task`, `0x4b0470`)

Verified `__thiscall` (`ret 0x14`, 5 args): if the unit's own `task_list` (`[master+0x118]`) has no matching task and `master->villager_mode` (`BYTE PTR [ecx+0x12c]`) is non-zero, loops over `0 .. player->master_count - 1` (`[player+0x10]`, `[player+0x14]`), matches `villager_mode` (`+0x12c`), `class_` (`+0x16`), and `type` (`+0x04`), checks the candidate's `task_list`, and calls `this->transform(candidate_master)` (`vtable[+0x60]`):
```asm
4b04be: mov    ecx, DWORD PTR [ebx+0xc]
4b04c1: cmp    BYTE PTR [ecx+0x12c], al   ; villager_mode != 0?
4b04c7: je     0x4b055a
...
4b0505: cmp    al, BYTE PTR [edx+0x12c]   ; candidate->villager_mode == mine?
4b050b: jne    0x4b055f
...
4b0541: call   DWORD PTR [eax+0x4]        ; candidate->task_list->get_target_task
4b0549: test   edx, edx
4b054b: je     0x4b0562
4b054d: mov    eax, DWORD PTR [ebx]
4b054f: mov    ecx, ebx
4b0551: push   esi                        ; candidate master*
4b0552: call   DWORD PTR [eax+0x60]       ; RGE_Static_Object::transform
...
4b055c: ret    0x14
```
Because it loops up to `player->master_count`, **any RESERVED unit slot (`1224..1400`) works automatically**.

#### D. Deposit Resource Storage Initialization (`RGE_Static_Object::setup`, `0x8b5640`)

Verified loop unrolled over all 3 `resource_storages` (`types` at `master+0x72, +0x74, +0x76`; `amounts` at `master+0x78, +0x7c, +0x80`; `store_modes` at `master+0x90, +0x91, +0x92`):
```asm
8b565e: cmp    ax, 0xffff                 ; type != -1?
8b5662: je     0x8b566e
8b5664: mov    WORD PTR [ecx+0x50], ax    ; unit->attribute_type_held = type (full int16!)
8b5668: mov    eax, DWORD PTR [edx+0x78]
8b566b: mov    DWORD PTR [ecx+0x48], eax  ; unit->attribute_amount_held = amount
```
No upper bound on `type` — supports `211..32767`.

#### E. Player Resource Addition (`WorldPlayer::add_attribute_num` `0x73d7e0` -> `WorldPlayerBase::add_attribute_num` `0x74cfe0`)

- `0x73d7e0` checks special IDs `3` (gold -> `0xa9`), `0xc4`, `0` (`0xa6`), `1` (`0xa7`), `2` (`0xa8`), `0x32`, `0xb7`, then calls `0x74cfe0`.
- `0x74cfe0` checks `0 <= si < [ecx+0x38]` (`player->attribute_num`) and adds `xmm1` to `[eax + edx*4]` (`player->attributes[id]`):
  ```asm
  74cfe4: mov    si, WORD PTR [ebp+0x8]
  74cfe8: test   si, si
  74cfeb: js     0x74d0ae
  74cff1: movsx  edx, si
  74cff4: cmp    edx, DWORD PTR [ecx+0x38] ; dynamic attribute_num (grown by builder!)
  74cff7: jge    0x74d0ae
  74cffd: mov    eax, DWORD PTR [ecx+0x3c]
  74d008: addss  xmm0, DWORD PTR [eax+edx*4]
  74d00d: movss  DWORD PTR [eax+edx*4], xmm0
  ```
- Wololoader's `wololoader/src/resources.c` already hooks `0x74cfe0` to enforce optional `max` caps on `211+`.

#### F. BROKEN UI — `TRIBE_Panel_Object::draw` (`0x713f00`) & `draw_item` (`0x7167c0`)

All **four** switches on `*(short *)(obj + 0x50)` in `TRIBE_Panel_Object::draw` (`0x713f00`, `__thiscall`, `ret 0` at `0x715a81`) bail out for any `res_id > 0x38` (or `0x39`):

1. **`0x7150bc`** (Gaia/neutral resource deposit, `command_attribute == 1` — e.g. clicking a Crystal Deposit):
   ```asm
   7150bc: movsx  eax, WORD PTR [eax+0x50]
   7150c5: cmp    eax, 0x38
   7150c8: ja     0x715a55          ; bails out -> draws nothing!
   ```
2. **`0x71541d`** (Owned storage/farm object carrying resource):
   ```asm
   71541d: movsx  eax, WORD PTR [esi+0x50]
   715421: cmp    eax, 0x38
   715424: ja     0x7154e2          ; bails out -> draws nothing!
   ```
3. **`0x715859`** (`command_attribute == 5` trade/relic unit):
   ```asm
   715859: movsx  eax, WORD PTR [ecx+0x50]
   71585d: cmp    eax, 0x38
   715860: ja     0x715a55
   ```
4. **`0x71592e`** (Owned villager/unit carrying resource, `command_attribute == 3` — e.g. clicking a Crystal Miner carrying crystal):
   ```asm
   71592e: movsx  eax, WORD PTR [esi+0x50]
   715932: cmp    eax, 0x39
   715935: ja     0x715a55          ; bails out -> draws nothing!
   ```

**Key panel offsets (verified in `0x6b35b0`, `0x713f00`, `0x7167c0`)** for fixing this cleanly in Wololoader:
- `[GameScreen + 0xb10]` = `TRIBE_Panel_Object*` (right next to `[GameScreen + 0xb0c]` = `TRIBE_Panel_Inven*` used by `resbar.c`!).
- `[panel + 0xb8]` = `x`, `[panel + 0xbc]` = `y`, `[panel + 0xc0]` = `w`, `[panel + 0xc4]` = `h`.
- `[panel + 0xbf0]` = `WorldPlayer*` (check `[player + 0x254] <= 1` so we only draw on single-unit selection, matching `0x713f00`!).
- `[panel + 0xbf4]` = `RGE_Static_Object*` (currently displayed unit/deposit).
- `[panel + 0xc04]` = `byte mode` (skip when `mode == 2`, matching `0x713f00`!).
- `TRIBE_Panel_Object::draw_item` (`0x7167c0`) places row `r` (`0..3`) icon at `(panel->x + 15, panel->y + 72 + r * 20)` and text at `(panel->x + 57, panel->y + 74 + r * 20)`.
  - For a **Gaia deposit** (`command_attribute == 1`, `[master + 0x97] == 1`), `r = 0` (top stat row) is completely empty because deposits have no attack/armor/range.
  - For a **villager carrying a custom resource** (`class_ == 4`), rows `0..2` hold attack/armor/etc. (or whatever `iVar10` ended on — we can hook `0x713f00` or inspect `[panel + 0xbd4]`/`iVar10` to place it on the exact next free row, typically row `2` or `3` for miners since miners have attack on row 0 and armor on row 1, or row 1 if attack is hidden — verify in-game!).

#### G. Edge Cases: Garrison (`0x4a9180`), Build Finish (`0x4b4402`), Drop-Off (`0x73e820`), AI (`0x5179e0`, `0x502a30`), Tribute (`0x8151c0`)

- **Garrison into TC (`RGE_Action_Enter`, `0x4a9180`)**:
  ```asm
  4a91ea: movzx  ebx, WORD PTR [ecx+0x50]
  4a91ee: cmp    bx, 0xf   ; ... 0x10, 0x11 -> map to 0 (food)
  4a9200: test   bx, bx    ; 0, 1, 2, 3, 0x38 allowed
  ...
  4a9217: cmp    bx, 0x38
  4a921b: jne    0x4a927f  ; skips auto-deposit for 211+!
  ```
  *Optional fix*: a tiny trampoline/patch at `0x4a9217` allowing `bx >= 211` (and `< player->attribute_num`) makes garrisoned villagers auto-deposit custom resources into TCs/Towers just like gold/stone.
- **Finishing construction (`TRIBE_Action_Build`, `0x4b4402..0x4b447d`)**:
  ```asm
  4b4416: movzx  ecx, WORD PTR [eax+0x50]
  4b441a: cmp    cx, 0xffff
  4b441e: jle    0x4b44dc  ; accepts ANY res_id >= 0 (including 211+!)
  ```
  Works automatically when finishing a TC (`621`), Mill (`68`), Mining Camp (`584`), or Lumber Camp (`562`). Only skips if the finished building itself is a *mod* custom building ID.
- **Drop-Off command (`WorldPlayer::command_drop_off`, `0x73e820`)**:
  ```asm
  73e8b0: movsx  eax, WORD PTR [eax+0x50]
  73e8ba: cmp    eax, 0x11
  73e8bd: ja     0x73e950  ; skips voice master lookup; still sends drop-off command!
  ```
- **AI (`TribeResourceAIModule::resource` `0x5179e0` & AI drop-site finder `0x502a30`)**:
  ```asm
  5179e6: test   edx, edx
  5179e8: js     0x517a0a           ; return -1
  5179ea: cmp    edx, DWORD PTR [ecx+0xf4] ; 4
  5179f0: jge    0x517a0a           ; return -1
  ```
  AI drop-site finder (`0x502a30`, `0x502ac5`) switches on `WORD PTR [edx+0x50]` (`cmp eax, 0x11; ja skip`), safely returning `NULL`.
- **Tribute (`TRIBE_Command::do_command_give_attribute`, `0x8151c0`)**:
  ```asm
  815204: movzx  ecx, BYTE PTR [edi+0x3]    ; uint8 resource ID! (0..255 only)
  815219: mov    eax, DWORD PTR [eax+0x3c]
  81521c: movss  xmm3, DWORD PTR [eax+ecx*4]
  ```
  Safe for `211..255` (up to 45 custom resources); wraps modulo 256 if a tribute command is ever issued for `id >= 256`. Diplomacy dialog UI (`0x6a8630`/`0x6aae50`, `UNVERIFIED` in disasm) only has buttons for `0..3`.

---

## 3. Random Map Scripts (RMS) Placement

Checked `resources/_common/drs/gamedata_x2/random_map.def`, vanilla HD 5.8 `.rms` scripts (`Arabia.rms`, `Kilimanjaro.rms`, `Nile Delta.rms`, `real_world_eastafrica.rms`), and the RMS parser/generator in `AoK HD.exe`.

### 3.1 Findings

1. **`#const` is already standard practice in HD 5.8 RMS**:
   - `random_map.def` defines vanilla objects up to ID `1186` using `#const` (e.g., `#const GOLD 66`, `#const STONE 102`).
   - Official DLC maps in `gamedata_x2/` routinely declare newer unit IDs at the top of the `.rms` file via `#const` (e.g., `Kilimanjaro.rms`: `#const DLC_ORANGEBUSH 1059`, `#const DLC_GOAT 1060`) and pass the symbol to `create_object`.
2. **`#const` is REQUIRED — raw numeric literals FAIL in `create_object`**:
   - In `RGE_RMM_Script_Controller` (`0x5ff4f0` / `0x5f9ee0`, decompiled; `0x5fec20` & `0x5fde10` **VERIFIED** in disassembly), `create_object` (token `0x30`) takes parameter type `3`, which calls `FUN_005fec20` -> `FUN_005fde10` (binary search tree lookup by string name in `[this + 0x3d890]`). It does **not** fall back to `atoi` (`0x5ff0e0`, param type `2`).
   - Passing a raw number like `create_object 1250` triggers `"Encountered undefined const 1250"`.
   - Passing `#const CRYSTAL_DEPOSIT 1250` followed by `create_object CRYSTAL_DEPOSIT` works: `#const` allocates a `0x78`-byte BST node (`0x5ffd50`) storing the full 32-bit `int` at `+0x68` with no count limit.
3. **No unit ID limit during object placement**:
   - Verified in `RGE_RMM_Object_Generator::place_objects` (`0x5f83a0`, disassembly `0x5f83f7..0x5f841a`, and `0x5f7d50` decompiled):
     ```asm
     5f8409: mov    eax, DWORD PTR [edx+0x184] ; world->players
     5f840f: mov    ebx, DWORD PTR [eax+ecx*8] ; player 0 (Gaia) when owner < 0
     5f8412: mov    ecx, DWORD PTR [esi]       ; obj_id from create_object
     5f8414: mov    eax, DWORD PTR [ebx+0x14]  ; player->master_objects
     5f8417: mov    eax, DWORD PTR [eax+ecx*4] ; direct lookup — no upper cap!
     ```
   - Any valid Gaia unit ID (such as a Wololoader deposit in `1224..1400`) spawns cleanly with standard RMS directives (`number_of_objects`, `number_of_groups`, `set_gaia_object_only`, `set_tight_grouping`, `min_distance_to_players`, etc.).
4. **Dynamic ID problem for RMS authors**:
   - Because Wololoader allocates RESERVED unit slots (`1224..1400`) dynamically across all enabled mods, an RMS author cannot hardcode `1250` if another mod might shift the slot index.
   - **Two clean solutions** (implement both):
     1. **RMS `#const` auto-injection / generated include**: During `datbuild`, write `<game>\wololoader\cache\wl_constants.def` containing `#const WL_DEPOSIT_<KEY> <dep_id>` (and optionally prepend it to `random_map.def` in memory via `files.c`, so *every* RMS script — even without `#include` — can write `create_object WL_DEPOSIT_CRYSTAL` directly!).
     2. **Lua fallback**: Expose `Game.DepositUnitId(key)` (or `Game.ResourceInfo(key).deposit_unit`) in `lua.c` so mods can also spawn deposits dynamically at match start (`on_game_start`) via `Game.Player(0):CreateUnit(dep_id, x, y)` on *any* map (including vanilla Arabia/Black Forest without requiring a custom RMS!).

---

## 4. Step-by-Step Implementation Plan

### Step 1 — `mod.json` Schema Extension (`resources[]`)

Extend each entry in `mod.json`'s `"resources"` array with an optional `"gather"` object:

```json
{
  "resources": [
    {
      "key": "crystal",
      "name": "Crystal",
      "icon": "icons/crystal.png",
      "start": 0,
      "show": true,
      "gather": {
        "deposit_name": "Crystal Deposit",
        "miner_name": "Crystal Miner",
        "template": 66,
        "amount": 600,
        "work_rate": 0.38,
        "carry_capacity": 10,
        "drop_sites": [109, 584],
        "minimap_color": 92,
        "productivity_resource": -1,
        "deposit_graphics": { "standing": 90100 },
        "miner_graphics": {
          "male_working": 1606,
          "male_carrying": 2210,
          "female_working": 1956,
          "female_carrying": 2211
        }
      }
    }
  ]
}
```

- All sub-keys in `"gather"` have sensible defaults (cloning Gold Mine `66` and Gold Miners `579`/`581`).
- `"drop_sites"` accepts 1 or 2 building unit IDs (default `[109, 584]`), or `"unique_building"` / custom building key if integrated with civ building slots.

### Step 2 — Dat Builder (`wololoader/tools/build.py` + `wololoader/src/datbuild.c`)

Keep `build.py` and `datbuild.c` **byte-identical** (`AGENTS.md` Rule 9):

1. **Parse `"gather"`** in `find_custom_resources()` (both Python and C), validating types and defaults.
2. **Allocate RESERVED unit slots**:
   - Reserve 3 slots per gatherable resource (`dep_id`, `male_id`, `female_id`) deterministically from the `1224..1400` pool *before* (or cleanly coordinated with) `unique_unit` / `custom_units` / `unique_building` so there are zero collisions, failing cleanly with a clear error if slots run out.
3. **Build the Gaia Deposit (`civs[0].units[dep_id]`)**:
   - Clone `civs[0].units[template]` (default `66`), set `id = id2 = id3 = dep_id`, `language_dll_name = sid_dep` (from Wololoader string allocator), `resource_storages[0] = (rid, amount, 0)`, `resource_storages[1..2] = (-1, 0.0, 0)`, optional `minimap_color`, optional `standing_graphic` via `add_graphic()`.
   - Set `dat.unit_headers.headers[dep_id]` to `exists = 0, commands = None`.
4. **Build Male & Female Miners (`civs[1..N].units[male_id]` and `[female_id]`)**:
   - After placeholders (`32..34`) and custom civs (`35+`) exist (or before cloning civs so custom civs inherit them automatically — doing it on `civs[1..31]` *before* placeholder/custom-civ cloning is simplest and guarantees every civ inherits them!).
   - Clone `579` -> `male_id`, `581` -> `female_id`; set `id = id2 = id3`, `language_dll_name = sid_miner`, `enabled = 0`, `hide_in_editor = 1`, `class_ = 4`, `villager_mode = 1` / `2`, `work_rate`, `resource_capacity`, `drop_site = [ds0, ds1]`.
   - Clone `dat.unit_headers.headers[579]` -> `male_id`, `[581]` -> `female_id`; strip existing Gold Mine (`66`) and `ROCKGX` (`841`) tasks; append the new `type=5` Gather `UnitCommand` targeting `unit_id = dep_id`, `resource_in = rid`, `resource_productivity_multiplier = prod_res`, `resource_out = -1`, `unknown4 = 1`, `unknown5 = 3.0`, `unknown10 = 3`, `unknown11 = 1`, and configured graphics.
5. **Write metadata**:
   - Append `dep_id`, `male_id`, `female_id` columns (or write `cache/gatherables.txt`) alongside `cache/resources.txt`.
   - Write `cache/strings.txt` entries for `deposit_name` and `miner_name`.
   - Write `cache/wl_constants.def` with `#const WL_DEPOSIT_<KEY> <dep_id>` lines.
6. **Verify byte-identity**: Run `test_datbuild` vs `python3 -I tools/build.py` and diff the output `.dat` and cache text files.

### Step 3 — Engine UI Fix: Unit Info Panel (`wololoader/src/resbar.c` / `uikit.c`)

Fix the missing remaining/carried amount in `TRIBE_Panel_Object` (`0x713f00`) without fragile mid-function assembly patches:

1. **Render in `uikit.c`'s existing `GameScreen::paint` (`0x6c2e40`) hook** (same GDI `HDC` + WIC PNG icon cache already in `resbar.c`):
   - Read `panel = *(void **)((char *)screen + 0xb10)` (`TRIBE_Panel_Object*`, verified in `0x6b35b0`).
   - Skip if `!panel`, `*(unsigned char *)((char *)panel + 0xc04) == 2` (queue/special mode), or `player = *(void **)((char *)panel + 0xbf0)` has `*(int *)((char *)player + 0x254) > 1` (multi-selection).
   - Read `obj = *(void **)((char *)panel + 0xbf4)`. If `!obj`, skip.
   - Read `short rid = *(short *)((char *)obj + 0x50)` and `float amt = *(float *)((char *)obj + 0x48)`.
   - If `rid >= 211` and `(int)(amt + 0.5f) > 0` (matching `0x713f00` rounding `+ 0.5f`):
     - Look up the custom resource entry in `g_wl` (for its PNG icon path & display name).
     - Determine row index `r`:
       - If Gaia deposit (`*(unsigned char *)(master + 0x97) == 1`): `r = 0`.
       - If villager (`*(short *)(master + 0x16) == 4`): place on the first free row below HP/attack/armor (verify exact row index `r = 2` vs `3` in-game, or capture the final row count from a lightweight detour on `0x7167c0` `draw_item` during `0x713f00`!).
     - Draw custom resource PNG (`20x20` or `24x20`, matching `0x7167c0`) at `(panel->x + 15, panel->y + 72 + r * 20)` and shadowed integer count at `(panel->x + 57, panel->y + 74 + r * 20)` using the panel's font.
2. **Optional polish hook — Garrison auto-deposit (`RGE_Action_Enter`, `0x4a9180`)**:
   - Either hook `0x4a9180` or patch the branch at `0x4a9217` (`66 83 fb 38 75 62`) so villagers carrying `res_id >= 211` auto-deposit when garrisoning into a TC (matching gold/stone behavior).

### Step 4 — RMS & Lua Integration

1. **RMS auto-constants (`wololoader/src/files.c`)**:
   - Intercept reads of `random_map.def` (or write a merged copy in `cache/random_map.def` during `datbuild` and redirect `random_map.def` in `files.c` — just like `empires2_x2_p1.dat`!) appending `#const WL_DEPOSIT_<KEY> <dep_id>` for every gatherable custom resource.
   - Result: any `.rms` file can immediately write `create_object WL_DEPOSIT_CRYSTAL { number_of_objects 4 ... set_gaia_object_only }` without knowing the allocated unit ID!
2. **Lua API (`wololoader/src/lua.c`)**:
   - Expose `Game.ResourceDepositId(key)` (returns `dep_id` or `nil`) so mods can also spawn deposits dynamically on standard maps via `Game.Player(0):CreateUnit(dep_id, x, y)`.

---

## 5. Open Questions to Test In-Game

1. **Exact `TRIBE_Panel_Object` row index for carrying villagers**: Confirm whether a Gold Miner (`579`) carrying gold draws its carried-gold row at `r = 2` or `r = 3` (depending on whether attack/armor/range rows are drawn for `class_ == 4`), or track `max_row + 1` via a `draw_item` (`0x7167c0`) hook during `0x713f00`.
2. **Cursor icon & hover tooltip when hovering a Crystal Deposit with villagers selected**: Does `command_attribute = 1` + a task-swap `type = 5` Gather task show the normal gather/pickaxe cursor on hover, or does cursor selection have a separate switch on `resource_in` / `class_`? (Check if cursor switches on `unit->attribute_type_held` or `task->action_type == 5`.)
3. **Depletion & auto-search next deposit**: When a Crystal Deposit hits `0`, confirm the deposit disappears (same as Gold Mine `66`) and the miner automatically walks to the next nearby Crystal Deposit via `auto_search_targets = 1` (`0x4ab7b0` case 8 tail).
4. **Save/Load round-trip**: Verify saving and reloading a game while a villager is mid-gather or carrying resource `211+` preserves `unit->attribute_type_held (+0x50)`, `unit->attribute_amount_held (+0x48)`, and the active `RGE_Action_Gather` task cleanly.
5. **Custom mod building as `drop_site[1]`**: Test a villager depositing at a mod `unique_building` (`1300..1400`) both automatically after filling capacity and via manual right-click on the building.
6. **Multiplayer sync**: Confirm two clients with identical `mod.json` / `cache/empires2_x2_p1.dat` stay in sync when gathering/depositing `211+` (note `WorldPlayerBase::checksum` `0x74e230` only hashes attributes `0..3`, while commands and unit positions/tasks are synced deterministically).
7. **RMS `random_map.def` redirect**: Verify the exact path/API the game uses to open `random_map.def` (`files.c` IAT hook vs DRS archive lookup) so we know whether `cache/random_map.def` redirect works seamlessly.
