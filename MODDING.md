# Wololoader Modding Guide

Wololoader is a modular runtime mod loader and patcher for **Age of Empires II HD (Edition 5.8)** (`AoK HD.exe`, 32-bit). It hooks file operations and game routines via a `version.dll` proxy, allowing you to add **brand new civilizations** beyond the hardcoded 31-civ limit, customize unit stats, create unique bonuses and tech trees, replace or add game assets, script random maps and AIs, and raise the population cap to 1000 — all **without replacing your original game data files**.

---

## Table of Contents

1. [Install](#1-install)
2. [Folder Layout of a Mod](#2-folder-layout-of-a-mod)
3. [mod.json](#3-modjson)
4. [Adding a Civilization](#4-adding-a-civilization)
5. [Units](#5-units)
   - [Supported Unit Fields Table](#supported-unit-fields-table)
   - [Unit graphics](#unit-graphics-graphics)
   - [Common Unit IDs Table](#common-unit-ids-table)
   - [Common Unit Classes Table](#common-unit-classes-table)
6. [Effects and Bonuses](#6-effects-and-bonuses)
   - [Effect Command Types](#effect-command-types)
   - [Resource IDs Table](#resource-ids-table)
   - [Attribute IDs Table](#attribute-ids-table)
   - [Common Bonus Recipes](#common-bonus-recipes)
7. [Tech Tree Changes](#7-tech-tree-changes)
   - [Common Technology IDs Table](#common-technology-ids-table)
8. [Unique Techs](#8-unique-techs)
9. [Replacing and Adding Game Files](#9-replacing-and-adding-game-files)
   - [Asset Overrides](#asset-overrides)
   - [New Maps and AI Scripts](#new-maps-and-ai-scripts)
10. [Text and Description Formatting](#10-text-and-description-formatting)
11. [Multiplayer and Vanilla Mode](#11-multiplayer-and-vanilla-mode)
12. [Scriptorium (Visual Mod Creator)](#12-scriptorium-visual-mod-creator)
13. [Troubleshooting and Diagnostics](#13-troubleshooting-and-diagnostics)
14. [Lua scripting](#14-lua-scripting)
   - [Scripting API](#scripting-api)
   - [Low-level `wl` API](#low-level-wl-api)

---

## 1. Install

Download the setup for your system from the [Releases page](https://github.com/Vitrie32/wololoader/releases/latest).

- **Windows**: run `wololoader-windows-setup.exe`. It finds the game through Steam (or pick the folder that holds `AoK HD.exe` with **Browse**) and installs `version.dll` and the `wololoader\` folder next to the game. Run it again to uninstall.
  On Windows the setup also clears the ASLR flag in the header of `AoK HD.exe` (one bit; the untouched exe is kept in `wololoader\backup\`, Uninstall sets the flag back): Wololoader patches the game at fixed addresses, and with ASLR Windows loads the game somewhere else. After Steam updates or verifies the game files, run the setup again (Wololoader tells you so when the game starts).
- **Linux / Steam Deck (Steam Proton)**: in a terminal,
  ```bash
  curl -fsSL https://wololoader.vercel.app/install.sh | sh
  ```
  (or download `wololoader-linux-setup.sh` and run `sh wololoader-linux-setup.sh`; options: a game folder, `--no-examples`, `--uninstall`). Then, in Steam, right-click **Age of Empires II HD** &rarr; **Properties** &rarr; **Launch Options**, and put:
  ```bash
  WINEDLLOVERRIDES="version=n,b" %command%
  ```
  *(This makes Wine load Wololoader's `version.dll` before its own.)*

Start the game: the main menu shows "Wololoader" with its version, and the **Mods** button lists the mods.

### Installing Mods

Every mod lives in its own folder inside:
```
<game>/wololoader/mods/<ModFolder>/
```
At every start Wololoader reads the `mod.json` of each folder, notices changed files and rebuilds its cache by itself. Turn mods on and off with the **Mods** button of the main menu (restart the game after a change).

---

## 2. Folder Layout of a Mod

A Wololoader mod is completely self-contained. The folder structure looks like this:

```
<game>/wololoader/mods/MyMod/
├── mod.json                          # Required: Mod metadata and enabled state
├── icon.png                          # Optional: shown in mod menus (any size, scaled automatically)
├── scripts/                          # Optional: Lua scripts (section 14)
│   ├── main.lua                      #   runs at start; loads the other scripts with require
│   ├── ai/economy.lua                #   a module: require("ai.economy")
│   └── visual.lua                    #   written by Scriptorium from the node graphs (do not edit)
├── civs/                             # Optional: Custom civilizations
│   └── my_civ.json                   # One JSON file per civilization
├── resources/                        # Optional: Mirrors the game's resources/ folder
│   ├── _common/
│   │   ├── drs/
│   │   │   └── graphics/             # Custom SLP sprite overrides
│   │   └── sound/                    # Custom sound and music overrides
│   ├── random-map-scripts/           # Custom .rms scripts (appear directly in lobby)
│   └── ai/                           # Custom .ai and .per scripts (appear in lobby)
├── select.mp3                        # Optional: Audio played on civ selection
└── game_b.slp                        # Optional: Custom in-game bottom HUD interface art
```

---

## 3. mod.json

Every mod must include a `mod.json` file at its root.

### Supported Keys

| Key | Type | Default | Description |
|---|---|---|---|
| `name` | string | Folder name | Human-readable name of the mod displayed in the in-game Mods screen and Scriptorium. |
| `version` | string | `""` | Version string (e.g. `"1.0"`, `"0.2"`). |
| `author` | string | `""` | Author name or handle. |
| `description` | string | `""` | Short description of what the mod provides. |
| `enabled` | boolean | `true` | When `false`, Wololoader skips loading this mod, and its civs and assets are excluded from the game. |

### Example `mod.json`

```json
{
  "name": "Monklanians",
  "version": "0.2",
  "author": "ali",
  "description": "Adds the Monklanians, a Briton-based civilization of tough, fast-walking monks with longer conversion range.",
  "enabled": true
}
```

---

## 4. Adding a Civilization

Civilizations are defined as JSON files placed inside the `civs/` folder of your mod (e.g. `mods/MyMod/civs/my_civ.json`).

Custom civilizations receive IDs starting at **35** (IDs 0..31 are base game civilizations, and 32, 33, 34 are reserved by the engine for RANDOM, SPECTATOR, and MIRROR). Wololoader bridges the gap automatically.

### Changing a civilization of the game (`extend`)

Instead of a new civilization, a civ file can change one of the game's own (1..31) in place:

```json
{
  "extend": 1,
  "units": { "4": { "hit_points": 40 } },
  "effects": [ { "type": 1, "a": 3, "b": 1, "d": 100 } ],
  "unique_building": { "copy_from": 104, "name": "Longbow Range", "commands": [ { "button": 1, "train": 4 } ] }
}
```

Allowed with `extend`: `units` (with `graphics`), `resources`, `effects`, `team_bonus_effects` (added to the civ's own team bonus), `enabled_techs`, `disabled_techs`, `enabled_units`, `disabled_units` and `unique_building`. The civ keeps its name, AI names, unique unit and unique techs. Several mods may extend the same civ; their changes add up in mod order.

### All Supported Keys in Civ JSON

Both the C builder (`datbuild.c`) and reference builder (`build.py`) strictly validate civ JSON files. Any unknown key will trigger a validation build error.

| Key | Type | Required | Default | Description |
|---|---|---|---|---|
| `name` | string | **Yes** | — | Display name shown in menus, lobby civ picker, and tech tree. |
| `dat_name` | string | **Yes** | — | Internal dat name. **Must be ASCII** and at most **19 characters** (e.g. `"Monklanians"`). |
| `base` | integer | **Yes** | — | Base civilization ID (1..31) to clone default units, building graphics, sounds, and tech tree from. |
| `description` | string | No | `""` | Civ bonus summary shown on the selection rollover and tech tree screen. Supports `\n` line breaks and `<b>` bold tags. UTF-8 text, no `&` character. |
| `ai_names` | list of strings | No | Base civ's AI names | Player names used by the AI in the lobby when playing this civilization (up to 40 names). |
| `sound` | string | No | Base civ selection sound | Relative path to an audio file inside the mod folder (e.g. `"select.mp3"`). Played when clicked in the lobby civ picker. |
| `interface_slp` | string | No | Base civ interface art | Relative path to an SLP file inside the mod folder (e.g. `"game_b.slp"`) overriding the in-game bottom HUD frame. |
| `history` | object | No | Base civ description fallback | Custom history entry for the in-game History screen. Keys: `title` (string), `text_file` (relative path to `.txt`), `text` (inline string), `image` (top-right illustration PNG), `side_image` (bottom-left illustration PNG). |
| `enabled_techs` | list of ints | No | `[]` | Technology IDs to enable that were previously forbidden in the base civ (removes engine `type=102` bans). |
| `disabled_techs` | list of ints | No | `[]` | Technology IDs to ban/hide in the tech tree (injects engine `type=102` bans). |
| `enabled_units` | list of ints | No | `[]` | Unit IDs to enable (injects engine `type=2, a=unit, b=1`). |
| `disabled_units` | list of ints | No | `[]` | Unit IDs to disable (injects engine `type=2, a=unit, b=0`). |
| `unique_techs` | list of 2 ints | No | Base civ unique techs | Two technology IDs: `[castle_tech_id, imperial_tech_id]` researchable at the Castle. |
| `effects` | list of objects | No | `[]` | Custom effect commands appended to the civilization's tech tree effect. |
| `team_bonus_effects` | list of objects | No | `[]` | Custom effect commands applied as team bonuses to the player and allies. |
| `units` | object | No | `{}` | Direct per-unit stat overrides: `"<unit_id>": { "<field>": value }`. |
| `resources` | object | No | `{}` | Direct starting resource overrides: `"<resource_index>": value`. |
| `tech_tree` | integer | No | Base civ tech tree ID | Base tech tree effect ID to clone before applying custom overrides. |
| `team_bonus` | integer | No | Base civ team bonus ID | Base team bonus effect ID. |

### Base Civilization IDs (1..31)

*Verified directly against the game's data file:*

| ID | Civilization | ID | Civilization | ID | Civilization | ID | Civilization |
|---|---|---|---|---|---|---|---|
| **1** | Britons (`British`) | **9** | Saracens | **17** | Huns | **25** | Ethiopians |
| **2** | Franks (`French`) | **10** | Turks | **18** | Koreans | **26** | Malians |
| **3** | Goths | **11** | Vikings | **19** | Italians | **27** | Berbers |
| **4** | Teutons | **12** | Mongols | **20** | Indians | **28** | Khmer |
| **5** | Japanese | **13** | Celts | **21** | Incas | **29** | Malay |
| **6** | Chinese | **14** | Spanish | **22** | Magyars | **30** | Burmese |
| **7** | Byzantines (`Byzantine`) | **15** | Aztecs | **23** | Slavs | **31** | Vietnamese |
| **8** | Persians | **16** | Mayans (`Mayan`) | **24** | Portuguese | | |

### Full Working Example: Monklanians (`monklanians.json`)

```json
{
  "name": "Monklanians",
  "dat_name": "Monklanians",
  "base": 1,
  "description": "<b>Monklanians<b> \nMonk civilization \n\n· Monks have +50% HP (45) and move 20% faster \n· Monks convert from +2 range and see +1 further \n· Start with +100 gold \n· Redemption and Bloodlines enabled \n· Cavalier disabled \n· Villagers have 40 HP \n\n<b>Team Bonus:<b> \nMonasteries work 20% faster",
  "ai_names": ["Abbot Wololo", "Sister Ayoyo", "Brother Rogan", "Prior Halleluja", "Father Mumbles"],
  "sound": "select.mp3",
  "interface_slp": "game_b.slp",
  "history": {
    "title": "The Monklanians",
    "text_file": "history/monklanians.txt",
    "image": "history/monklanians.png",
    "side_image": "history/monklanians_side.png"
  },
  "enabled_techs": [316, 435],
  "disabled_techs": [209],
  "disabled_units": [283],
  "effects": [
    { "type": 1, "a": 3, "b": 1, "c": -1, "d": 100.0 }
  ],
  "team_bonus_effects": [
    { "type": 5, "a": 104, "b": -1, "c": 13, "d": 1.2 },
    { "type": 5, "a": 30, "b": -1, "c": 13, "d": 1.2 }
  ],
  "unique_techs": [3, 461],
  "units": {
    "125": { "hit_points": 45, "speed": 0.84, "max_range": 11, "line_of_sight": 12, "search_radius": 12 },
    "286": { "hit_points": 45, "speed": 0.84, "max_range": 11, "line_of_sight": 12, "search_radius": 12 },
    "83":  { "hit_points": 40 },
    "293": { "hit_points": 40 }
  }
}
```

### In-Game History Screen (`history`)

Wololoader integrates modded civilizations into the game's **History** screen (**Main Menu > History**).

A toggle button (**Mod civs: Off** / **Mod civs: On**) appears at the top above the history listbox:
- **Mod civs: Off**: Displays the vanilla 52 historical topics and civilizations with original artwork, narration sounds, and texts.
- **Mod civs: On**: Populates the list with all active custom mod civilizations (IDs 35+). Selecting a mod civilization displays its custom title, formatted history text (with `<B>` bold heading support), top-right artwork, and bottom-left side illustration, while playing the civilization's selection sound (`"sound"` key).

#### Defining Civ History in JSON

In your civilization's JSON (`mods/<mod>/civs/<civ>.json`), add an optional `"history"` object:

```json
"history": {
  "title": "The Monklanians",
  "text_file": "history/monklanians.txt",
  "image": "history/monklanians.png",
  "side_image": "history/monklanians_side.png"
}
```

- **`title`** *(string, optional)*: Header title shown above the history text and in the listbox. Defaults to civilization `name`.
- **`text_file`** *(string, optional)*: Path relative to your mod directory to a UTF-8 text file containing the civilization's history article. Supports newline characters and formatting tags such as `<B>Title<B>`, which are parsed through the engine's rich text formatter.
- **`text`** *(string, optional)*: Inline history text if `text_file` is not used.
- **`image`** *(string, optional)*: Path relative to your mod directory to a PNG image (e.g. `history/monklanians.png`). Scaled and displayed in the main illustration panel above the article text (x=710, y=90, w=280, h=240).
- **`side_image`** *(string, optional)*: Path relative to your mod directory to a PNG image (e.g. `history/monklanians_side.png`). Displayed in the bottom-left illustration panel below the topic listbox (x=184, y=514, w=280, h=206). If omitted, the bottom-left slot remains empty with vanilla pictures hidden so no stale image remains.
- **Sound**: When selected, the civ's audio defined in `"sound"` (e.g. `"sound": "select.mp3"`) is played via the game's sound system. If omitted, no sound plays. Sound automatically stops when clicking Back, toggling Mod civs off, or selecting another topic.
- **Fallback**: If no `"history"` object is provided, the civilization's `"description"` text is used, ensuring every mod civilization has an entry on the History screen.

---

## 5. Units

Unit stats can be modified in two ways:
1. **Direct overrides in `units` object**: Permanently alters the unit's base stats for your civilization (e.g. `"125": { "hit_points": 45 }`).
2. **Civ Effects (`effects`)**: Applies dynamic bonuses or multipliers via effect commands (e.g. multiply attribute 0 by 1.5).

### Supported Unit Fields Table

The Genie engine uses an inheritance hierarchy across unit types:
- `Type 10`: Unit (base for all objects)
- `Type 20/25`: UnitFlag (moving units, flags)
- `Type 30`: UnitDeadFish
- `Type 40`: UnitBird
- `Type 50`: UnitType50 (combat units and weapons)
- `Type 60`: UnitProjectile (projectiles)
- `Type 70`: UnitCreatable (trained units, siege, ships, villagers)
- `Type 80`: UnitBuilding (buildings and structures)

*All 146 supported unit fields from `datbuild.c` grouped by category:*

#### General & Identity (Supported by all unit types: 10, 20, 30, 40, 50, 60, 70, 80)
- `hit_points` (int16): Unit maximum health points.
- `line_of_sight` (float32): Distance the unit reveals in fog of war.
- `id`, `id2`, `id3` (int16): Internal unit identifiers.
- `type` (uint8): Unit type category (10..80).
- `class_` (int16): Unit class ID (e.g. 4=Villager, 6=Infantry, 12=Cavalry, 18=Monk).
- `enabled` (int16): Whether the unit is available by default (1=enabled, 0=disabled).
- `civilization` (int8): Owning civilization filter ID.
- `garrison_capacity` (int8): Number of units this unit can garrison.
- `editor_radius` (list of 2 float32): Editor selection bounds `[x, y]`.
- `selection_radius` (list of 2 float32): Clickable selection radius `[x, y]`.
- `size_radius` (list of 2 float32): Physical collision radius `[x, y]`.
- `hp_bar_height1`, `hp_bar_height2` (float32): Vertical offset of health bar.
- `icon_id` (int16): Inventory / UI icon ID.
- `standing_graphic` (list of 2 int16): Idle sprite IDs `[normal, snow]`.
- `dying_graphic` (list of 2 int16): Death animation sprite IDs.
- `dead_unit_id` (int16): Unit spawned on death (corpse/rubble).
- `dying_sound` (int16): Sound played when unit dies.
- `selection_sound` (int16): Sound played when unit is clicked.
- `train_sound` (list of 2 int16): Sound played when unit creation starts.
- `unselectable` (int8): 1 if player cannot select unit.
- `hide_in_editor` (int8): 1 if hidden from scenario editor.
- `terrain_restriction` (int16): Terrain passability table index.
- `placement_mode` (int8), `placement_terrain` (list of 2 int16), `placement_bypass_terrain` (list of 2 int16).
- `interaction_mode`, `air_mode`, `fly_mode`, `hill_mode`, `death_mode`, `trigger_type`, `blast_armor_level`, `command_attribute`, `attribute`, `selection_shape`, `selection_shape_type`, `selection_mask`, `selection_effect`, `editor_selection_colour`, `minimap_mode`, `minimap_color`, `visible_in_fog`, `convert_terrain`, `hotkey`, `language_dll_name`, `language_dll_creation`, `language_dll_help`, `language_dll_hotkey_text`, `name_length`, `unknown1`, `unknown6`, `unknown7`, `unknown8`, `unknown3a`.

#### Movement & Pathfinding (Types >= 20; all except Type 10)
- `speed` (float32): Movement speed (tiles per second). E.g. Villagers: 0.8, Monks: 0.7, Knights: 1.35.
- `rotation_speed` (float32, Types >= 30): Turn rate speed.
- `walking_graphic` (list of 2 int16, Types >= 30): Walk animation sprite IDs `[normal, snow]`.
- `tracking_unit`, `tracking_unit_density`, `tracking_unit_used` (Types >= 30).

#### Vision, Animals & Tasks (Types >= 40)
- `search_radius` (float32): Range at which unit automatically spots and engages enemies.
- `work_rate` (float32): Gathering, building, and work speed multiplier (default 1.0).
- `villager_mode` (int8): Task mode flag for villagers.
- `animal_mode` (int8): Wild/domestic animal behavior mode.
- `sheep_conversion` (int16): Automatic team conversion behavior.
- `move_sound` (int16): Sound played when ordered to move.
- `attack_sound` (int16): Sound played when attacking.
- `drop_site` (list of 2 int16): Building unit IDs where gathered resources can be deposited.

#### Combat, Range & Armor (Types >= 50: Combat Units, Projectiles, Creatable Units, Buildings)
- `max_range` (float32): Maximum attack/conversion range. E.g. Monks: 9.0, Archers: 4.0.
- `min_range` (float32): Minimum attack range (e.g. Skirmishers: 1.0, Mangonels: 3.0, Trebuchets: 16.0).
- `reload_time` (float32): Attack interval in seconds (lower means faster attacks).
- `accuracy_percent` (int16): Base attack accuracy percentage (0..100).
- `accuracy_error_radius` (float32): Spread radius on missed shots.
- `frame_delay` (int16): Animation frame delay before projectile release or strike.
- `default_armor` (int16): Base armor class.
- `blast_width` (float32): Area-of-effect blast diameter in tiles.
- `blast_attack_level` (int8): Blast damage drop-off curve.
- `projectile_unit_id` (int16): Unit ID of fired projectile (e.g. arrow, stone).
- `displayed_attack` (int16): Attack value shown in the UI panel.
- `displayed_range` (float32): Range shown in the UI panel.
- `displayed_reload_time` (float32): Reload time shown in the UI panel.
- `displayed_melee_armour` (int16): Melee armor shown in the UI panel.
- `tower_mode` (int8): 1 if building behaves as a defensive tower.
- `attack_graphic` (int16): Attack animation sprite ID.
- `attack_count`, `armour_count` (uint16): Internal counts.
- `ter_restriction_for_dmg_multiplier` (int16), `graphic_displacement` (list of 3 float32).

#### Creatable Units & Buildings (Types 70 and 80)
- `train_time` (int16): Production time in seconds.
- `train_location_id` (int16): Unit ID of the building that trains this unit (e.g. 12=Barracks, 101=Stable, 104=Monastery).
- `button_id` (int8): Placement slot in the creation command panel.
- `hero_mode` (int8): 1 for hero status (regenerates health, immune to conversion).
- `special_ability` (int8): Secondary special abilities.
- `displayed_pierce_armour` (int16): Pierce armor shown in the UI panel.
- `total_projectiles` (float32): Base number of arrows/projectiles fired simultaneously.
- `max_total_projectiles` (int8): Maximum extra projectiles when garrisoned.
- `projectile_spawning_area` (list of 3 float32): Relative origin coordinates `[x, y, z]` where projectiles spawn.
- `secondary_projectile_unit` (int32): Secondary projectile ID.
- `garrison_graphic` (int32), `special_graphic` (int32).

#### Buildings Only (Type 80)
- `construction_graphic_id` (int16): Construction scaffold sprite ID.
- `construction_sound` (int16): Hammering construction sound.
- `garrison_heal_rate` (float32): Health points restored per second while garrisoned.
- `garrison_type` (int8): Allowed garrison unit type flag.
- `disappears_when_built` (int8): 1 for foundations that vanish once completed.
- `adjacent_mode` (int8): Wall/gate adjacency connection behavior.
- `foundation_terrain_id` (int16): Terrain change under foundation.
- `graphics_angle` (int16): Isometric perspective angle.
- `head_unit`, `pile_unit`, `stack_unit_id` (int16): Multi-tile construction helpers.
- `research_id` (int16): Technology automatically unlocked upon building completion.
- `transform_unit` (int16): Transformation target unit ID.
- `snow_graphic_id` (int16): Snow-covered building graphic ID.
- `old_terrain_like_id` (int16), `looting_table` (list of 6 int8).

#### Projectiles Only (Type 60)
- `projectile_arc` (float32): Parabolic flight trajectory curve.
- `stretch_mode` (int8), `compensation_mode` (int8), `drop_animation_mode` (int8), `penetration_mode` (int8).

> [!NOTE]
> **Read-Only / Structured Fields**: Fields with complex nested sub-structures (`damage_graphics`, `attacks`, `armours`, `annexes`, `resource_costs`, `resource_storages`, and `name`) cannot be overridden via simple JSON values in `units`. Overriding them will trigger a build validation error. Use [civ effects](#6-effects-and-bonuses) to modify attack, armor, or costs instead.

---

### Unit graphics (`graphics`)

A unit can get its own sprites. Put `"graphics"` in a unit's override (`"units": {"125": {"graphics": {...}}}`), in `unique_unit` (also `"elite_graphics"` for the elite; without it the elite gets the same), or in a unique building's `train` command:

```json
"graphics": {
  "standing": 15010,
  "walking": 15011,
  "attack": { "slp": 15012, "frames": 12 },
  "dying": 15013
}
```

| Animation | Unit field | Notes |
|---|---|---|
| `standing`, `standing2` | `standing_graphic` [0], [1] | every unit |
| `dying`, `decay` | `dying_graphic` [0], [1] | `decay` = the corpse left behind |
| `walking`, `running` | `walking_graphic` [0], [1] | moving units |
| `attack` | `attack_graphic` | combat units |

For each animation Wololoader copies the unit's current graphic and points the copy at your SLP, `<mod>/resources/_common/drs/graphics/<slp>.slp`. Everything else comes from the copied graphic: frame and angle counts, speed, sounds, and its extra parts (shadows, the monk's halo, ...). So draw your SLP with the same number of frames and angles as the game's one, or set `"frames"` (1..1000) and `"angles"` (1..64) in the object form; when `angles` changes, the per-angle attack sounds of the copy are dropped. The easiest start is to export the game's own SLP for that animation and repaint it, keeping its frame layout. SLP ids from 15000 up are unused by the game. The unit must already have the animation (a unit without a running graphic cannot get `running`).

### Common Unit IDs Table

*Verified directly against the game data:*

| Unit ID | In-Game Name | Internal Name | Class ID | Unit Type | Description |
|---|---|---|---|---|---|
| **83** | Villager (Male) | `VMBAS` | 4 | 70 | Male base villager. |
| **293** | Villager (Female) | `VFBAS` | 4 | 70 | Female base villager. |
| **125** | Monk | `MONKX` | 18 | 70 | Monastery monk healer & converter. |
| **286** | Monk with Relic | `RMONK` | 43 | 70 | Monk carrying a relic. |
| **4** | Archer | `ARCHR` | 0 | 70 | Archery Range foot archer. |
| **7** | Skirmisher | `XBOWM` | 0 | 70 | Anti-archer ranged skirmisher. |
| **24** | Crossbowman | `CARCH` | 0 | 70 | Castle Age archer upgrade. |
| **39** | Cavalry Archer | `CVRCH` | 36 | 70 | Mounted ranged cavalry. |
| **474** | Heavy Cavalry Archer | `HCVAR` | 36 | 70 | Imperial Age cavalry archer. |
| **5** | Hand Cannoneer | `HCANR` | 44 | 70 | Gunpowder infantry. |
| **8** | Longbowman | `LNGBW` | 0 | 70 | Britons unique archer unit. |
| **74** | Militia | `SPRMN` | 6 | 70 | Dark Age basic infantry. |
| **75** | Man-at-Arms | `SWDMN` | 6 | 70 | Feudal Age sword infantry. |
| **77** | Long Swordsman | `THSWD` | 6 | 70 | Castle Age sword infantry. |
| **473** | Two-Handed Swordsman | `HTHSW` | 6 | 70 | Imperial Age sword infantry. |
| **567** | Champion | `HEROI` | 6 | 70 | Final swordsman upgrade. |
| **93** | Spearman | `PKEMN` | 6 | 70 | Anti-cavalry infantry. |
| **358** | Pikeman | `ISPKM` | 6 | 70 | Castle Age anti-cavalry upgrade. |
| **359** | Halberdier | `HLBDM` | 6 | 70 | Imperial Age anti-cavalry upgrade. |
| **448** | Scout Cavalry | `SCOUT` | 47 | 70 | Light reconnaissance cavalry. |
| **546** | Light Cavalry | `LTCAV` | 12 | 70 | Castle Age light cavalry upgrade. |
| **441** | Hussar | `HUSSAR` | 12 | 70 | Imperial Age light cavalry upgrade. |
| **38** | Knight | `KNGHT` | 12 | 70 | Heavy shock cavalry. |
| **283** | Cavalier | `PALDN` | 12 | 70 | Castle & Imperial heavy cavalry upgrade. |
| **569** | Paladin | `HEROC` | 12 | 70 | Final heavy cavalry upgrade. |
| **329** | Camel | `CVLRY` | 12 | 70 | Anti-cavalry desert unit. |
| **330** | Heavy Camel | `HCLRY` | 12 | 70 | Upgraded camel unit. |
| **35** | Battering Ram | `BTRAM` | 13 | 70 | Anti-building siege ram. |
| **422** | Capped Ram | `CBATR` | 13 | 70 | Upgraded battering ram. |
| **548** | Siege Ram | `SGRAM` | 13 | 70 | Final siege ram upgrade. |
| **280** | Mangonel | `MANGO` | 13 | 70 | Catapult firing splash stones. |
| **550** | Onager | `ONAGR` | 13 | 70 | Upgraded mangonel. |
| **279** | Scorpion | `SCBAL` | 55 | 70 | Bolt thrower with pierce damage. |
| **542** | Heavy Scorpion | `HWBAL` | 55 | 70 | Upgraded bolt thrower. |
| **36** | Bombard Cannon | `BCANN` | 13 | 70 | Gunpowder siege weapon. |
| **42** | Trebuchet | `TREBU` | 54 | 80 | Long-range siege engine (unpacked). |
| **12** | Barracks | `BRKS` | 3 | 80 | Infantry training building. |
| **87** | Archery Range | `ARRG` | 3 | 80 | Archer training building. |
| **101** | Stable | `STBL` | 3 | 80 | Cavalry training building. |
| **49** | Siege Workshop | `SIWS` | 3 | 80 | Siege weapon building. |
| **104** | Monastery | `CRCH` | 3 | 80 | Monk training & relic storage building. |
| **30** | Monastery (Variant) | `CRCH2` | 3 | 80 | Secondary monastery slot. |
| **82** | Castle | `CSTL` | 3 | 80 | Stronghold building. |
| **109** | Town Center | `RTWC` | 3 | 80 | Primary command building. |
| **84** | Market | `MRKT` | 3 | 80 | Trade cart & commodity exchange. |
| **68** | Mill | `MILL` | 3 | 80 | Food collection depot. |
| **70** | House | `HOUS` | 3 | 80 | Residential population building. |
| **79** | Watch Tower | `WCTW` | 52 | 80 | Defensive outpost tower. |
| **209** | University | `UNIV` | 3 | 80 | Academic technology research center. |
| **276** | Wonder | `WNDR` | 3 | 80 | Monument structure. |

*(Note: Villager job variants 56, 57, 118, 120, 122, 124, 156, 206, 218, 220, 222 share the base stats of Villagers 83/293).*

---

### Common Unit Classes Table

When applying effects to entire unit groups using `b=<class_id>`, use these verified class codes:

| Class ID | Class Description | Typical Member Units |
|---|---|---|
| **0** | Archer | Archer (4), Skirmisher (7), Crossbowman (24), Arbalest |
| **2** | Trade Unit | Trade Cog (17), Trade Cart (128) |
| **3** | Building | Barracks (12), Stable (101), Monastery (104), Town Center (109), Castle (82) |
| **4** | Civilian / Villager | Male Villager (83), Female Villager (293), all task variants |
| **6** | Infantry | Militia (74), Man-at-Arms (75), Long Swordsman (77), Spearman (93), Champion (567) |
| **12** | Cavalry | Knight (38), Cavalier (283), Paladin (569), Light Cav (546), Camel (329) |
| **13** | Siege Weapon | Battering Ram (35), Mangonel (280), Bombard Cannon (36) |
| **18** | Monk | Monk (125) |
| **20** | Transport Ship | Transport Ship (545) |
| **21** | Fishing Ship | Fishing Ship (13) |
| **22** | Warship | Galley (21), War Galley (442), Galleon (539), Fire Ship (527) |
| **27** | Wall / Gate | Stone Wall (117), Fortified Wall (155), Palissade Wall (72) |
| **36** | Cavalry Archer | Cavalry Archer (39), Heavy Cavalry Archer (474) |
| **43** | Monk with Relic | Monk with Relic (286) |
| **44** | Gunpowder Hand Cannon | Hand Cannoneer (5), Janissary |
| **47** | Scout Cavalry | Scout Cavalry (448) |
| **49** | Farm | Farm (50) |
| **52** | Tower | Watch Tower (79), Guard Tower (234), Keep (235) |
| **54** | Trebuchet | Trebuchet (42) |
| **55** | Scorpion | Scorpion (279), Heavy Scorpion (542) |

---

## 6. Effects and Bonuses

Civilization bonuses are written as effect commands inside the `effects` array (for civ bonuses) or `team_bonus_effects` array (for team bonuses).

Each command is an object with five parameters:
```json
{ "type": 5, "a": -1, "b": 18, "c": 0, "d": 1.5 }
```
- `type`: Operation code (0..6, 101..103).
- `a`: Target Unit ID (`-1` = any/all).
- `b`: Target Unit Class ID (`-1` = any/all) or mode.
- `c`: Target Attribute ID (`-1` = none).
- `d`: Numeric value, amount, or multiplier.

### Effect Command Types

| Type | Operation | Parameter `a` | Parameter `b` | Parameter `c` | Parameter `d` |
|---|---|---|---|---|---|
| **0** | Set Attribute | Unit ID (or -1) | Class ID (or -1) | Attribute ID | New value to assign |
| **1** | Modify Resource | Resource ID | Mode: `0`=Set, `1`=Add | `-1` | Amount to set or add |
| **2** | Enable / Disable Unit | Unit ID | `0`=Disable, `1`=Enable | `-1` | `0.0` |
| **3** | Upgrade Unit | From Unit ID | To Unit ID | `-1` | `0.0` |
| **4** | Add Attribute | Unit ID (or -1) | Class ID (or -1) | Attribute ID | Amount to add |
| **5** | Multiply Attribute | Unit ID (or -1) | Class ID (or -1) | Attribute ID | Multiplier (e.g. `1.2` = +20%) |
| **6** | Multiply Resource | Resource ID | Mode (`0`) | `-1` | Multiplier |
| **101** | Modify Tech Cost | Tech ID | Cost mode | Resource ID | Cost modifier |
| **102** | Disable Tech | `-1` | `-1` | `-1` | Tech ID to forbid |
| **103** | Modify Tech Time | Tech ID | Mode | `-1` | Time multiplier |

---

### Resource IDs Table

*Verified directly against the game data:*

| Resource ID | Resource Name | Usage & Verified Examples |
|---|---|---|
| **0** | Food | Primary food resource stockpile. |
| **1** | Wood | Primary wood resource stockpile. |
| **2** | Stone | Primary stone resource stockpile. |
| **3** | Gold | Primary gold resource stockpile. |
| **4** | Population Headroom | Current maximum housing capacity provided by houses/castles (up to pop cap). E.g. Huns bonus sets resource 4 to 1000. |
| **6** | Current Age | `0`=Dark Age, `1`=Feudal Age, `2`=Castle Age, `3`=Imperial Age. |
| **11** | Current Population | Number of active player units alive. |
| **27** | Atonement | Enables Monks to convert enemy monks (`1.0` = enabled). |
| **28** | Redemption | Enables Monks to convert buildings and siege (`1.0` = enabled). |
| **32** | Bonus Population Cap | Extra population ceiling beyond match cap (e.g. Goths +10 pop in Imperial Age). |
| **35** | Illumination | Faster monk faith recovery. |
| **36** | Farm Food Amount | Base food capacity of farms (Crop Rotation, Heavy Plow, Horse Collar). |
| **46** | Tribute Inefficiency | Tribute tax (Coinage, Banking). *(not verified)* |
| **50** | Cartography | Shared exploration vision with allies (`1.0` = enabled). |
| **77** | Conversion Resistance | Resistance against enemy monk conversion (Faith, Teutons bonus). |
| **78** | Market Trade Fee | Fee multiplier for buying/selling goods (Guilds). |
| **84** | Starting Villagers Count | Starting villager count adjustment (Chinese: +6, Mayans: +4). |
| **91** | Starting Food Offset | Match startup food offset (Chinese: -200, Persians: +50, Mayans: -50). |
| **92** | Starting Wood Offset | Match startup wood offset (Persians: +50, Huns: -100). |
| **94** | Starting Gold Offset | Match startup gold offset (Aztecs: +50). |
| **192** | Heresy | Converted units instantly die instead of switching teams (`1.0` = enabled). |
| **193** | Theocracy | Only one monk in a group loses faith upon completing conversion. |

---

### Attribute IDs Table

*Verified directly against the game data:*

| Attr ID | Attribute Name | Description & Verified Examples |
|---|---|---|
| **0** | Hit Points | Maximum health points (e.g. Loom adds +15 HP; Ship HP x1.10). |
| **1** | Line of Sight | Vision range in fog of war (e.g. Town Watch adds +4 LOS; Magyars team bonus adds +2). |
| **2** | Garrison Capacity | Garrison unit capacity (e.g. Town Center garrison, Transport Ship). |
| **5** | Movement Speed | Velocity in tiles/second (e.g. Husbandry multiplies cavalry speed by 1.10). |
| **8** | Armor | Base armor. Encoded as `(armor_class * 256) + armor_amount`. |
| **9** | Attack | Base damage. Encoded as `(attack_class * 256) + damage_amount`. |
| **10** | Reload Time | Attack cooldown interval in seconds (e.g. Japanese infantry x0.75 for 25% faster attack). |
| **11** | Accuracy % | Hit probability percentage (e.g. Thumb Ring sets archer accuracy to 100%). |
| **12** | Max Range | Maximum attack/conversion range (e.g. Fletching adds +1 range). |
| **13** | Work Rate | Working/gathering/training rate multiplier (e.g. Monasteries 1.2 for 20% faster; Slavs farmers 1.15). |
| **14** | Carry Capacity | Resource carrying capacity of villagers (Heavy Plow, Wheelbarrow). |
| **19** | Projectile Guidance | Ballistics tracking enabler (`1.0` = shots track moving targets). |
| **20** | Minimum Range | Minimum firing distance (e.g. Murder Holes removes min range). |
| **21** | Garrison Housing / Heal | Health restoration rate while garrisoned. |
| **22** | Blast Width | Area-of-effect blast damage diameter (Druzhina, Logistica). |
| **23** | Search Radius | Target acquisition distance (Tracking adds +2). |
| **100** | Resource Cost (Wood/Primary) | Production cost discount multiplier. |
| **101** | Train / Build Time | Production duration discount multiplier. |
| **102** | Extra Projectiles | Number of additional arrows/shots fired (Yasama adds +3). |
| **103** | Food Cost | Unit food cost multiplier. |
| **104** | Gold / Wood Cost | Multiplier for unit gold or wood cost. |
| **105** | Gold Cost | Unit gold cost multiplier (e.g. `0.85` = -15% gold cost). |
| **106** | Stone Cost | Building stone cost multiplier. |
| **107** | Max Total Projectiles | Maximum arrows fired when garrisoned. |
| **108** | Garrison Heal Multiplier | Herbal Medicine heal rate multiplier (x4). |
| **109** | Health Regeneration Rate | Passive hit point regeneration rate (Berber Maghrebi Camels). |

---

### Common Bonus Recipes

#### 1. Start with +100 Gold
```json
{ "type": 1, "a": 3, "b": 1, "c": -1, "d": 100.0 }
```
*(Type 1: a=3 [Gold], b=1 [Add], d=100.0).*

#### 2. Start with +100 Food and +50 Wood
```json
[
  { "type": 1, "a": 0, "b": 1, "c": -1, "d": 100.0 },
  { "type": 1, "a": 1, "b": 1, "c": -1, "d": 50.0 }
]
```

#### 3. Villagers gather and work 15% faster
```json
{ "type": 5, "a": -1, "b": 4, "c": 13, "d": 1.15 }
```
*(Type 5: a=-1 [All units], b=4 [Class 4: Villager], c=13 [Work rate], d=1.15).*

#### 4. Monks have +50% Hit Points
```json
{ "type": 5, "a": -1, "b": 18, "c": 0, "d": 1.5 }
```
*(Type 5: a=-1, b=18 [Class 18: Monk], c=0 [Hit points], d=1.5).*

#### 5. Infantry move 15% faster
```json
{ "type": 5, "a": -1, "b": 6, "c": 5, "d": 1.15 }
```
*(Type 5: a=-1, b=6 [Class 6: Infantry], c=5 [Speed], d=1.15).*

#### 6. Team Bonus: Monasteries work 20% faster
Put this into the `team_bonus_effects` array:
```json
[
  { "type": 5, "a": 104, "b": -1, "c": 13, "d": 1.2 },
  { "type": 5, "a": 30,  "b": -1, "c": 13, "d": 1.2 }
]
```
*(Type 5: a=104 & a=30 [Monastery units], c=13 [Work rate], d=1.2).*

---

## 7. Tech Tree Changes

Wololoader gives you complete control over your civilization's tech tree without manually editing thousands of binary bytes:

- `"enabled_techs": [id, ...]`: Removes base civ technology restrictions, making those technologies researchable.
- `"disabled_techs": [id, ...]`: Injects tech bans, hiding and preventing those technologies from being researched.
- `"enabled_units": [id, ...]`: Enables production of units not originally available in the base civ.
- `"disabled_units": [id, ...]`: Disables production of units originally available in the base civ.

### Example

To give Britons base access to Redemption and Bloodlines, while removing Cavalier and the Cavalier upgrade:
```json
{
  "enabled_techs": [316, 435],
  "disabled_techs": [209],
  "disabled_units": [283]
}
```

---

### Common Technology IDs Table

*Verified directly against the game data:*

| Tech ID | Technology Name | Internal Dat Name | Building |
|---|---|---|---|
| **101** | Feudal Age | `Middle Age` | Town Center |
| **102** | Castle Age | `Feudal Age` | Town Center |
| **103** | Imperial Age | `Imperial Age` | Town Center |
| **22** | Loom | `Loom` | Town Center |
| **8** | Town Watch | `Town Watch` | Town Center |
| **316** | Redemption | `Redemption` | Monastery |
| **319** | Atonement | `Atonement` | Monastery |
| **231** | Sanctity | `Sanctity` | Monastery |
| **252** | Fervor | `Fervor` | Monastery |
| **233** | Illumination | `Illumination` | Monastery |
| **230** | Block Printing | `Block Printing` | Monastery |
| **438** | Theocracy | `Theocracy` | Monastery |
| **439** | Heresy | `Heresy` | Monastery |
| **441** | Herbal Medicine | `Herbal Medicine` | Monastery |
| **435** | Bloodlines | `Bloodlines` | Stable |
| **39** | Husbandry | `Husbandry` | Stable |
| **209** | Cavalier | `Cavalier` | Stable |
| **265** | Paladin | `Paladin` | Stable |
| **67** | Forging | `Forging` | Blacksmith |
| **68** | Iron Casting | `Iron casting` | Blacksmith |
| **75** | Blast Furnace | `Blast Furnace` | Blacksmith |
| **81** | Scale Barding Armor | `Scale Barding Armor` | Blacksmith |
| **82** | Chain Barding Armor | `Chain Barding Armor` | Blacksmith |
| **199** | Fletching | `Fletching` | Blacksmith |
| **200** | Bodkin Arrow | `Bodkin Arrow` | Blacksmith |
| **201** | Bracer | `Bracer` | Blacksmith |
| **211** | Padded Archer Armor | `Padded Archer Armor` | Blacksmith |
| **212** | Leather Archer Armor | `Leather Archer Armor` | Blacksmith |
| **219** | Ring Archer Armor | `Ring Archer Armor` | Blacksmith |
| **93** | Ballistics | `Ballistics` | University |
| **47** | Chemistry | `Chemistry` | University |
| **51** | Architecture | `Architecture` | University |
| **140** | Guard Tower | `Guard Tower` | University |
| **377** | Siege Engineers | `Siege Engineers` | University |
| **379** | Hoardings | `Hoardings` | Castle |
| **380** | Heated Shot | `Heated Shot` | University |

---

### Castle unique unit (`unique_unit`)

Gives the civilization its own castle unit and elite upgrade. Confirmed in-game: training at the castle, name, rich button tooltip (cost, help, HP / attack / armor / range), tech tree slot and tooltip. Known limits: the tech tree box label shows the unit's internal name (fix in progress), and the tech tree opened from the menu (outside a game) cannot fill the stat tags, so they show as `<invalid tag>`; inside a game they are filled. AI use is not tested.

```json
"unique_unit": {
  "copy_from": 125,
  "name": "Wololo Monk",
  "elite_name": "Elite Wololo Monk",
  "help": "Tough castle monk of the Monklanians.",
  "units": { "hit_points": 60, "max_range": 11 },
  "elite_units": { "hit_points": 80 },
  "elite_cost": { "food": 1000, "gold": 800 },
  "elite_time": 60
}
```

| Key | Required | Meaning |
|---|---|---|
| `copy_from` | yes | Unit id to clone (must be a trainable unit, type 70). |
| `elite_copy_from` | no | Unit id cloned for the elite version (default `copy_from`). |
| `name` / `elite_name` | `name` yes | Display names (`elite_name` defaults to "Elite " + name). |
| `help` | no | Help text. |
| `units` / `elite_units` | no | Field overrides like the civ `units` key; `elite_units` is applied on top of `units` for the elite. |
| `elite_cost` | no | Elite upgrade cost (default food 1000, gold 800). |
| `elite_time` | no | Elite upgrade research time in seconds (default 60). |

Second example: `examples/Woodlanders` clones the Knight (`"copy_from": 38`) into the Axe Rider, a melee cavalry unique unit, so its tooltip shows attack and armor values:

```json
"unique_unit": {
  "copy_from": 38,
  "name": "Axe Rider",
  "help": "Heavy forest cavalry of the Woodlanders. Strong against buildings and infantry.",
  "units": { "hit_points": 110, "line_of_sight": 6 },
  "elite_units": { "hit_points": 140 }
}
```

How it works: the unit and its elite go into free dat slots (1224 and up, two per civ), trained at the castle (button 1). Two techs are cloned from the base civilization's own unique unit techs: one makes the unit available in the Castle Age, one is the elite upgrade at the castle. Names use string ids 28500 + (civ - 35) * 20 + 0..9 (must stay below 32768: the game reads them as signed 16-bit), served by Wololoader.

---

### Unique building (`unique_building`)

A building only this civ can build, cloned from an existing building and placed on the villager's build menu:

```json
"unique_building": {
  "copy_from": 104,
  "name": "Abbey",
  "help": "The great abbey of the Monklanians.",
  "page": "mods",
  "button": 1,
  "age": 2,
  "slp": 15001,
  "units": { "hit_points": 2400 }
}
```

| Key | Required | Meaning |
|---|---|---|
| `copy_from` | yes | Building to clone (type 80): its size, stats, construction and destruction graphics. |
| `name`, `help` | name yes | Button name and tooltip text (served by Wololoader). |
| `page` | no | `"mods"` (default): a third villager build page for mod buildings, reached with the next-page button (economy -> military -> mod buildings); or `"economy"` / `"military"`. |
| `button` | no | Button slot 1..13 on that page (default 13; slots 14 and 15 belong to the game's own buttons). On the economy page 13 is the Portuguese Feitoria's slot. |
| `age` | no | 0 Dark, 1 Feudal, 2 Castle (default), 3 Imperial: when it becomes available. |
| `slp` | no | SLP id of the building's own picture, a file `<mod>/resources/_common/drs/graphics/<slp>.slp`. Without it the copied building's picture is used. |
| `units` | no | Field overrides, same names as in `units`. |

The building goes into a free unit slot (1400 down, the castle unique units count up from 1224) and a free tech makes it available in the chosen age. Units are trained where their own `train_location_id` says, so a copy of the Monastery trains no monks. It does not appear on the tech tree screen.

**What the building does: `commands`.** A list of buttons for the building's own panel (slot 1..15):

```json
"commands": [
  { "button": 1, "train": 125 },
  { "button": 2, "train": { "copy_from": 125, "name": "Abbot", "help": "A senior monk.", "units": { "hit_points": 60 } } },
  { "button": 6, "research": { "name": "Holy Bells", "help": "Monks +20 HP.", "cost": { "food": 100, "gold": 200 },
                               "time": 40, "effects": [ { "type": 4, "a": -1, "b": 18, "c": 0, "d": 20 } ] } },
  { "button": 11, "lua": "Bless", "text": "Bless", "help": "Pay 50 gold for a monk.", "icon": 49 }
]
```

- `train`: a unit trained here. The game allows one training building per unit, so this is a copy of the unit (same name unless you give `name`/`help`; `units` overrides fields). Effects by unit class, such as monk technologies, apply to the copy too.
- `research`: a new technology researched here: `name`, `help`, `cost` (up to 3 of food/wood/stone/gold), `time` in seconds, `effects` (same format as civ `effects`), `age` (default: the building's age), `icon` (technology icon number), `copy_from` (technology whose other fields are kept, default 22 Loom).
- `lua`: a button that runs the mod's Lua code: `Game.OnCommand("Bless", function(building) ... end)`. `icon` is a frame of the command icon sheet 50721 (or an icon you added). It runs only for the player who pressed it; send game commands (`AddGold`, `CreateUnit` ...) from it as usual.

**Finding it in game:** mod buildings have their own villager page. Open it with the **Mod Buildings** button (hammer with the Wololoader shield) on the villager's panel, or with the next-page button (economy -> military -> mod buildings). They use unit slots 1400 down to 1300.

**Extra icons:** a mod can add icons to the game's icon sheets. Put single-frame SLPs in `<mod>/icons/<sheet>/<name>.slp`, where `<sheet>` is `50721` for command buttons or `buildings` for building icons (added to all four culture sheets). They get the numbers after the game's own frames, in mod order and then file-name order.

**Making the picture:** `wololoader/art/buildings/abbey.py` (Blender) shows the pipeline the original game art used. It builds a 3D model, renders it with the game's 2:1 camera (one tile = 96 px wide, sun from the left) at double size with CC0 Poly Haven textures. `to_palette.py` then shrinks it, draws contour lines from a normals pass and maps it to the game palette, with magenta = player colour. `to_slp.py` writes the SLP using `wololoader/tools/slpwrite.py`. Run `blender -b -P abbey.py -- OUT`, then `python3 to_palette.py OUT 50500.bina OUT/abbey` and `python3 to_slp.py OUT abbey 15001.slp`.

## 8. Unique Techs

Every civilization in Age of Empires II has two unique Castle technologies: one unlocked in Castle Age, and one unlocked in Imperial Age.

You configure unique techs with the `unique_techs` key:
```json
{
  "unique_techs": [castle_tech_id, imperial_tech_id]
}
```
If omitted, your civilization automatically inherits the unique technologies of its `base` civilization.

### Verified Unique Technology IDs

| Civ | Castle Unique Tech (ID) | Imperial Unique Tech (ID) |
|---|---|---|
| **Britons** | Yeomen (`3`) | Warwolf (`461`) |
| **Franks** | Chivalry (`493`) | Bearded Axe (`83`) |
| **Goths** | Anarchy (`16`) | Perfusion (`457`) |
| **Teutons** | Ironclad (`489`) | Crenellations (`11`) |
| **Japanese** | Yasama (`484`) | Kataparuto (`59`) |
| **Chinese** | Great Wall (`462`) | Rocketry (`52`) |
| **Byzantines** | Greek Fire (`464`) | Logistica (`61`) |
| **Persians** | Boiling Oil (`488`) | Mahouts (`63`) |
| **Saracens** | Madrasah (`490`) | Zealotry (`9`) |
| **Turks** | Sipahi (`491`) | Artillery (`10`) |
| **Vikings** | Chieftains (`463`) | Berserkergang (`49`) |
| **Mongols** | Nomads (`487`) | Drill (`6`) |
| **Celts** | Stronghold (`482`) | Furor Celtica (`5`) |
| **Spanish** | Inquisition (`492`) | Supremacy (`440`) |
| **Aztecs** | Atlatl (`460`) | Garland Wars (`24`) |
| **Mayans** | Obsidian Arrows (`485`) | El Dorado (`4`) |
| **Huns** | Marauders (`483`) | Atheism (`21`) |

---

## 9. Replacing and Adding Game Files

Wololoader includes a transparent file-system virtualization hook in `files.c`.

### Asset Overrides

Any file placed under your mod's `resources/` folder overrides the corresponding file in `<game>/resources/`.
- The directory path inside `resources/` must mirror the game's path (case-insensitive).
- **Zero modification to game files**: Files are redirected on the fly in memory when the game calls `CreateFileA`/`W`.
- **Conflict Resolution**: If multiple enabled mods override the same file, the mod whose folder name comes later alphabetically takes precedence.

### New Maps and AI Scripts

Wololoader hooks directory enumeration (`FindFirstFileA` / `FindNextFileA`). Any new files your mod provides that do not exist in the vanilla game folder are dynamically inserted into the game's directory listings!
- **Random Maps**: Any `.rms` file placed in `resources/random-map-scripts/` appears directly in the single-player and multiplayer map dropdowns.
- **Custom AIs**: Any `.ai` and `.per` files placed in `resources/ai/` appear directly in the lobby computer AI selector.

### Example File Tree

```
wololoader/mods/MyMod/
├── mod.json
├── civs/
│   └── my_civ.json
└── resources/
    ├── _common/
    │   └── drs/
    │       └── graphics/
    │           └── 50035.slp           # Overrides vanilla sprite 50035
    ├── random-map-scripts/
    │   └── holy_mountain.rms           # Injected: appears in game map list!
    └── ai/
        ├── monk_ai.ai                  # Injected: appears in AI list!
        └── monk_ai.per
```

---

## 10. Text and Description Formatting

The game's text system uses **UTF-8**. Save your JSON files as UTF-8; Wololoader passes the text through unchanged and only replaces invalid bytes with `?` (an invalid byte would otherwise freeze the game's text layout). To ensure your text displays cleanly in-game:

### Crucial Text Rules

1. **Never use the `&` ampersand character**:
   The engine's font renderer interprets `&` as a Windows menu hotkey accelerator. An ampersand will either disappear or corrupt the surrounding text. Always write out `"and"` instead.
2. **Bold Tags use `<b>...<b>`**:
   The game engine's markup parser does not use closing tags with slashes. Both the opening and closing tags are `<b>`.
   - **Correct**: `<b>Monk civilization<b>`
   - **Incorrect**: `<b>Monk civilization</b>`
3. **Line Breaks**:
   Use standard `\n` line breaks in JSON strings for multi-line formatting.
4. **Bullet Points**:
   Characters like `·` (`\u00b7`) work when the file is saved as UTF-8.

---

## Wololoader Base (built-in features)

Wololoader's own gameplay and UI features come as a normal mod, `mods/WololoaderBase`, so they can be
switched off like any other mod on the Mods screen (restart the game after a change). Single features can
be turned off in its `mod.json`:

```json
"features": {
  "popcap_1000": true,     // population limit option 1000
  "selection_250": true,   // select up to 250 units (orders, circles, the +N slot)
  "borderless": true,      // borderless fullscreen instead of exclusive fullscreen (Alt+Tab works)
  "formations_tab": true   // Formations button in the military command page, opens the game's formations page
}
```

If the folder is missing (older installs) every feature is on. Installing never overwrites this file.
Multiplayer: everyone in a game should use the same settings.

## 11. Multiplayer and Vanilla Mode

### Playing Multiplayer with Mods

- *Not verified yet:* players who all have **Wololoader** with the **same mods** may be able to play together. This has not been tested; the safe choice for online games is Vanilla Mode.
- If a player without Wololoader attempts to join a modded lobby, the game will reject the connection or desync due to dat checksum differences.

### Vanilla Mode

Wololoader includes a zero-friction **Vanilla Mode** so you can play in official multiplayer lobbies with friends who do not use mods, without deleting files or uninstalling.

#### How to Toggle Vanilla Mode:
1. **In-Game**: Click the **Mods** button on the main menu (which replaces the Steam Workshop button), then toggle **Vanilla Mode**.
2. **File System**: Create an empty file named `vanilla.flag` in the `<game>/wololoader/` folder:
   ```bash
   touch "<game>/wololoader/vanilla.flag"
   ```
   *(To re-enable mods, simply delete `wololoader/vanilla.flag`).*

While `vanilla.flag` is present:
- All mod loading, dat redirects, and civ hooks are disabled.
- The main menu label displays `Wololoader (Vanilla)`.
- The game loads the original untouched `empires2_x2_p1.dat`.
- **Restart Required**: Because Age of Empires II only reads its data file once upon launching, switching into or out of Vanilla Mode requires restarting the game.

### Population Cap 1000

Wololoader patches the hardcoded population limits in the executable:
- Adds a `"1000"` selection to the population dropdown on the game setup screen and lobby browser filters.
- Lifts the engine clamps on player resources 4 (population headroom) and 32 (bonus population cap) from 500 to 1000.
- Civilizations with population bonuses (such as Huns) receive the full 1000 headroom without building houses.
- Standard population settings (25..500) operate exactly as in vanilla. All players in a 1000 pop multiplayer game must have Wololoader installed.

---

## 12. Scriptorium (Visual Mod Creator)

> [!NOTE]
> Scriptorium is not part of the 0.3 release yet. It can be built from source (`make` builds `build/Scriptorium.exe`); when `Scriptorium.exe` is in the `wololoader` folder, the Mods screen shows a **Mod Creator** button.

Wololoader includes a dedicated visual editor: **Scriptorium** (`<game>/wololoader/Scriptorium.exe`).

Runs natively on Windows and on Linux under Wine/Proton. Scriptorium provides an interactive web-based UI served locally to an application window.

### Features
- **Mod Manager**: Create, enable, disable, and package mods.
- **Scripts**: `scripts/main.lua`, new modules (`New script`, a name like `ai.economy` makes `scripts/ai/economy.lua`) with `Add require to main.lua`, and visual scripts. The node graphs are compiled into `scripts/visual.lua`, which `main.lua` loads with `require("visual")`.
- **Civ Wizard**: Configure civilization identity, pick base civs, configure AI names, and upload sound/art assets.
- **Bonus Builder**: Pre-configured bonus presets (starting resources, unit HP boosts, gathering rate multipliers) plus an advanced effect command editor.
- **Unit Catalog**: Searchable list of all 1300+ game units with base statistics, stat overrides, and side-by-side difference highlighting.
- **Tech Tree Matrix**: 3-state toggles (Base / Enabled / Disabled) for technologies and units.
- **Live Validator**: Runs the dat builder in-memory to catch syntax and logic errors before launching the game.

---

## 13. Troubleshooting and Diagnostics

### 1. Log File: `wololoader.log`
Whenever the game starts, Wololoader creates or truncates:
```
<game>/wololoader/wololoader.log
```
Check this log first if something does not appear as expected. It records:
- Detected and active mods.
- Redirect rules and indexed asset overrides.
- Civilization registration steps and assigned IDs.
- Vectored crash exception dumps (EIP, registers, call stack addresses) if a crash occurs.

### 2. Build Validation Errors
If a civ JSON file contains errors, the builder logs the problem and falls back safely to prevent crashing. Common error messages:
- `missing required key 'name'` / `'dat_name'` / `'base'`: Ensure all three required keys are present.
- `dat_name '...' must be ASCII`: The internal dat name cannot contain non-ASCII characters.
- `unknown key(s) [...]`: Check spelling of JSON keys.
- `unit id ... does not exist in civ ...`: The specified unit ID is out of range for the chosen base civ.
- `field '...' is a structured/raw field and cannot be overridden`: Fields like `attacks`, `armours`, and `damage_graphics` cannot be set directly; use effect commands instead.

### 3. Automatic Cache Rebuilding
The compiled runtime files live in:
```
<game>/wololoader/cache/empires2_x2_p1.dat
<game>/wololoader/cache/civs.txt
```
Wololoader fingerprints your mods and civ files automatically. Whenever you modify a JSON file or toggle a mod, the cache is rebuilt automatically on next startup. You never need to manually delete the cache folder.

---

## 14. Lua scripting

A mod can ship a Lua 5.4 script that reacts to game events. Wololoader embeds the Lua runtime, so nothing has to be installed.

```
<game>/wololoader/mods/MyMod/scripts/main.lua
```

The file is run once at startup, after all mods are scanned (not in Vanilla Mode, and not for disabled mods). Working examples: `wololoader/examples/Monklanians/scripts/main.lua` and `wololoader/examples/DevPanel/scripts/main.lua`. A larger mod can split its code over several files, see [Modules](#modules-require).

```lua
print("loaded " .. MOD.name)
Game.OnStart(function()
    UI.Message("Monklanians: Wololo!")
end)
```

### Scripting API

Every script also gets a simple high-level API, written on top of `wl.*` (which stays available):

| Name | What it is |
|---|---|
| `Game.LocalPlayer`, `Game.Players()`, `Game.Player(id)` | players |
| `Game.OnCommand(name, fn(building))` | a Lua button of a mod building was pressed (see unique_building `commands`) |
| `Game.Time`, `Game.Selected()` | game time in seconds (stops while paused); the units you have selected (first 60) |
| Player | `.Id`, `.IsLocal`, `.Food`, `.Wood`, `.Stone`, `.Gold`, `.Age` (0..3), `.AgeName` ("Dark".."Imperial"), `.Population`, `:IsEnemy(other)`, `:Units(filter)` |
| `p:Units()` / `p:Units("Monk")` / `p:Units(function(u) ... end)` | units; class names: Archer, Trade, Building, Villager, Infantry, Cavalry, Siege, Monk, Transport, FishingShip, Warship, Wall, CavalryArcher, MonkWithRelic |
| Unit | `.Id`, `.X`, `.Y`, `.Type` (dat unit id), `.Class`, `.ClassName`, `.IsBuilding`, `.Built` (a finished building, not a foundation), `.Owner`, `:Delete()` |
| `p:AddGold(n)`, `p:AddFood(n)`, `p:AddWood(n)`, `p:AddStone(n)`, `p:AddResource("Gold", n)` | give (or take, with a negative n) resources |
| `p:CreateUnit(unitId, x, y)` | create a unit (dat unit id, e.g. 125 Monk, 4 Archer) at tile x, y |
| `p:Research(techId)` | finish a technology instantly (skipped if already researched) |
| `p:SetAge(age)` | `0`..`3` or `"Dark"`..`"Imperial"`. Up: the age-up techs are researched. Down: the age goes back and the age-ups can be researched again; units, buildings and bonuses already unlocked stay (the game cannot undo tech effects) |
| `Game.OnStart(fn)`, `Game.OnSecond(fn)` | events; they return a connection with `:Disconnect()` |
| `Game.OnAgeUp(fn(player, ageName))`, `Game.OnUnitCreated(fn(unit))`, `Game.OnUnitDied(fn(id, player))` | checked once a second (up to 1 s late); "died" also fires when a unit is converted or deleted |
| `Game.OnResearch(fn(player, techId))`, `Game.OnBuildingDone(fn(unit))` | a technology was researched (also age-ups and free techs); a building was finished (not when its foundation is placed). Checked once a second; what a player has at the start of a game does not fire |
| `UI.Message(text, {Color=n})`, `UI.Chat(text, {From=, Color=})`, `UI.Dialog(text)` | screen output (Message color: a game palette index, default 36) |
| `local c = UI:CreateCounter(text)` | a line in the countdown area: `c:SetText(t)`, `c:Hide()`, `c:Show()`, `c:Destroy()`; several counters share the line, joined with "  \|  " |
| `UI:CreateFrame(props)`, `UI:CreateLabel(props)`, `UI:CreateButton(props)`, `UI:CreateTextBox(props)` | on-screen panels, text and buttons that do **not** pause the game (see below) |
| `UI.Ask(text)` | Yes/No dialog, returns true/false (nil in multiplayer); `UI.Ask(text, fn)` for a callback |
| `Sound.Play(name)` | `Chat`, `CantDo`, `Tribute`, `Attack`, `TownBell`, `TownRecall`, `Objective`, `Relic` |
| `Time.Wait(s)`, `Time.After(s, fn)`, `Time.Every(s, fn)`, `Time.Cancel(h)` | timers in game seconds: they stop while the game is paused (F3, F10 menu) and follow the game speed (`Time.Wait` only inside events/timers) |

Reading players and units never changes the game, so it is multiplayer safe. Examples: `examples/Monklanians/scripts/main.lua`, and `examples/DevPanel` (a testing panel: resources, ages, spawning, cloning, deleting).

**On-screen UI.** Frames, labels and buttons are drawn over the game every frame; the game keeps running and you click them while playing. Create them once (at the top of `main.lua`) and change their properties later:

```lua
local panel = UI:CreateFrame({ Anchor = "TopRight", Position = {10, 60}, Size = {190, 70} })
local label = UI:CreateLabel({ Parent = panel, Text = "Gold: 0", Position = {0, 6}, Size = {190, 22}, Align = "Center" })
local button = UI:CreateButton({ Parent = panel, Text = "+50 gold", Position = {10, 34}, Size = {170, 28}, Hotkey = "F5" })
button:OnClick(function(b) Game.LocalPlayer:AddGold(50); b.Text = "Done!" end)
button:OnHover(function(b) b.TextColor = "#ffff00" end)
button:OnLeave(function(b) b.TextColor = "#ffffff" end)
Game.OnSecond(function() label.Text = "Gold: " .. math.floor(Game.LocalPlayer.Gold) end)
```

| Property | Value |
|---|---|
| `Text` | text (labels wrap, buttons are one centered line) |
| `Position`, `Size` | `{x, y}` / `{w, h}` in pixels; Position is measured from the `Anchor` edge inwards |
| `Anchor` | `TopLeft` (default), `Top`, `TopRight`, `Left`, `Center`, `Right`, `BottomLeft`, `Bottom`, `BottomRight`: where it sticks inside its parent (or the screen) |
| `Parent` | another element: position is relative to it, it hides and is destroyed with it |
| `Visible` | `true`/`false` (also `:Show()`, `:Hide()`) |
| `Color`, `TextColor`, `BorderColor` | `"#RRGGBB"` or `{r, g, b}`; `Color = false` / `BorderColor = false` removes the background / border |
| `Transparency` | background 0 (solid) .. 1 (invisible) |
| `Placeholder`, `MaxLength`, `Numeric` | text boxes: grey hint while empty, max characters (default 32), digits only |
| `TextSize`, `Bold`, `Align` | pixels (default 16), `true`/`false`, `Left`/`Center`/`Right` |
| `Hotkey` | `"F5"`, `"A"`.`"Z"`, `"0"`.`"9"`, `"F1"`.`"F12"`, with optional `"Ctrl+"`, `"Shift+"`, `"Alt+"` prefixes (e.g. `"Ctrl+S"`, `"Alt+F4"`); fires a click when pressed while the button is shown |
| `Image` | path to a PNG file relative to the mod folder (e.g. `"icons/gold.png"`); drawn scaled into the element rect; buttons with both text and image show the image left of the text |

Text boxes: click to type (the game gets no keys meanwhile), Enter submits, Esc or a click elsewhere leaves; `box.Text` is the current text, `:OnChanged(fn(box, text))` fires on every edit, `:OnSubmit(fn(box, text))` on Enter, `:Focus()` puts the cursor in it.

Methods: `:OnClick(fn(element))`, `:OnHover(fn(element))`, `:OnLeave(fn(element))` (buttons and frames; each returns a connection with `:Disconnect()`, and `Time.Wait` works inside the handlers), `:SetText(t)`, `:SetPosition(x, y)`, `:SetSize(w, h)`, `:Show()`, `:Hide()`, `:Destroy()`; `.Destroyed` tells whether it is gone. Clicks on frames and buttons do not reach the game below them; labels let clicks through. The UI is hidden automatically when another screen (F10 game menu, pause dialogs) is on top of the game screen. At most 256 elements, 64 cached images. The UI is local: each player sees only what the scripts on their own machine made, and click/hover handlers run only on the clicking player's machine, so game commands sent from a handler (like `AddGold` above, before any `Time.Wait`) are always sent from that machine.

**Game commands** (`Add*`, `CreateUnit`, `Research`, `Delete`) go through the game's own command system, so in multiplayer every player sees the same result on the same turn. Every player runs the same scripts, so only one machine sends each command: the owner's machine for a human player, the lowest-numbered human player's machine for computer players and Gaia. The call returns `true` on the machine that sent it and `false` on the others; you can write `Game.Player(2):AddGold(100)` without checking `IsLocal`. Wrong arguments (unknown unit id, no game running) raise an error. At most 100 commands a second go to the game; more wait in a queue and follow on the next frames (up to 4096 waiting), so `for i = 1, 1000 do p:CreateUnit(...) end` works, it just takes 10 seconds. All players need the same Wololoader version.

### Modules: `require`

`main.lua` can load other Lua files of the same mod with `require`. Put them in the `scripts/` folder of the mod. A dot in the name is a folder:

```
scripts/main.lua               runs at start, loads the modules below
scripts/economy.lua            require("economy")
scripts/ai/economy.lua         require("ai.economy")
scripts/visual.lua             written by Scriptorium from the visual scripts; main.lua loads it with require("visual")
```

```lua
-- scripts/main.lua
local economy = require("ai.economy")
economy.start()
```

```lua
-- scripts/ai/economy.lua
local M = {}

function M.start()
    Game.OnSecond(function()
        -- ...
    end)
end

return M
```

- **Names**: 1 to 64 characters, letters, digits and `_`, with a single `.` between parts. Anything else is an error (`bad module name`): `..`, `/`, `\`, `:` (so `C:` and absolute paths), spaces, an empty name. Only the `scripts/` folder of your own mod is read; no other file of the game or of another mod can be loaded.
- **Same environment**: a module runs in the same environment as `main.lua`. Its globals, `Game`, `UI`, `Time` and `wl` are the same as in `main.lua`.
- **Runs once**: the first `require` runs the file. The returned value is kept, and the next `require` of the same name returns it without running the file again. A module that returns nothing gives `true`; one that returns `false` gives `false`. The cache lasts as long as the game runs (it is not reset when a new game starts).
- **Errors**: an error in a module is written to `wololoader.log` with the file name (`...\scripts\ai\economy.lua:3: ...`) and raised in the code that required it. A module that failed is not cached, so the next `require` runs it again.
- **Circular requires** (a module that requires itself, directly or through other modules) are an error: `circular require of 'name'`.
- **Text only**: precompiled Lua (binary chunks) is refused.
- **Budget**: the instructions of a module count against the same 10 million instruction limit as the script that required it.
- **Top level**: `Time.Wait` / `wl.wait` cannot be used while a module is being loaded (the same as at the top level of `main.lua`). Call it from a function the module defines, and run that function from an event.
- Not available: `package`, `package.loaded` and `package.path`, `dofile`, `loadfile`. A module cannot be replaced or reloaded from a script.

### Low-level `wl` API

The Scripting API above is built on these functions; they stay available.

### Sandbox

- Available: `assert, error, pcall, xpcall, select, type, tostring, tonumber, pairs, ipairs, next, rawget, rawset, rawequal, rawlen, getmetatable, setmetatable, collectgarbage`, the libraries `string`, `table`, `math`, `utf8`, `coroutine`, and `require` for the mod's own modules.
- Not available: `io`, `os`, `package`, `debug`, `dofile`, `loadfile`, `load`, `warn`, `string.dump`. Scripts cannot read or write files or run programs. Precompiled (binary) Lua is rejected.
- `require` exists, but it is the mod's own: it only loads `scripts/<name>.lua` of the same mod, with the rules in [Modules](#modules-require). The file name is checked before any path is built, so `require("../x")`, `require("/x")`, `require("C:x")` and `require("a\\b")` are refused and nothing outside `scripts/` is read.
- `collectgarbage` only accepts `"collect"`, `"count"` and `"step"`. `getmetatable("")` returns `"locked"` (the string metatable is shared by all mods and cannot be changed).
- `xpcall(f, handler, ...)` calls `handler` after `f` has failed (with the error message), not while the error is being raised, so the handler gets no traceback of `f`.
- Pattern functions (`string.find`, `match`, `gmatch`, `gsub`, and the same methods on strings) refuse a pattern that could take too long on the given text (many `*`, `-`, `+` on a long text): they raise "pattern too complex". Use plain `find(text, s, 1, true)` or simpler patterns.
- Every mod runs in its own environment: globals you assign are private to your mod, and your copies of `string`, `table`, `math` etc. are not shared with other mods.
- `print(...)` is the same as `wl.log(...)`.
- A single script run or event handler is aborted with an error after 10 million Lua instructions (so an endless loop cannot freeze the game). The count includes every coroutine the call runs, and the abort cannot be caught with `pcall`, `xpcall` or `coroutine.resume`. A `require` inside the call shares this count. The Lua state is limited to 64 MB.
- Errors never crash the game. They are written to `wololoader.log` as `[lua:<mod id>] ...` (a module's errors name the module file). A handler that raises an error is disabled (the other handlers keep running); an error in `main.lua` itself keeps the handlers registered before it.
- Log lines longer than about 250 characters are cut off. A mod writes at most 2000 log lines per game; after that its output is not logged until the next game.
- UI images (`Image`) must be files inside the mod folder: absolute paths and `..` are refused.

### The `MOD` table

`MOD.id` (folder name), `MOD.name`, `MOD.version` (from `mod.json`, empty string if missing), `MOD.dir` (absolute mod folder).

### The `wl` API

| Function | What it does |
|---|---|
| `wl.log(...)` | Writes the arguments (tab separated, like `print`) to `wololoader.log` as `[lua:<mod id>] ...`. |
| `wl.on(event, fn)` | Registers `fn` for the event. Several handlers per event and per mod are allowed; they run in mod order, then registration order. Events with an integer argument pass it to `fn`. |
| `wl.message(text)` | Shows a short message in the game.  |
| `wl.dialog(text)` | Shows a dialog box with the text. Same fallback as `wl.message`. |
| `wl.chat(text [, sender [, player]])` | Adds a line to the in-game chat history. `sender` is the name shown before the text (default: none, a system line). `player` 1..8 uses that player's colour (default and out-of-range values: the game's neutral system colour). |
| `wl.ask(text, callback)` | Shows a Yes/No dialog and later calls `callback(true)` (Yes) or `callback(false)` (No). One dialog is open at a time, shared with `wl.dialog`; more are queued. **In multiplayer no dialog is opened**: the text appears as a notification and `callback(nil)` is called on the next frame. Always handle `nil`. |
| `wl.sound(name_or_id)` | plays a game UI sound now. Names: `chat`, `cantdo`, `tribute`, `attack`, `townbell`, `townrecall`, `objective`, `relic`; or an id 0..50 (full table in `re/research/sound_counter.md`). Only the local player hears it. |
| `wl.counter(text)` | shows one line of your own text in the game's countdown-clock area (where `Wonder: 300 Years` appears) and keeps showing it until you change it; call it again as often as you like (e.g. from `wl.every(1, ...)`) to update the text. `wl.counter(nil)` or `wl.counter("")` hides it. Text is cut to 100 characters. There is one line per game (the last panel of the game's ten timer panels): if the game itself shows ten timers at once, your line is not shown. It is cleared when a new game starts. Local to each player. |
| `wl.after(seconds, fn)` | Calls `fn()` once after `seconds` (game time). Returns a handle. |
| `wl.every(seconds, fn)` | Calls `fn()` every `seconds` (game time, minimum 0.05). Returns a handle. |
| `wl.cancel(handle)` | Cancels a timer from `wl.after` / `wl.every`. Returns `true` if it was still active. |
| `wl.wait(seconds)` | Pauses the running event handler or timer callback for `seconds` (game time) and then continues. Only works inside handlers, timer callbacks and ask callbacks (they run as coroutines); calling it at the top level of `main.lua` raises an error. Call it directly from the handler, not from your own `coroutine.create` threads. |
| `wl.version` | The Wololoader version string, for example `"0.3"`. |

### Timers, waiting and multiplayer

- Timers and `wl.wait` use **game time**: they stop while the game is paused (F3, F10 menu) and follow the game speed. They are driven from the game screen's per-frame update.
- All timers, waiting coroutines and open questions are dropped when a new game starts or the game screen goes away. Create them from `game_start` (or later), not at the top level of `main.lua`.
- An error in a timer callback or in a coroutine after a `wl.wait` is logged and ends only that call (a repeating timer keeps running). The 10 million instruction limit applies to every resume separately. At most 64 timers, 64 waiting coroutines and 16 open questions per game.
- **Multiplayer**: dialogs that pause or block the game are never shown (`wl.dialog` and `wl.ask` become notification lines, see above). Timers run on each player's machine separately; to change the game, use the game commands of the Scripting API (`AddGold`, `CreateUnit`, ...), which are sent from one machine only and arrive for everyone on the same turn.

### Events

| Event | Argument | Status |
|---|---|---|
| `game_start` | none | fired when a game starts (`Game.OnStart`) |
| `every_second` | none | fired once per game second (`Game.OnSecond`) |
| `game_end` | none | planned, not fired yet |

## Custom taunts

Put sound files in a `taunts/` folder of your mod, named like the game's own taunts: the number, a space, a name, `.mp3` (for example `taunts/43 wololo again.mp3`). Typing `43` in chat then plays it. The game ships taunts 1 to 42; chat accepts numbers up to 256. A file with the same number and name as a game taunt (for example `30 wololo.mp3`) replaces it. Taunts are sent as chat text, so they do not change the game data, but other players only hear the sound if they have the mod too. Mod taunts also show in the in-game taunt list (F11).

## Dev Panel and console

Wololoader includes an in-game **Dev Panel** and standalone **Lua console** as part of `mods/WololoaderBase` (can be disabled via `"dev_panel": false` in `WololoaderBase/mod.json`).

- **Toggle**: click the **DEV** button (top-left) or press `Ctrl+Shift+D` for the Dev Panel. Press `Ctrl+Shift+C` or click the **Console** button in the Dev Panel to toggle the Lua console window (`Esc` closes it).
- **Actions**: add/reset resources, change age, research economy/blacksmith techs, spawn 1–1000 units, clone/delete selected units, and kill/raze enemies. Actions use the multiplayer-safe command path (`gamecmd.c`).
- **Lua console**: opens as a dark, slightly transparent panel docked on the right side of the screen (40% width, between the top resource bar and bottom command panel, adapting to resolution and window resizes) and automatically focuses the bottom input line without leaking typed keys to the game. Press `Enter` to run expressions or statements (auto-returns expressions; `= expr` is also supported). Use the mouse wheel, `PageUp`/`PageDown`, or `^`/`v` to scroll, `Up`/`Down` for command history, and `Clr` or `clear()` to clear. Built-in helpers: `help()` (prints the grouped `Game`, `Player`, `Unit`, `UI`, and `Events` API), shortcuts `me` (`Game.LocalPlayer`), `sel` (`Game.Selected()`), and `players` (`Game.Players()`), pretty-printing of tables (one level deep with keys) and `Player`/`Unit` objects, and `print(...)` to console output. Runs in a dedicated sandboxed environment with the same instruction/memory limits as mod scripts (no `io`, `os`, or bytecode loading).
- **Multiplayer rule**: in single player the Dev Panel and console are always available. In multiplayer (`Game:IsMultiplayer()`), they are disabled unless cheats are enabled in the match settings (`Game:CheatsEnabled()`), enforced in both Lua and C.

## Per-Civilization Victory Sound (`victory_sound`)

A custom civilization JSON can specify an optional `"victory_sound"` identity key pointing to an `.mp3` or `.wav` file inside your mod folder (for example `"victory_sound": "sounds/victory.mp3"`). You can also set it in Scriptorium under the civilization editor's **Theme Audio** section.

- When a match ends and the **local player** has won, Wololoader plays that civilization's `victory_sound` on the music channel instead of the game's default victory music (`won1.mp3` / `won2.mp3`).
- If the local player lost, or if their civilization has no `victory_sound` (or the file is missing/unreadable), vanilla victory/defeat music plays as usual.

---

## Custom Units (`custom_units`)

Any civilization JSON (including `"extend"` mods) can define an optional `"custom_units"` array to add brand-new trainable units. Each entry allocates a deterministic unit slot in the `1224..1400` `RESERVED` pool (right after `unique_unit` slots), assigns string IDs in `strings.txt`, and enables the unit **only** for that civilization at the chosen age.

```json
"custom_units": [
  {
    "id": "forest_tracker",
    "name": "Forest Tracker",
    "copy_from": 4,
    "help": "Light woodland bowman with extra speed and sight.",
    "cost": { "wood": 25, "gold": 40 },
    "hit_points": 35,
    "attack": 5,
    "armor": { "melee": 0, "pierce": 1 },
    "speed": 1.05,
    "line_of_sight": 7,
    "available_in_age": 1,
    "icon": 15000,
    "graphics": {
      "standing": 15010,
      "walking": 15011,
      "attack": { "slp": 15012, "frames": 10 },
      "dying": 15013
    },
    "trained_at": {
      "building": 87,
      "page": 1,
      "button": 6
    }
  }
]
```

### Supported Keys

| Key | Required | Description |
|---|---|---|
| `id` | **Yes** | Short unique string key within this civilization (1–64 chars). |
| `name` | **Yes** | Display name (also truncated to 30 ASCII chars for the internal dat name). |
| `copy_from` | **Yes** | Template unit ID (must be a trainable unit, type `70`). |
| `help` | No | Tooltip description (defaults to `name`). |
| `cost` | No | Object with any 1–3 of `food`, `wood`, `stone`, `gold` (`0..32767`). Preserves population cost when using up to 2 resources. |
| `hit_points` | No | Maximum HP (integer). |
| `attack` | No | Base attack damage (integer `0..32767`, updates `displayed_attack` and primary melee/pierce attack). |
| `armor` | No | Integer for melee armor, or `{"melee": n, "pierce": n}`. |
| `pierce_armor` | No | Pierce armor (integer `0..32767`). |
| `range` | No | Sets both `max_range` and `displayed_range`. |
| `speed` | No | Movement speed in tiles/sec. |
| `line_of_sight` | No | Fog-of-war vision radius. |
| `train_time` | No | Training duration in seconds. |
| `units` | No | Advanced field overrides using the same dat field names as top-level `units` (applied before convenience stat keys). |
| `graphics` | No | Custom animation SLPs (`standing`, `standing2`, `walking`, `running`, `attack`, `dying`, `decay`) in `<mod>/resources/_common/drs/graphics/<slp>.slp`. |
| `icon` | No | Vanilla unit icon frame index (`0..999` in `50730.slp`) or name/number of a 1-frame SLP in `<mod>/icons/units/<name>.slp` (merged into `50730.slp` starting at frame 249). |
| `available_in_age` | No | Age when the unit unlocks: `0` Dark (default), `1` Feudal, `2` Castle, `3` Imperial. |
| `trained_at` | No | `{ "building": <unit_id or "unique_building">, "page": n, "button": 1..14 }` — registers a train button in the building's command panel via `commands.txt`. |

---

## Quick-Testing a Civilization (`Test it` / `autostart.json`)

Scriptorium includes a **Test it** button on each civilization row and in the civilization editor header to drop you straight into a single-player Random Map match with your civilization, skipping all menus.

- **How it works**: Scriptorium saves any pending edits, enables the mod if it was disabled, writes `<game>/wololoader/autostart.json`, and launches AoE2 HD via Steam (`steam://rungameid/221380`) if it is not already running. If the game is already running in a match, return to the Main Menu — Wololoader polls `autostart.json` while the Main Menu is active (~0.5 s), deletes the file immediately, configures single-player `GameSettings`, and launches the match automatically.
- **Match options popover** (caret button next to **Test it**): choose map (`Arabia`, `Arena`, `Black Forest`, `Islands`, etc.), map size (`tiny`..`giant`), AI count (`0..7`), AI civilization (`random`, `mirror`, or any vanilla civ), difficulty (`easiest`..`hardest`), starting resources (`standard`, `low`, `medium`, `high`), reveal map, and cheats enabled. Defaults are **Arabia, Tiny, 1 Easiest AI, Standard resources, cheats on**, remembered in `localStorage`.
- **Manual use**: you can also write `<game>/wololoader/autostart.json` by hand:

```json
{
  "civ": 35,
  "map": "Arabia",
  "size": "tiny",
  "ai": 1,
  "ai_civ": "random",
  "difficulty": "easiest",
  "resources": "standard",
  "reveal_map": false,
  "cheats": true
}
```

*(Never runs in Vanilla Mode or in multiplayer.)*

---

## Hot Reload (Lua scripts)

Wololoader automatically watches `<mod>/scripts/*.lua` (including subfolders) for every enabled mod while a game is running (checked about once per second with a 250 ms debounce so half-written saves are not loaded):

- **Clean per-mod reset**: Only the changed mod is unloaded and re-run in a fresh sandbox with a cleared `require` cache. Everything that mod registered is removed first: event handlers (`Game.On*`, `wl.on`), `Game.CommandCondition`, timers (`Time.After`/`Every`), waiting coroutines and `UI.Ask` callbacks, UI elements (frames, labels, buttons, text boxes, hotkeys), and counters. Other mods are untouched.
- **Mid-game `Game.OnStart`**: When a mod reloads while a match is already in progress, its `Game.OnStart` handlers run again right after `main.lua` succeeds so match-start timers and UI setup work without restarting the match.
- **Errors**: On a syntax or runtime error, the mod stays unloaded, the full error is written to `wololoader.log`, and a short in-game notification (`Reload failed: <mod>: <error>`) is shown; on success, `Reloaded <mod>` is shown.
- **Manual reload**: Run `reload("<mod>")` or `reload()` (all changed mods) in the Dev Console, or call `wl.reload("<mod>")` / `wl.reload()` from Lua.
- **Multiplayer**: Both automatic and manual hot reloads are disabled in multiplayer unless cheats are enabled.

---

## In-Game Mod Hub

You can browse, install, update, and uninstall community mods directly inside Age of Empires II HD without opening Scriptorium:

1. From the Main Menu, open **Mods** and click **Mod Hub** in the top-right corner. (The Mod Hub button is hidden in the multiplayer lobby read-only view.)
2. Browse the paged mod list (icon, name, version, author, and Installed/Update badge). Select any mod to view its description, size, tags, and dependencies.
3. Click **Install** or **Update** to download and verify the archive (SHA-256, size, and safe ZIP checks run in a background thread with live progress in the status line; dependencies are installed first). Click **Uninstall** to move a Hub-installed mod to `wololoader/trash/`.
4. Click **Refresh** to reload the remote index, or **Back** to return to the **Mods** screen (which rescans installed mods and hot-reloads changes when returning to the Main Menu).

---

## Main-Menu Mod Hot Reload (Level 2)

Wololoader applies content-mod changes **without restarting the game** whenever you are at the Main Menu (outside any match, editor, or lobby):

- **Automatic trigger**: Leaving the in-game **Mods** screen (or returning from **Mod Hub** and clicking **Back**) automatically detects enabled/disabled mods, Hub installs/updates/uninstalls, and external edits made in Scriptorium or directly on disk.
- **Manual trigger**: Click **Reload Mods** in the top-right corner of the **Mods** screen, or run `reload_mods()` in the Dev Console / `wl.reload_mods()` from Lua (only permitted at the Main Menu when no match or lobby is active).
- **What reloads live**:
  - `.dat` data changes (units, techs, effects, bonuses, custom civilizations) — rebuilt into `wololoader/cache/empires2_x2_p1.dat`, with cached `GameDataSet` CRC32 invalidated so multiplayer lobby checks use the new `.dat`.
  - Custom civilizations (added, removed, or modified) — reconciled in the game's civ registry (`DlcPackage`, `Civilization` registry, and unique-tech table); persistent Profile `GameSettings` slots referencing a disabled custom civ are reset to **Random**.
  - Custom strings, descriptions, history entries, selection/victory sounds, interface art, merged icon sheets (`50721`, `50705..50708`, `50730`), RMS/AI/taunt directory listings, and asset overrides.
  - DRS packages (`graphics`, `sounds`, `terrain`, and `interface` when brand-new filenames are added/removed).
  - All Lua mod scripts (full runtime restart) and the multiplayer content checksum (`[WL]` lobby tag).
- **What still requires a restart**:
  - **Vanilla Mode** (on/off).
  - **Wololoader Base** binary engine patches applied at startup (`popcap_1000`, `selection_250`, `borderless`, `formations_tab`). When any of these change, Wololoader reports `Restart required for: ...` on the Mods screen and Main Menu label.
- **Error recovery**: If a mod fails validation during reload, Wololoader logs the error, clears the broken cache so the game stays playable with the original `.dat`, and shows the error on the Mods screen.





