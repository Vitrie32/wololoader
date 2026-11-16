# Wololoader — notes for coding agents


Wololoader is a mod loader for **Age of Empires II HD 5.8** (`AoK HD.exe`, 32-bit, ImageBase `0x400000`).
It is a `version.dll` proxy written in C (built with `i686-w64-mingw32-gcc`) that patches the game at runtime.
The game runs on Linux through Steam + Proton; the user tests in-game, agents cannot.

## Layout

| Path | What it is |
|---|---|
| `wololoader/src/main.c` | `DllMain`: forwards `version.dll` exports, checks the exe build, calls every `wl_install_*`. |
| `wololoader/src/proxy_version.S`, `version.def` | Jump stubs for the 17 real `version.dll` exports. |
| `wololoader/src/hook.c` | `wl_hook` (5+ byte JMP detour with trampoline) and `wl_patch_call` (CALL patch). |
| `wololoader/src/mods.c` | Scans `<game>\wololoader\mods\*\mod.json` into `g_wl.mods`. |
| `wololoader/src/files.c` | IAT hooks on file APIs: dat -> `wololoader\cache\`, registered redirects, asset override (`<mod>\resources\...`) and mod files merged into folder listings. |
| `wololoader/src/civs.c` | Custom civs (id 35+): registration, names, string lookup hook, tech tree count, UI art patch. |
| `wololoader/src/menu.c` | "Wololoader vX - N mods loaded" label on the main menu. |
| `wololoader/src/ui_mods.c` | In-game Mods screen (replaces the Steam Workshop button). |
| `wololoader/src/datbuild.c/.h` | C port of the dat builder (byte-identical to `tools/build.py`). |
| `wololoader/src/json.c/.h` | Small JSON parser shared by the builder and the DLL. |
| `wololoader/src/zlib_game.c` | Raw deflate/inflate via the game's `zlibwapi.dll` (zlib 1.2.7, `__stdcall`). |
| `wololoader/src/autobuild.c` | At start: fingerprints mods, rebuilds `cache\` with datbuild when they changed; on error uses the original dat. |
| `wololoader/src/crash.c` | Vectored exception logger (writes EIP, registers, exe return addresses to the log). |
| `wololoader/src/shell.c` | Opens folders (`winebrowser` under Wine, `ShellExecute` on Windows). |
| `wololoader/tools/datfile.py` | Reference Genie dat parser/serializer (full round trip verified). |
| `wololoader/tools/build.py` | Reference builder: merges `mods/*/civs/*.json` into `cache/empires2_x2_p1.dat`. |
| `wololoader/tools/test_datbuild.c` | Native Linux test for `datbuild.c` (system zlib). |
| `wololoader/examples/` | Example mods. |
| `re/FINDINGS.md` | **All verified game addresses** and how they were found. Read the relevant section before touching game code. |
| `re/scripts/` | Ghidra/objdump helper scripts. `re/out/` (decompiled pseudo-C, not in git) may exist locally. |

## Runtime facts

- Game folder (read-only for agents): `<game dir>`.
- The game uses `resources/_common/dat/empires2_x2_p1.dat` (raw deflate, 32 civs: 0 = Gaia, 1..31).
- Civ ids 32, 33, 34 are hard-wired in the exe (RANDOM, SPECTATOR, MIRROR_RANDOM); custom civs start at **35**, and the builder inserts placeholder civs 32..34.
- Custom string ids: `9000000 + civ*100 + slot` (slot 0 name, 10 AI-name count, 11.. AI names) and civ descriptions at `0x1d555+id` / `0x4eb5+id`, all served by the hook on `0x448370`.
- Civ json identity keys (`description`, `ai_names`, `sound`, `interface_slp`) are read by the DLL (`civs.c`); both builders only allow them. Keep `build.py` and `datbuild.c` key lists in sync.
- Log: `<game>\wololoader\wololoader.log` (truncated every start).

## Rules

1. **Never write into the game folder** except through `make install`, and only when asked.
2. **Only use addresses that are verified** in `re/FINDINGS.md` or that you verified in the disassembly yourself (prologue bytes, `ret N`, calling convention). Say clearly what is unverified.
3. Detours: `wl_hook(addr, fn, stolen)` — `stolen` must cover whole instructions and contain no relative jumps/calls. Write the stolen bytes in a comment.
4. Game calling conventions in C: `__thiscall` typedefs for member functions; our replacements are `__fastcall(void *self, void *edx, ...)` so the stack cleanup matches.
   Hooks must return what the original returns: constructors return `this` in eax, so a `void` ctor hook crashes the caller (lobby crash, 2026-11-15).
5. Never call into other DLLs (zlibwapi, game code) from `DllMain`: their CRT may not be initialised yet (R6030). Do such work lazily on first use.
6. Game objects that the game frees must be allocated with the game's `operator new` (`0x8d47df`).
7. Copied vtables must include the RTTI slot at `vtable[-1]` (`dynamic_cast` reads it).
8. Comment the *why*: which game function, which address, what was verified. The next agent may be a smaller model.
9. Keep `datbuild.c` byte-identical to `tools/build.py`; re-run the comparison after any change.

## Build and test

```bash
cd wololoader && make                         # build/version.dll, must have zero warnings
gcc -O2 -Wall -Wextra -std=c11 -o /tmp/t tools/test_datbuild.c src/datbuild.c src/json.c -lz
python3 -I tools/build.py "<fake game dir>"   # reference output to compare against
```

Install (only when the user asks): `make install`. Steam launch options must contain `WINEDLLOVERRIDES="version=n,b"`.
