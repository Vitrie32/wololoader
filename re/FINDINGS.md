# Wololoader — AoE2 HD reverse engineering notes

Target: `AoK HD.exe` (HD 5.8, source branch `age2hd_archive_5.7_with_hotfix`, 32-bit, ImageBase `0x400000`).
Game runs under Wine/Proton (logs show `Z:\run\media\...`).

## Tools
- Ghidra 12.2: `~/tools/ghidra_12.2_PUBLIC` (`JAVA_HOME=/usr/lib/jvm/java-21-openjdk`)
- Project: `~/tools/ghidra_projects/AoE2HD`
- Pseudo-C dump: `re/out/decompiled.c` (15,465 functions), function list `re/out/functions.tsv`
- Scripts: `re/scripts/` (ExportAll, VtableDump, SymFind)
- Ghidra's RTTI analysis didn't convert class names to symbols. vtables extracted from raw RTTI (`re/scripts/rtti.py`).
- Compiler: `i686-w64-mingw32-g++` installed, so proxy DLL can be built here.

## Dat loading
| Address | What |
|---|---|
| `0x72ee50` | `loadDatFile()`: World/GameData loader. `this[0x57]` = civilization count read from dat, `this[0x56]` = civilization array. Each civilization read via `FUN_0084add0`. |
| `0x72ee50` caller | `decompiled.c` line ~506351 |

Result: **Civilization count is read dynamically from dat file.** Adding a new civilization on the dat side is possible.

## Hard-coded civilization lists (main obstacle)
- `0x5ca530`: `CivType` reflection enum. Order is: GAIA, BRITON … VIETNAMESE (0x1f), **NULL_CIV, RANDOM_CIV, SPECTATOR_CIV, MIRROR_RANDOM_CIV**.
  So civilization number 32 conflicts with `NULL_CIV`. For new civilizations, either make room at the end of the enum or remap special values.
- `0x4f5960`: switch for AI script symbols (`BRITON-CIV`…`VIETNAMESE-CIV`). Unrecognized civilization id is silently skipped here, so it doesn't crash but AI `civ-selected` checks don't recognize the new civilization.
- Classes: `Civilization`, `DlcPackageCivilization`, `LimitedCivPool`, `CivilizationMode`.
- To do: list of all places comparing against `0x1f`/`0x20` constants (civilization selection screen, tech tree, name/icon tables).

## Game's own mod system
- `0x7cd3b0`: `ModManager` Step 2, scans local mod folders (each folder `0x38` byte record, package object `0x440` bytes).
- `NOMODS` command line flag exists.
- Logs: `Logs/<date>/Mod.txt`, `mod-status.txt`.
- Source files: `modmanager.cpp`, `modpackage.cpp`, `gamedataset.cpp`.

## Main menu (for Wololoader label)
- `MainMenuScreen` vtable: `0x9d66a8` (59 slots)
- Constructor: `0x6e0c30` (loads `ui\main-menu\default` layout, source `mainmenuscreen.cpp`)
- To do: find text label creation call in constructor. This way "Wololoader vX — N mods loaded" label can be added to bottom-left using the same method. If this doesn't work, backup plan is d3d9 `EndScene` overlay.

## Proposed Wololoader architecture
1. **Proxy DLL** (e.g. `version.dll`; requires `WINEDLLOVERRIDES="version=n,b"` in Wine). Loads without touching the game and scans `wololoader/mods/*` folder.
2. **Data patching:** Each mod carries a small JSON/patch file. Applied to original dat before game starts and creates merged dat (dat format according to genieutils structure).
3. **Code patching:** Hooks for `CivType` limit, civilization selection list, AI symbols and name/icon tables.
4. **Main menu label:** Hook to `MainMenuScreen` constructor.

## Main menu label (verified: static analysis, not run)
- Main menu layout comes from JSON (`resources/_common/textures/ui/main-menu/default/main-menu.json`), but there's no free label in JSON. Texts are `TextPanel` objects created in code (vftable `0x9b28a4`, size `0x210`, ctor `0x492c80`).
- **Label creator `0x48ff40`**: `__thiscall`, ecx = screen (MainMenuScreen), 12 stack arguments, `ret 0x30`, returns TextPanel*. Arguments: `parent(=screen), const char* text, x, y, w, h, Font ref_ptr[4]*, f24, f28, f2c, strId(-1 = plain text), f34`.
- Example call in ctor `0x6e19b7` (x=375 y=720 w=250 h=20, font id 0xa, flags 1,0,0,-1,0); others `0x6e1ddf`, `0x6e1ec2`.
- Font array: `0x472500(thiscall, ecx=ref_ptr[4], int fontId)` ret 4 (ctor uses 0xa and 1).
- Panel adds itself to parent in base ctor `0x482b10` (`0x4836b0`). No need to do anything extra afterwards.
- Coordinates are "reference" units; `0x491190` multiplies by screen scale (`screen+0xc0/+0x130`, `+0xc4/+0x134`) to pixels. Reference size = window's `GetClientRect` size when screen is created (`0x456060`), at screen+0x130/+0x134. For bottom-left: x=10, y=*(int*)(screen+0x134)-28.
- Change text later: `vtbl[54]` (+0xd8) = `0x497040(this, const char*, 12 byte empty vector)` ret 0x10. Empty vector = 3 zero dwords (`0x48ff40` calls it this way too).
- Hook: `MainMenuScreen::ctor 0x6e0c30` (`__thiscall(this, a, b)`, `ret 8`, returns this). First 5 bytes `55 8b ec 6a ff` (no relocation needed). Object 0x5f4 bytes, always created with `(0,0)` arguments.
- Backup plan: `TDrawSystem*` global `0xad20b4`, D3D9 device = `*(IDirect3DDevice9**)(TDrawSystem+0x48)` (`CreateDevice` call `0x453c10`, EndScene call in `0x4548b0`/`0x455940` at vtbl+0xa8). IDirect3D9 = `TDrawSystem+0x44` / `0xad20a8`.

