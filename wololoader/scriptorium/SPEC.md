# Scriptorium — Wololoader's mod creator

A standalone Windows program (`Scriptorium.exe`, 32-bit, C, Win32 API, built with
`i686-w64-mingw32-gcc`) that creates and edits Wololoader mods. No game patching:
it only reads the player's game files and writes into `<game>\wololoader\mods\`.
Runs on Windows and on Linux through Wine/Proton.

Installed at `<game>\wololoader\Scriptorium.exe`, so the game folder is the parent
of the exe's folder (fallback: `--game "<dir>"` argument, then a folder picker).
Later the in-game Mods screen gets a "Creator" button that launches it.

## Files

| File | Owner | What |
|---|---|---|
| `catalog.h` | contract (do not change) | Game data API: civs, units + stats, techs. |
| `catalog.c` | job A | Implements `catalog.h` (dat parse + strings file). |
| `../tools/test_catalog.c` | job A | Native Linux test (system zlib): prints the catalog. |
| `main.c`, `ui_*.c`, `modio.c/.h`, `sc_zlib.c` | job B | The GUI program. |
| `../Makefile` target `scriptorium` | job B | Builds `build/Scriptorium.exe`; `install` also copies it. |

Reused unchanged from `../src`: `json.c/.h`, `datbuild.c/.h` (for "Validate").
Never change `datbuild.c` behaviour (it must stay byte-identical to `tools/build.py`).

## Mod format (what Scriptorium reads and writes)

Same files Wololoader already uses — see `../examples/Monklanians/`:

- `mods\<Folder>\mod.json`: `name`, `version`, `author`, `description`, optional `enabled`.
- `mods\<Folder>\civs\<file>.json`, one per civ. Keys:
  `name`, `dat_name` (ASCII, max 19 chars), `base` (civ id 1..31),
  `description` (text, `\n` newlines, `<b>..<b>` bold, never use `&`),
  `ai_names` (list of strings), `sound`, `interface_slp`,
  `units` (object: unit id string -> { field: number }),
  `enabled_techs`, `disabled_techs`, `enabled_units`, `disabled_units` (lists of ids),
  `effects`, `team_bonus_effects` (lists of `{type,a,b,c,d}`), `unique_techs` ([castle, imperial]),
  plus advanced keys (`tech_tree`, `team_bonus`, `resources`) that Scriptorium must
  **preserve untouched** when it saves a file it loaded.
- Saving rewrites the json pretty-printed (2-space indent), keeps unknown keys and
  key order where possible, writes UTF-8 without BOM, atomically (temp file + rename).

Effect command cheat sheet: 0 set attribute, 1 resource (a=res, b=0 set / 1 add, d=amount),
2 enable unit (a, b=0/1), 3 upgrade unit, 4 add attribute, 5 multiply attribute
(a=unit, b=class, c=attr, d), 6 multiply resource, 101 tech cost, 102 disable tech (d=tech),
103 tech research time. Attribute ids: 0 hit points, 1 line of sight, 5 speed, 12 range,
13 work rate, 10 reload time.

## Window layout (version 1)

Main window ~1000x680, title "Scriptorium — Wololoader mod creator".
Look: parchment-like background (#E8D9B5), dark brown text (#3B2A1A), Georgia font,
standard Win32 controls. (AoE2 art from the player's game files is a later step.)

- Left column: list of mods found in `mods\` (name + folder), buttons
  **New mod**, **Open folder**. Selecting a mod loads it. Under it, the list of the
  mod's civs with **New civ** / **Delete civ**.
- Right side: tab control with the pages below, and at the bottom
  **Save**, **Validate**, **Revert**. Status line shows the last result.
- Unsaved changes: ask before switching mod / closing.

### Tab "Mod"
Name, version, author, description (multi-line), enabled checkbox.

### Tab "Civilization" (new civ wizard = this tab with defaults)
Name, dat_name (auto from name, ASCII only, max 19), base civ (combobox from
catalog civs), description (multi-line, with a "Generate from bonuses" button that
writes a bullet list), AI names (multi-line, one per line), sound file and
interface art file (pick a file: it is copied into the mod folder, the key stores
the file name relative to the mod folder).

### Tab "Bonuses"
A checklist of presets, each with one number to edit, turned into `effects` /
`team_bonus_effects` entries. Presets v1:
- Start with +N gold / wood / food / stone  → type 1 (a=3/1/0/2, b=1, d=N)
- Villagers +N% work rate → type 5 on class 4 (a=-1, b=4, c=13, d=1+N/100)
- Monks +N% hit points → type 5 (a=-1, b=18, c=0)
- Infantry / Archers / Cavalry +N% hit points → type 5 on classes 6 / 0 / 12
- Team bonus: Monasteries work N% faster → team_bonus_effects type 5 on units 104 and 30, c=13
Below: an "Advanced effects" list (type, a, b, c, d) for anything else, add/remove rows.
Effects that do not match a preset are shown in the advanced list (no data is lost).

### Tab "Units"
Filter box + list of catalog units (id, display name, internal name). Selecting a
unit shows the editable stats of `catalog.h` (only those with `has`), with the base
civ's original value next to each field. A changed value is stored in
`units[<id>][<field>]`; "Reset" removes the override. Units with an override are
marked in the list.

### Tab "Tech tree"
Two filterable lists (Techs, Units) with a three-state column per row:
"base" (no entry), "enabled" (in `enabled_techs` / `enabled_units`),
"disabled" (in `disabled_techs` / `disabled_units`). Plus two combo boxes for
`unique_techs` (castle age, imperial age), defaulting to the base civ's.

### Validate
Writes the current state to disk in a temp copy of the mods folder containing only
this mod, runs `wl_dat_build` (from `datbuild.c`) into a temp folder, and shows
"OK" or the error message. zlib comes from the game's `zlibwapi.dll`
(see `../src/zlib_game.c`, adapt it without the DLL's globals).

## Command line (for agents: tests run under `wine`)

- `Scriptorium.exe --game "<dir>" --selftest` — no window: load the catalog, print
  counts and a few entries, load every mod, save each into a temp folder, re-parse
  and compare the json trees (must be equal), run Validate on each; exit 0 if all OK.
- Every error goes to stderr and to `<game>\wololoader\scriptorium.log`.

## Rules
- Comment the why; the next agent may be a smaller model.
- Zero warnings with `-Wall -Wextra`.
- Never write into the game folder outside `wololoader\mods\` (and the log).
- Text: the GUI is UTF-8 internally, use the W (wide) Win32 APIs for display and
  file paths, convert with MultiByteToWideChar(CP_UTF8).
