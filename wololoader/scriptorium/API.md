# Scriptorium v2 — web UI architecture and API contract

v1 (Win32 controls) looked dated. v2 keeps the C backend (catalog.c, modio.c,
sc_zlib.c, ../src/datbuild.c, ../src/json.c) and replaces the Win32 UI with an
HTML/CSS/JS front end shown in an app-mode browser window ("Electron-like" but
using the browser already installed, so the exe stays small and fast).

## Process

`Scriptorium.exe [--game "<dir>"] [--port N] [--no-window] [--selftest]`

1. Find the game dir (exe in `<game>\wololoader\`, or `--game`).
2. Load the catalog once (kept in memory).
3. Start an HTTP/1.1 server (Winsock, single thread is fine) bound to
   **127.0.0.1 only**, port `--port` or a free one (bind port 0, read it back).
   A random 32-hex token is generated at start; every `/api/*` request must
   carry header `X-Scriptorium-Token: <token>` (the page gets it from the URL
   `http://127.0.0.1:PORT/#token=...`). Requests without it get 403. Reject
   requests whose `Host` is not `127.0.0.1:PORT` (DNS rebinding).
4. Open the app window (unless `--no-window`):
   - Windows: first found of Edge (`%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe`,
     `%ProgramFiles%\...`), Chrome (`%ProgramFiles%\Google\Chrome\Application\chrome.exe`,
     `%LocalAppData%\Google\Chrome\Application\chrome.exe`) with
     `--app=URL --user-data-dir=%LOCALAPPDATA%\Scriptorium\browser --window-size=1280,820`;
     else `ShellExecute(URL)` (normal browser tab).
   - Under Wine (detect: `GetProcAddress(GetModuleHandle("ntdll"),"wine_get_version")`):
     run `start /unix <browser> --app=URL --user-data-dir=<home>/.cache/scriptorium-browser --window-size=1280,820`
     trying `/usr/bin/google-chrome-stable`, `/usr/bin/google-chrome`, `/usr/bin/chromium`,
     `/usr/bin/chromium-browser`, `/usr/bin/brave-browser`, `/usr/bin/microsoft-edge`
     (check existence through `Z:` path); else `winebrowser URL`.
     (`<home>`: from env `WINEHOMEDIR` -> unix path, or `HOME` if set, else `/tmp`.)
5. Lifetime: the page POSTs `/api/ping` every 2 s. If no ping for 15 s after the
   first one, the server exits (window closed). Also exits on `POST /api/quit`.
6. Static files (index.html, app.css, app.js and assets under `ui/`) are
   **embedded in the exe** (Makefile generates `build/ui_files.h` with `xxd -i`
   or a small script) and served with correct Content-Type. No external CDNs:
   everything works offline.

## JSON API (all bodies UTF-8 JSON; errors -> HTTP 4xx/5xx `{ "error": "message" }`)

| Method + path | Body | Response |
|---|---|---|
| `GET /api/info` | | `{ "game_dir", "mods_dir", "version": "2.0" }` |
| `GET /api/catalog` | | `{ "civs":[{id,name}], "units":[{id,type,class,icon,internal,name}], "techs":[{id,icon,internal,name}], "stats":[{field,label}] }` (icon = dat icon_id, -1 if unknown) (stats = SC_STAT_FIELD/LABEL order) |
| `GET /api/stats?civ=C&unit=U` | | `{ "<field>": number, ... }` only fields the unit has; 404 if none |
| `GET /api/mods[?all=1]` | | `[{ "folder", "name", "version", "author", "enabled": bool, "civs": ["file.json", ...], "has_icon": bool, "modified": unix seconds of the last change to mod.json or civs/ (0 = unknown) }]`. WololoaderBase (Wololoader's own feature switches) is left out unless `all=1`. |
| `GET /api/mod/<folder>` | | `{ "folder", "mod": <mod.json object as-is>, "civs": { "<file.json>": <civ object as-is> } }` |
| `PUT /api/mod/<folder>` | `{ "mod": {...}, "civs": { "<file>": {...} }, "deleted_civs": ["<file>"] }` | `{ "ok": true }` — each object is re-serialized pretty (2 spaces, key order kept) and written atomically; deleted civ files removed. File names must match `^[A-Za-z0-9_\-]+\.json$`, folder `^[A-Za-z0-9_\- ]+$` (reject `..`, slashes). |
| `POST /api/mods` | `{ "name": "My Mod" }` | `{ "folder" }` — creates `mods/<Folder>/mod.json` (`name`, `version` "0.1", `author` "", `description` "") and `civs/`. Folder = name with unsafe chars removed; 409 if exists. |
| `POST /api/validate` | same as PUT body + `"folder"` | `{ "ok": bool, "error": "...", "civs_built": n }` — writes the given state to a temp mods dir containing only this mod, runs `wl_dat_build` to a temp out dir, cleans up. Never touches the real mod. |
| `GET /api/icon/<folder>` | | the mod's `icon.png` (image/png) or 404. Needs the token header, so the UI fetches it and shows a blob URL (an `<img src>` cannot send the header) |
| `GET /api/icon-library`, `GET /api/icon-library/<category>/<id>.png` | | the built-in icon library (embedded in the exe from `scriptorium/icon_library`): catalog `[{id,name,category,tags,svg,png}]` (`png` = `<category>/<id>.png`, 64x64) and the PNGs (image/png). Needs the token header. The UI (`iconlib.js`) copies a chosen PNG into the mod with `PUT /api/file`. |
| `POST /api/upload?folder=F&name=N` | raw bytes | `{ "file": "N" }` — copies a sound / slp / icon into the mod folder (N: safe file name, max 20 MB) |
| `POST /api/open-folder` | `{ "folder"?: "F" }` | opens the mods dir or that mod's dir (shell.c logic: winebrowser under Wine) |
| `GET /api/game-file?path=P` | | the game's file `resources/P` (`.slp .bina .mp3 .wav .png` only; P relative, forward slashes, no `..`, no drive letters; 404 if missing) |
| `GET /api/game-list?dir=D` | | `{ "files": [{name, size}], "truncated": bool }` (allowed extensions only, max 20000 entries) |
| `GET /api/mod-tree?folder=F` | | `[{ "path": "taunts/43 x.mp3", "size": n }]` every file of the mod folder, below it too ("/" separators, max 4000 files, 6 folder levels). Hidden names, `civs/` and `mod.json` are left out. |
| `GET /api/files?folder=F&dir=D` | | the file names directly in `D` (folders left out) |
| `GET /api/file?folder=F&path=P`, `PUT` (raw bytes body), `DELETE` | | one file of the mod folder. P: relative, `/` between 1..6 segments of `[A-Za-z0-9_.- ]`, no `..`. Not `mod.json` and not `civs/` (those go through `PUT /api/mod/<folder>`). Max 20 MB. |
| `POST /api/ping`, `POST /api/quit` | | `{ "ok": true }` |

Mod/civ json keys: see SPEC.md "Mod format". The front end edits these objects
directly and must keep keys it does not know.

## Front end (ui/index.html, ui/app.css, ui/app.js — vanilla JS, no build step)

Modern, friendly, AoE2-flavoured: warm dark wood sidebar, parchment content
cards, gold accents, rounded corners, soft shadows, smooth hover/transition,
serif headings (`"Cinzel", "Trajan Pro", Georgia, serif` — local fonts only),
clean sans body text. Responsive down to 1000 px wide. Keyboard: Ctrl+S save.

- **Sidebar**: app title "Scriptorium", search box, mod cards (icon or initial
  letter badge, name, version, enabled dot), "+ New mod", "Open mods folder".
- **Mod header**: name, version, author, enabled toggle switch, Save (primary),
  Validate, Revert; "unsaved" badge when the state differs from the loaded one
  (compare JSON, so loading never marks it dirty).
- **Tabs**: Overview (mod.json fields + civ cards + "New civilization"),
  then per selected civ: Identity, Bonuses, Units, Tech tree (as in SPEC.md, but
  modern widgets):
  - Identity: name, dat_name (auto, ASCII, max 19), base civ select with names,
    description editor with live preview (`<b>..<b>` bold, `\n` lines) and
    "Generate from bonuses", AI names as removable chips, sound / interface art
    file pickers (upload).
  - Bonuses: preset cards with a toggle and a number input each (SPEC.md list),
    plus an "Advanced effects" table with type as a named select
    (0 Set attribute, 1 Resource, 2 Enable unit, 3 Upgrade unit, 4 Add attribute,
    5 Multiply attribute, 6 Multiply resource, 101 Tech cost, 102 Disable tech,
    103 Tech time) and team-bonus switch per row.
  - Units: searchable virtualised list (1389 units: only render visible rows)
    with name, internal name, id; detail panel with each stat as a number input,
    the base civ's original value shown, changed values highlighted, reset per
    field / per unit; "modified only" filter.
  - Tech tree: searchable lists of techs and units with a 3-state segmented
    control (Base / Enabled / Disabled); unique techs as two searchable selects.
- Toasts for save / validate results; confirm dialogs for delete and for
  leaving unsaved changes. All text English.