## Main menu label — verified in-game (2026-11-14)
Just calling `0x48ff40` is not enough, label stays invisible. Need the same sequence as in constructor:
1. `screen->0x48d390(&pair, &pair)` (ret 8); `pair = {*(0xada500), *(0xada504)}`, at runtime `00000000 00bf0000`
2. `screen->0x48d3c0(0)` (ret 4)
3. Create panel with `0x48ff40(...)`
4. `screen->0x48d550(panel)` (ret 4); panel `+0x144` and `+0x15c` values set to 2 (visible state)
On 1366x768 screen `ref` and `cur` are the same. Label at position `(10, ref_h-28, 360, 20)`, font 0xa. Implementation in `wololoader/src/menu.c`.

## Civ limits (2nd round, verified)
Summary: Civilizations are no longer a fixed array, but in a **runtime registry** (global `std::map<int,Civilization*>` @ `0xaf25ec`). Main obstacle is special values (RANDOM=32...) conflicting with 32+ ids and a few `1..31` range checks.

### Special civ values (verified, from `0x5ca530` pushes)
`NULL_CIV=-1 (0xFFFFFFFF)`, `RANDOM_CIV=0x20`, `SPECTATOR_CIV=0x21`, `MIRROR_RANDOM_CIV=0x22`. (Previous note said NULL=32, incorrect.)
- `CivilizationMode` (4 elements, `DAT_00af28c0`, static init `0x41e0f0`): mode0 = specific (value 0), mode1 = random `push 0x20` @0x41e21b (text 0x281e), mode2 = spectator `0x21` @0x41e2b2 (0x281d), mode3 = mirror `0x22` @0x41e349 (0x281c). `0x59df50(idx)` reads this table (idx<4).
- `0x607490` `CivChoice::setFromInt(int)`: `cmp edi,0x20/0x21/0x22` @0x6074c9/0x607540/0x607586; other values mode0 + `map.find(id)`, if not found defaults to Briton(1). Callers send constants: `push 0x20` @0x60fac8, `push 0x21` @0x6159b1.
- `0x607820` `toInt()`: if mode==0 then `civ->id` (+8), else `mode+0x20`. `0x607840`: same but 0x21 for spectator.
- Dropdown special entries in `0x65e700`: `push 0x20` @0x65eb8d, `0x22` @0x65ec20, `0x21` @0x65ecc0; `0x65f050` `mov [edi+0x8d0],0x22/0x20` @0x65f14b/0x65f1ff; `0x59f5d0` `mov [esi+8],0x20` @0x59f5f6; `0x615540` `cmp eax,0x21` @0x615574; `0x75ef60` `cmp eax,0x21` @0x75f01e.
- Recommendation (minimal patching): add new civs with **id 35+** (32-34 placeholders in dat). Don't touch special values.

### Civ registry (clean way to add new civs)
- `0x5c9640` (cdecl): `reg(pkg, id, "x.mp3", nameStrId, bonusBaseStrId, uniqueTech1, uniqueTech2, playable)`. Inside searches `0xaf25ec` map with `0x59c990`, if not found creates `new Civilization(0x3c)` (ctor `0x5c9330`, fields: +8 id, +0xc mp3, +0x10 name string id, +0x14 bonus-string base, +0x18/+0x1c two unique-tech ids, +0x20 playable byte), then creates `DlcPackageCivilization(0x24)` and adds to package list (+0x10 = `0x4eb5+id` or `0x1d555+id`, description string id, depends on package flag).
- Calling wrappers (per package): `0x5c9d00` (Gaia + 1..13), `0x5c9a30` (14..18), `0x5c9af0` (19..23 Forgotten), `0x5c9bc0` (24..27 African K.), `0x5c9c60` (28..31 Rajas). All called from within `FUN_005d8ac0` (DLC list constructor): 0x5d9991/0x5dad54, 0x5d9edb/0x5dafe5, 0x5db2bd, 0x5db8e4, 0x5dbda0. Hook point: detour one of these wrappers, call original, then call `0x5c9640(pkg,...)` for new civs.
- Unique tech ids also written to global vector (`0xaf25e0..e4`) (`0x5ca0e0` membership check, used by `0x70ee80`). For Briton (3, 0x1cd=461) = Yeomen/Warwolf (from my own knowledge, not certain).

