# Wololoader

**Website: https://wololoader.vercel.app** · [Download](../../releases/latest) · [Modding guide](MODDING.md)

A mod loader for **Age of Empires II HD** (Steam, version 5.8). It runs as a `version.dll` next to the
game and adds, without changing the game's files:

- **Custom civilizations** (civ id 35 and up) with their own units, unique unit, techs, names and AI names.
- **Unique buildings** with their own art, on a villager "Mod Buildings" page, with train / research /
  Lua command buttons.
- **Lua scripting**: game events, commands (resources, units, techs, ages), timers on game time.
- **Non-pausing on-screen UI** for mods: frames, labels, buttons, text boxes, images, hotkeys.
- **Custom icons** and asset overrides (`<mod>/resources/...` replaces the game's files).
- An in-game **Mods** screen to turn mods on and off.

Two example mods come with it: **Monklanians** (a custom civ with an Abbey) and **Dev Panel** (a
developer panel: resources, ages, units).

## Install

Download the setup for your system from the [Releases](../../releases) page.

**Windows:** run `wololoader-windows-setup.exe`. It finds the game through Steam (or pick the
folder with Browse) and installs. Uninstall with the same file.

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

See [MODDING.md](MODDING.md). Mods live in `<game>/wololoader/mods/<ModName>/` (`mod.json`, plus
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
