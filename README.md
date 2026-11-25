# Wololoader

**Website: https://wololoader.vercel.app** · [Download](../../releases/latest) · [Modding guide](https://wololoader.vercel.app/guide/)

A mod loader for **Age of Empires II HD** (Steam, version 5.8). It runs as a `version.dll` next to the
game and adds, without replacing the game's data files:

- **Custom civilizations** (civ id 35 and up) with their own units, unique unit, techs, names and AI names.
- **Unique buildings** with their own art, on a villager "Mod Buildings" page, with train / research /
  Lua command buttons.
- **Lua scripting**: game events, commands (resources, units, techs, ages), timers on game time.
- **Non-pausing on-screen UI** for mods: frames, labels, buttons, text boxes, images, hotkeys.
- **Custom icons** and asset overrides (`<mod>/resources/...` replaces the game's files).
- **Custom units** (`custom_units` in a civ file), civ victory sounds and history pages.
- **Custom resources** declared by mods (Faith, Points, ...): costs, effects, gathering, Lua, and boxes in the top bar
  that look like the game's own, with a "Show all" window when there are many.
- **New lobby game modes** from mods (the game's mode list grows), with Lua rules, `Player:Win()` / `Defeat()` and
  an all-players-one-team option.
- **Buttons on the game's own buildings** from Lua (`Game.AddCommand`), with full-colour 32-bit PNG icons.
- **Mod settings** on the Mods screen, mod conflict warnings, automatic renumbering of clashing sprites, update badges.
- An in-game **Mods** screen: filters, details for every mod, turn mods on and off.
- An in-game **Mod Hub**: browse, install, update and remove mods from
  [wololoader-mods](https://github.com/Vitrie32/wololoader-mods).
- **Hot reload**: mod changes apply when you leave the Mods screen or press Reload Mods, no restart.
- A **Lua console** (Ctrl+Shift+C) and Dev Panel in the Wololoader Base mod (multiplayer: only with cheats on).
- **Scriptorium**, a mod editor (civs, scripts, node graphs, sprites, taunts), with a "Test it" button.

One example mod comes with it: **Monklanians** (a custom civ with an Abbey). More are in the Mod Hub: **Last Stand** (co-op zombie-style waves), **Age of Miracles**, **Merchant Caravans**, **Last Town Standing**, **Faith and Relics**, **Woodlanders**.

## Install

Download the setup for your system from the [Releases](../../releases) page.

**Windows:** run `wololoader-windows-setup.exe`. It finds the game through Steam (or pick the
folder with Browse) and installs. Uninstall with the same file. The setup also turns off one flag
in the header of `AoK HD.exe` (ASLR), so Windows loads the game at the fixed address Wololoader
patches; the untouched exe is kept in `wololoader/backup/`, and Uninstall sets the flag back. If Steam
updates or verifies the game, run the setup again.

**Linux (Steam + Proton), one command:**

```bash
curl -fsSL https://wololoader.vercel.app/install.sh | sh
```

or download `wololoader-linux-setup.sh` and run `sh wololoader-linux-setup.sh`
(`wololoader/installer/get.sh` is the one-command script).

Then add this to the game's launch options in Steam (right-click the game > Properties):

```
WINEDLLOVERRIDES="version=n,b" %command%
```

Options: a game folder as argument, `--no-examples`, `--uninstall`, `--yes`
(with the one-command install: `... | sh -s -- --no-examples`).

Start the game: the main menu shows "Wololoader <version>", and the Mods button lists the mods.

## Making mods

See the [Modding Guide](https://wololoader.vercel.app/guide/) (source: [MODDING.md](MODDING.md)). Mods live in `<game>/wololoader/mods/<ModName>/` (`mod.json`, plus
`civs/*.json`, `scripts/main.lua`, `resources/`, `icons/`). The examples are in `wololoader/examples/`.

## Building from source

Needs `i686-w64-mingw32-gcc` (mingw-w64), Python 3 and Pillow, `rsvg-convert`.

```bash
cd wololoader
echo 'GAME = /path/to/Age2HD' > local.mk   # your game folder (not in git)
make              # build/version.dll, build/Scriptorium.exe (mod editor, not in the release yet)
make install      # copies them into the game folder
make release      # build/release/: Windows and Linux setups (after one `make install`)
```

- `wololoader/src/` – the loader (C). `wololoader/tools/build.py` – reference dat builder
  (`src/datbuild.c` is its C port and must give byte-identical output).
- `re/` – reverse engineering notes (`re/FINDINGS.md`: verified game addresses) and helper scripts.
- `AGENTS.md` – notes for contributors and coding agents.

## License

[MIT](LICENSE). Lua (`wololoader/third_party/lua`) is MIT licensed by Lua.org, PUC-Rio.

Age of Empires II HD is a trademark of Microsoft. Wololoader is a fan project, not affiliated with or
endorsed by Microsoft. The Mods button images are made from the game's own art at install time and are
used under Microsoft's [Game Content Usage Rules](https://www.xbox.com/en-US/developers/rules).