### Name string ids (verified)
- `0x62f080` `civNameStrId(int)`: `0->0x2776 (10102)`, `1..31 -> map.find(id)->[+0x10]` (`lea eax,[ecx-1]; cmp eax,0x1e; ja` @0x62f0af), `0x20->0x281e (10270)`, `0x21->0x281d`, `0x22->0x281c`, else `-2`. In registry name id = `10270+id` (Briton 0x281f=10271 ... Vietnamese 0x283d=10301). So for id 35, 10305 is natural choice, but 10302..10304 remain empty; or any desired id can be given during registration.
- This function called by `0x62f430` (UI text provider, vtable `0x9ceec4`, `case 0x69`). **PATCH NEEDED:** for id>=35 returns `-2` (empty text). Extend `0x62f0af` check `ja` -> (e.g. instead of `cmp eax,0x1e / ja`, make id 35+ redirect to registry).
- Civ bonus list: `0x5c98b0(civ,n)` = string `[civ+0x14]+n+1`; `0x5c9970` = `atoi(string([civ+0x14]))` (bonus line count). Briton 4400, each civ +20 (e.g. Khmer 0x19e60). New civ needs 20 consecutive strings.

### Game setup dropdown (verified, NO limit)
`0x65e700` (8 slots, `param_1+0x2e8+i*0x70`): builds `Civilization*` vector appropriate for slot with `0x75ef60`, for each `itoa(civ[+0x10])` -> text -> `0x47b480(listbox,text,icon,value=civ id)`; then Random(0x20), Mirror(0x22), Spectator(0x21). Element count comes from vector, saw no hard upper limit. Icon tied to **DLC package** not civ (`ui\*-icon-16x16.png`, package table `0x422250`: basegame/kings/conquerors/forgotten/dlc2/dlc3), special entries `ui\random|mirror|spectator-icon-16x16.png`.
- Random civ resolution at launch `0x75f5a0`/`0x75ed90`/`0x760160` (launchlevel.cpp), vector-based, no fixed array.
- `0x607860` (caller `0x61e240`): if selected civ not in `0x5c97d0(vec,3)` list, falls back to Briton. `0x5c97d0` = civs with playable(+0x20) and ownership level >= 3.

### DLC gate
- Package constructor `0x5d2620` (0xbc bytes, vtable `0x9c97e8`), package indices 0..6 (retail1..5 + dlc3). Ownership `0x46fd10`: if `pkg+0x40` flag set returns `pkg+0x44` value (forced), else Steam `SteamApps()` returns 3 (owner) / 2 / `(pkg[2]==0)`. Civ access = max ownership in civ's packages (`0x5c99d0`) >= 3. **For installer:** if new civ package has `+0x40=1, +0x44=3`, DLC ownership is disabled (this setup is Steam-less anyway).
- `LimitedCivPool` (`0xa0` bytes, constructor `0x5dd8d0/0x5dd990`, populate `0x5ddae0`): seed `% 0xb4`, selects 5-7 civs based on package flags, counter array `+0x14..+0x9b` (34 ints) -> safe up to 34-element list (didn't verify list element count, estimate).

### Hard limits / harmless ones
- `0xaae130` (32 ints, alphabetical order -> civ id) only History screen `0x6cd400` (`cmp esi,0x1f` @0x6cd7cc, text `0x514b+index`, `history\%s.txt`): cosmetic for new civs; if >=32 uses `random.mp3`.
- Statistics/achievements: `0x5bf4ba` (`cmp al,0x1f`, table `0xaad6a4`), `0x5bfe33` (table `0xaad5c8`): `civ<0x20` protected, new civ skipped, doesn't crash. "Every civ" achievement loops fixed at 32 (`0x3a..0x5a`, `3..0x23`), won't break. Player record has civ as **byte** (+0x53, +0x1f).
- AI: `0x4f5960` jump tables (`cmp eax,0x1e` @0x4f5d4b, `cmp eax,0x1f` @0x4f660f) only for fixed symbol/definition, >=32 silently skipped. `my-civ`/`civ-selected`/`players-civ` return raw int, works.
- `loadDatFile` allocates civ array by count (`0x8ea479(count,4)` @0x72ee50), no hard upper limit.
- `CivType` reflection enum (`0x5ca530`, name hash -> value): only for settings read from text; if new civ not entered, won't be recognized this way.
- Hard-coded civ special cases: `0x70ee80` (`cmp [x+0x2d8], 0xb/0xe/0x12/0x13/0x15/0x18/0x1b/0x1f`), semantics not verified.

## Custom civilization — verified in-game (2026-11-14, Wololoader v0.2)
| Address | Usage |
|---|---|
| `0x448370` | string table `thiscall(table, const char* key) -> const char*`, ret 4. Hooked, 9000000+ ids are our texts. |
| `0x5c9c60` | Rajas registration function, cdecl(pkg). Runs last. Hook calls original then registers 35+ with `0x5c9640`. |
| `0x62f080` | civ id -> name string id. Hook for 35+. |
| `0x6c5aa9` | `game_b%d.slp` civ id. Custom civ uses base civ (6 byte call patch). Without patch crashes at `0x45a496`. |
| `0x5d63e0` | DLC civ count, only called by tech tree (`0x68bcf0`, `0x84f860`). Raised to largest custom id. Without patch crashes at `0x84fd7b`. |
| CreateFileA/W IAT | `empires2_x2_p1.dat` -> `wololoader\cache\` redirection |
Dat: `civilization` field in sound records must be copied for new civ, otherwise units are silent.

## Mods screen (static analysis, not run; 2026-11-14)
Helper: `re/scripts/UiBytes.py <exe> <n> <va...>` (first n bytes at VA).

### 1. Main menu click dispatch
- Global `0xaf3eec/0xaf3ef0` (vector begin/end, 16 byte record `{id, const char* name, flag, handler}`) filled by static init `0x423580`: about(0,h `0x6e8c00`), single-player(1,`0x6ea280`), multi-player(2,`0x6e95f0`), tutorial(5,`0x6ea520`), history(4,`0x6e9210`), options(7,`0x6e8d70`), **mod-manager(id 8, flag 0, handler `0x6e9380`)**, map-editor(3,`0x6e9ed0`), exit(6,`0x6e90c0`). Name strings `0x9d6640..0x9d669c`.
- Ctor `0x6e0c30` -> `0x6e8430(screen, layout)` creates button for each record with `0x6e7da0(id, flag, layoutDef, handler)` (`ButtonDrawableControlPanel`, vtbl `0x9b1534`), button at `screen+id*0xc+0x298` (ref_ptr). mod-manager button = `screen+0x2f8`.
- Click -> functor (vtbl `0x9d6da8`, slot2 `0x6e44b0`) -> `0x6e9af0(screen; id, flag, str*, handler) ret 0x10` -> `mov ecx,screen; call handler` (0x6e9ca8). So handler is **`__fastcall(MainMenuScreen* ecx)`, `ret` (no args)**.
- `0x6e9380` = mod-manager handler: `new 0x390; memset; ModManagerScreen ctor 0x6ee030(ecx=obj, push parent); ecx=[0xada4b4]; push obj; call 0x48a8e0`. Bytes: `55 8b ec 6a ff 68 02 10 96 00` -> **hook: `wl_hook(0x6e9380, fn, 5)`**, don't call original (replace completely). No other callers (function pointer in one place only).
- Button id not needed: handler itself is unique. (Button text is `STEAM_WORKSHOP` key; if desired can return "Mods" in `0x448370` hook, not verified.)

### 2. Screen management
- Two global managers: `[0xada4b4]` = screen list/stack, `[0xada3f0]` = screen registry by name.
- `0x48a8e0` thiscall(ecx=[0xada4b4], screen) `ret 4`: brings screen to top (deactivates previous with `0x484c30(0)`, `screen+0x258`=flag in list, shows with `0x48aad0`).
- `0x48a930` thiscall(ecx=[0xada4b4], const char* name) `ret 4`: find by name and set-current (return to main menu: name `"Main Menu"` = `0x9d09b4`, MainMenuScreen's name).
- `0x485e00` thiscall(ecx=[0xada3f0], const char* name) `ret 4`: find by name and delete with `vtbl[0](1)`.
- Deletion: ScreenPanel dtor body `0x489dc0` (deletes children, removes from list with `0x48a9f0`; if deleted is current, reactivates list's new top with `0x48aad0`) => **deleting screen returns to main menu**, no separate set-current needed.
- Safe close (from within click handler, don't kill button): game's ModManager "Close" path uses `0x4382e0(std::function*)` (cdecl, 1 arg) deferred call queue; functor class vtbl `0x9d7e18` (`[0]=0x9d7e18,[4]=screen,[0x24]=&itself`, 0x28 bytes), slot2 `0x6a5390` = `screen->vtbl[0](1)`. 0x6f17a0 builds this on stack and calls `0x4382e0`.

### 3. Screen class selection
- `ScreenPanel` (vtbl `0x9b24ac`, 59 slots) **size 0x27c** (dtor `0x489e80` `0x8d480f(this,0x27c)`). Ctor `0x489d50` thiscall(ecx=obj, `const char* name`) `ret 4` (name at `screen+0x58`, pointer stored -> give static string; registers itself with manager via `0x48a9e0`). Plain example: "Blank Screen" `0x541ec0` = `new 0x27c; memset; ctor(name 0x9c02c8); 0x48a8e0`.
- Preference (b): own vtbl copy (copy 59 dwords, change slot 49). Object must be allocated from game heap with `operator new 0x8d47df` (cdecl) (dtor deletes with `0x8d480f`); memset `0x8d62f0` cdecl.
- **Slot 49 (+0xc4)**: `thiscall(ecx=screen; panel*, int code, int p3, int p4) ret 0x10`; default `0x48d430`. Buttons (`0x479xxx`, 0x4796be/0x479631) `parent=[btn+0x10]` -> `[vtbl+0xc4](btn, code, ..)`; **code 3 = left click release**, 2 = press, 5/6 right click, 1 = list selection. If handled return 1, else forward to original slot 49. ModManager `0x6f17a0` does exactly this (`param_2==screen[0x2c0]` pointer comparison).
- Background: `0x48a1c0(ecx=screen, const char* name, int id, int 0) ret 0xc` (-> `0x4917b0`). ModManager: `("scr2p", 0xc3a4, 0)` (file `resources/_common/slp/scr2p_32.slp`); CampaignMenu `("scr3", 0xc385, 0)`.
- Then `0x491250(ecx=screen, int[2]* {w,h}) ret 4` reference size (`+0x130/+0x134`), ModManager `{0x556, 0x300}` (1366x768). If not given scale division may break (estimate).
- Style array same as ModManager `0x6ee1b1..0x6ee1e4` (`screen+0x194..0x1a4`), i.e. same as main menu's 0x48d390/0x48d3c0.

### 4. Buttons
- `0x48df30` **raw text button**: `thiscall(ecx=screen)` + stack `(screen, const char* text, const char* text2|NULL, x, y, w, h, 0, 0, 0)`, `ret 0x28`, returns `ButtonDrawableControlPanel*`. Text copied (callers use 0x78 stack buffer). Example `0x6ed279`: `push 0,0,0, 0x20(h), 0x6e(w), 0x20d(y), 0x96(x), 0(text2), str, edi; ecx=edi`. Last three args font/sound etc. (all 0; meanings not verified).
- `0x48ddf0` = string-id wrapper (stack: screen, strId, strId2, x,y,w,h,0,0,0 + 0x28 bytes std::function by value, `ret 0x50`); About screen `0x69d759`.
- Binding: in preference (b) store button pointer, in slot 49 compare `code==3 && panel==btn`. Alternative: functor with `0x4667c0(ecx=btn+0x5d0, std::function*)` (game's About/other screens).
- Focus chain (optional): `0x484fd0(ecx=screen; panel** arr, short n)` and `0x484c90(ecx=screen; panel)` ret 4.
- Text label (in-screen): `0x48fe60` `thiscall(ecx=screen)` stack `(screen, TextPanel** out, const char* text, x,y,w,h, int fontId, f24, f28, f2c)` `ret 0x2c`, fills `*out` (inside font array + `0x48fdb0` -> `0x48ff40`). Example ModManager title: `x0,y0xf,w0x556,h0x28,font1,(1,1,0)`; About: font 0xb, (0,0,0); main menu 0xa (1,0,0).

### 5. Visibility / per-frame
- Didn't see separate style call for buttons (not in About/ModManager); only for TextPanel the main menu sequence 0x48d390/0x48d3c0/0x48d550 (verified in-game). No per-frame requirement (draw slot 15 `0x489f80` + children).
- Slot 48 (`+0xc0`) active/inactive notification (calls `0x484c30`), slot 17 (+0x44) tick.

## Mods screen v2 (image/text) — static analysis, not run; 2026-11-14
Helpers: `scripts/UiRet.py <full.asm> <va..>` (ret N list), `scripts/UiImgPanel.py <full.asm>` (ret N of all functions in chain), `scripts/UiVtbl.py <exe> <vtbl> [n]` (vtbl dump).

### A. Image panel from PNG file (DLC banner path, `0x6e5400`)
- Classes (RTTI): `ui::ImageFactory` ctor `0x4979a0` global `0xada4e4`; loader chain = 12 byte vector global **`[0xada9f4]`**, single element `ui::ImageFileDrawableLoader` (vtbl `0x9b29ec`, slot2 `0x476cb0` -> `0x4763e0`). `DrawablePanel` (vtbl `0x9b1920`, **size 0x134**).
- **Load**: `0x49f1f0` = `__thiscall(ecx=*(void**)0xada9f4, void** outDrawable, const char* path)`, **ret 8**, returns out (inside does `*out=0`; error -> out stays NULL, log "Failed to load texture"). Chain `0x49ee10` (ret 0xc, loader vtbl+8).
- **Path resolution**: loader passes `path` **as-is** to `D3DXCreateTextureFromFileExA(DAT_00ad20ac, path, 0xffffffff, 0xffffffff, 1, 0, fmt 0x15 A8R8G8B8, pool 2, ...)` (`0x4763e0`), cache key is also this string (`0x476020`). So raw file path: **absolute path (Z:\...) works, doesn't have to be under resources**; if relative then relative to process CWD. DLC banner first converts {storage-id 0x19, "ui\main-menu\default\dlc\dlcN.png"} -> real path with `0x43a500` (`0x828670`: storage 0 = raw path); we skip this and give absolute path directly. File format: anything D3DX reads (PNG ok). Risk: ANSI API, non-ASCII characters in path may cause issues in Wine (not verified); prefer power-of-2 sizes (16/32/64).
- **Panel**: `p = game_new(0x134); memset(p,0,0x134); 0x47a5d0(ecx=p, parent=screen, int rect[4]={x,y,w,h}, drawable)` **ret 0xc**, returns p. Ctor adds itself to parent (base `0x482b10`), `screen` is direct parent. `drawable` (+4 refcount) consumed by panel like "by-value ref" (ctor net 0): passing ref from loader as-is = transferring ownership to panel (banner code also inc + releases its own ref, equivalent). For freshly created (rc=0) object first do `rc=1`, else ctor may delete.
- **Coordinate**: label factory converts rect with `0x491190(ecx=screen, int out[4], const int in[4])` ret 8 ref->pixel; DrawablePanel ctor doesn't convert. For same behavior convert with same function (when 1366x768 ref=pixel not needed).
- **Scaling**: draw (vtbl slot15 `0x47a760` -> drawable vtbl[2] `0x475970` -> `0x498550`) uses 3-field mode structure, drawable `+0xc=mode, +0x10=vAlign, +0x14=hAlign`; default `{0,1,1}`. mode 0 = natural size, 1 = fit preserving ratio, **3 = stretch to rect**, alignment 0 left/top, 1 center, 2 right/bottom. Set: `drawable->vtbl[3]` = `0x476e80` `thiscall(drawable, int p[3]={3,1,1})` **ret 4** (before or after panel ctor; new ImageFileDrawable created each load, not shared). Natural size: `vtbl[4]` = `0x4761e0` `thiscall(drawable, int wh[2])` ret 4.
- Recipe: `chain=*(void**)0xada9f4; void* d=0; ((LoadImg_t)0x49f1f0)(chain,&d,"Z:\\...\\icon.png"); if(d){int m[3]={3,1,1}; (*(void***)d)[3] -> SetFit(d,m); int r[4]={x,y,w,h}; ScaleRect(screen,rs,r); p=new(0x134); memset; DrawablePanelCtor(p,screen,rs,d);}`
- Frame/background (optional): `ui::SolidColorDrawable` ctor `0x4a0350(ecx=obj(0x10 bytes), const Color* 8 bytes)` ret 4 (slot2 `0x4a0430` = fill rect) can be given to DrawablePanel, but Color format not verified (`0xada500/0xada508...` named color globals; `0x414b40`). Cheap way: stretch own PNG (with border) with mode 3.

### B. TextPanel (vtbl `0x9b28a4`, 60 slots)
- **SetText = slot 54 (+0xd8) `0x497040`**: `thiscall(panel, const char* text, int z0, int z1, int z2)` **ret 0x10** (verified; 3 dwords = by-value empty `std::vector`, give zeros). Inside calls slot 59 (+0xec, `0x4936b0`: delete lines, relayout flag) then slot 57 (+0xe4, `0x495f40`, same signature ret 0x10). Text copied; draw every frame (slot 15 `0x496260`) from line list -> **no extra layout/redraw call needed**. "" = single empty line.
- **Line break**: `0x495f40` splits text on '\n' (10) (each line separate line object) -> `\n` always works.
- **Word wrap**: `+0x1ac` byte (= `0x48fe60`'s **f2c** argument -> `0x495790`) if 0 only '\n'; **≠0 then `0x4973a0` word wrapping** (breaks on \r \n and space, width = panel width-10 px, with binary search). For long text f2c=1, give height sufficient for line count.
- **f24 / f28** (`0x48ff40` inside `0x4951d0(f24^1, f28^1, 0)`): `f24^1` -> `rec[0]+0xc` -> `0x494be0` flags: 0 -> 5 (DT_CENTER|DT_VCENTER), 1 -> 4 (DT_LEFT|DT_VCENTER), 2 -> 6 (right). So **f24=1 horizontal CENTER, f24=0 LEFT** (About uses f24=0). `f28^1` -> panel `+0x198`; in `0x493f70` if 0 text block **vertically centered**, ≠0 starts from top: **f28=1 vertical center, 0 top**. Confidence: medium-high (flag values match D3DX DT_*; not tested in-game). Current ui_mods.c `(1,center,0)` -> lines may come out horizontally centered; to left-align try f24=0.
- **Color**: panel has 3 style blocks `+0x134/+0x14c/+0x164` (0x18 bytes: `{Color main(8), Color shadow(8), mode, flag}`; ctor main=`0xada508`, shadow=`0xada500`, `0x48d550` sets mode=2 (shadowed); `0x4a0b20` draws by mode). Color 8 bytes (two dwords), format and which block is "normal" not clear -> **not verified**. For selected line highlight we recommend button SetChecked (below) instead of color.

### C. Button state / visibility
- **Persistent "selected"**: `0x477450` `thiscall(button, char checked)` **ret 4** (non-virtual; `[btn+0x16c]=checked`). Main menu click handler `0x6e9af0` does `SetChecked(0)` on previous button, `SetChecked(1)` on new one and stores id at `screen+0x27c` (`0x6e9c26/0x6e9c5a`). Draw `0x4772f0`: when not mouse-over state = checked ? 1 : 0 (hover: checked?2... ), so pressed/selected appearance persists. Valid for 0x48df30 button too since it's ButtonDrawableControlPanel (static; not tested in-game).
- **Change button text (SetText)**: `0x479ce0` `thiscall(ecx=button, int slot, const char* text)` **ret 8**. Used by in-game ModManager (`0x6ef720`, `0x6ef780`, `0x6ef800`) to change button texts (Disable/Enable etc.). slot 0 is main text. Does `std::string` copy, safely releases old text.
- **Enable/disable**: `ButtonPanel` vtbl slot 7 (+0x1c) `0x479840` `thiscall(btn, char disabled)` **ret 4** (`+0xc9`, `+0x24`). (Previous note said slot 61, vtable dump verified it's slot 7).
- **Hide/show**: Panel `+0x1c` (int, 1 in ctor `0x482b6d`) = visible/active flag: screen draw `0x490490`, child draw wrapper slot 20 `0x4845d0`, input `0x483df0/0x4840b0/0x4841a0` and tick `0x4847e0` all skip if `[this+0x1c]==0`. No separate setter function; write directly `*(int*)(panel+0x1c)=0/1`. Confidence: medium (static, not tested). Alternative recipe: create as many labels as needed and use SetText instead of recreating lines.
- Focus: `+0xc8` = focus flag (key handler slot 22 `0x483f80` checks this), set with `0x484c30`.

## Population limit (pop cap 1000) — 2026-11-14, agy research + Claude verification

- `0x47b690` dropdown AddItem: `__thiscall(list, const char *text, int value)`, `ret 8` (verified).
- `0x64d25b..0x64d33c` game setup screen population list: loop 25..200 (`cmp esi,0xc8` @0x64d2b8), then 250/300/400/500 manually; call adding "500" `0x64d337` (`e8`, 5 bytes).
- `0x6da131` lobby browser filter: same structure, "500" call.
- `0x5961fe` `mov ecx,0x1f4; cmp eax,ecx; cmova` in FUN_005960a0 population 500 clamp.
- `0x748068..0x748098` player resource 32 (bonus population limit, `[eax+0x80]`) clamped to 25..500 range: `comiss xmm1,[0x9b50e0]` @0x748079, `mov [eax+0x80],500.0f` @0x748082.
  `0x9b50e0` (500.0f) shared with frame duration clamp (`0x6c1f31`) — to change, redirect comiss operand.
- `0x750fd1` FUN_00750f70 (resource setter): on player with `[this+8]==1`, resource 4 (population headroom) set to 500.0 every time. Hun bonus (resource 4 = 1000) was being cut to 500 here (504/504 seen in-game) → changed to 1000.0.
- `0x52c640` AI unit gathering: 500-element stack arrays (`cmp esi,0x1f4` @0x52c820, @0x52ca92) — NEVER enlarge.
- `0x637680` `/LIMIT` command line default 200 — not touched.
Implementation: `wololoader/src/popcap.c`.

## Multiplayer data check + unique unit (2026-11-14, agy; summary, detail in re/research/)
- Lobby compares CRC32 of game data: `0x584a40` host handler compares client's `packet+0x18` value, if mismatch `0x583f50(player, reason=3)` → "GAMEDATA_CRC_CHECK" (verified, decompiled). Client side `GAMEDATA_CRC_CHECK_HOST` (around `0x6566dd`, GUESS). Result: same Wololoader mods = same dat = same CRC → should be playable together (not tested in-game).
- Steam lobby data "Dataset" / "DatasetWorkshopId" (SetLobbyData in `0x5b5310`), strings present in exe (verified).
- Empty slots in dat for new units: units 1221 and 1224..1400 named "RESERVED" (verified with datfile.py, civ 1). Some game arrays limited to 1400 (`0x578`) → place new units in these slots.
- Castle unique unit: `train_location_id=82`, `button_id=1`, enabled by civ-specific tech (civ field) with effect type 2; tech tree screen gets castle units from data (agy, partially verified).

## Text encoding (2026-11-14, found with freeze watchdog)
- Game's text system is UTF-8. `0x444e40` UTF-8 decoder: returns 0 without advancing on invalid byte; caller `0x4936e0` (`SetText` 0x497040 → 0x4973a0 → 0x4936e0) enters infinite loop → game freezes (single 0xB7 byte in tech tree description).
- Old "Â·" bug was json.c's double encoding; Windows-1252 conversion removed, civs.c now turns invalid UTF-8 bytes to '?'.

## Yes/No dialog close (2026-11-14)
- Dialog opened with `0x490da0` doesn't close by itself. Parent closes it by name with `0x485e00` `__thiscall(*(void**)0xada3f0, const char *name)` `ret 4` after receiving answer in event slot (slot 49) (main menu "WorkshopTOS" at `0x6e4cde`). code 0 = Yes.

## Text ids, UI textures, Workshop (2026-11-14, verified in-game)
- Unit button rich tooltip: game wants `creation id + 20000` (Teutonic Knight 6112 -> 26112). Our unit 28501 -> 48501 seen in log.
- Tech tree tooltip: `0x708d50` inside `mov eax,[unit+0x270]; add eax,0x5208` (0x708e02) -> `name id + 21000`. 49500 seen in log.
- Tech tree wants `X` and `X+10000` pairs for researches (e.g. 7410 / 17410).
- UI images (main menu buttons etc.) loaded with `d3dx9_43!D3DXCreateTextureFromFileExA` (exe import), doesn't go through CreateFile hooks.
- `0x7ce430` = `NOMODS` command line flag reader (`a0 18 59 af 00 c3`, `mov al,[0xaf5918]; ret`). Callers 0x5c34a8, 0x7cd48b (`test al,al; je`). Changing to `mov al,1; ret` skips ModManager/Workshop sync ("Commandline NOMODS detected").
- Startup freeze: main thread was waiting in `0x454dae` `call [ecx+0x44]` (IDirect3DDevice9::Present) while Workshop dialog open; not seen again with NOMODS.

## Taunt list UI (2026-11-17, verified in disassembly)
- Function `0x686210`: `__thiscall`, prologue `55 8b ec 6a ff 68 14 35 95 00`, `ret 4` at `0x686695`. Builds the in-game taunt list (F11 menu).
- Loop variable: `esi` register holds taunt number (starts at 1). Loop at `0x6862e3`–`0x6865b8` iterates over list entries (56-byte stride, `0x38`), jump back at `0x6865b8` (`jb 0x6862e3`).
- String lookup: taunts 1-38 use string id `taunt + 0x1bc5` (`0x6863ab`), 39-41 use `taunt + 0x1edc` (`0x6863cf`), 42 uses `0x1bec` (`0x6863f0`).
- **Filename extraction for taunts > 42**: code at `0x6863fc`–`0x686446` reads `[ebp-0x138]` (filename buffer), skips "NN " prefix (`lea eax,[ecx-0x3]` at `0x686417`), uses the rest as display text. This is built-in vanilla game code, no hook needed.
- **Filter blocking custom taunts**: two identical checks reject taunts outside specific ranges (3-6, 31-38, 42). Each ends with `cmp esi,0x2a; jne <reject>` which accepts only taunt 42 and rejects 43+.
  - First filter: `0x68635f` (`83 fe 2a`), `0x686364` (`75 04`, jne +4 → `0x68636a` reject)
  - Second filter: `0x68651e` (`83 fe 2a`), `0x686521` (`75 3a`, jne +58 → `0x68655d` reject)
- **Patch**: change both `75` (jne) to `7C` (jl) so `taunt >= 42` accepted. Verified bytes at file offsets `0x285364` and `0x285521`.

## Taunt sound playback (2026-11-17)
- `0x495eb0`: `__thiscall`, prologue `55 8b ec 6a ff 68 90 3c 91 00`, `ret 0x14` (2 dwords + a 12-byte string object by value). Verified in disassembly. It has 38 callers, so it is a generic sound function, not taunt-only: do not hook it globally for a taunt mute; patch the taunt call site instead (not found yet).

## GameSetupScreen constructor (2026-11-17, from decompiled code + lobby.c)
- Function `0x647430`: `__thiscall(void *this, void *parent)`, GameSetupScreen constructor. Signature from Ghidra: `undefined4 * __thiscall FUN_00647430(undefined4 *param_1, int param_2)` — returns first param (this pointer). From lobby.c comments: `ret 4`, prologue `55 8b ec 6a ff`. Vtable at `0x9d0d10`, event handler slot 49 at offset `0x48d430`. **PARTIALLY VERIFIED**: calling convention and return type confirmed from decompiled code; prologue bytes need objdump confirmation.
