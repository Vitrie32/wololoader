# Wololoader Mod Hub — Index & Package Format

The **Mod Hub** allows Wololoader users to browse, install, update, and uninstall community mods directly from a public mod index — both **in-game** (**Main Menu > Mods > Mod Hub**) and in **Scriptorium**.

### In-Game Mod Hub Screen (`ui_hub.c`)

- **Access**: Opened via the **Mod Hub** button at the top right of the in-game **Mods** screen (hidden in the multiplayer lobby read-only view).
- **Networking & Icons**: Uses the shared `hubcore` module (`src/hubcore.c`) in a background worker thread so the game UI never blocks. Remote mod icons are cached under `<game>\wololoader\cache\hub_icons\` and scaled with WIC (`iconcache.c`).
- **Actions**: Paged mod list (`< Prev` / `Next >`), **Install** / **Update** / **Uninstall** / **Refresh** / **Back**, live progress in the status line, and offline/error reporting. Returning via **Back** rescans `<game>\wololoader\mods\`; installing or uninstalling only changes files on disk and takes effect after restarting the game.

---

## 1. Index Location & Configuration

- **Default Index URL**:
  ```
  https://raw.githubusercontent.com/Vitrie32/wololoader-mods/main/index.json
  ```

- **Configuration Precedence** (highest to lowest):
  1. Environment variable `WL_HUB_URL` (accepts `https://`, `http://`, `file://`, or a local filesystem path).
  2. Scriptorium setting `hub_url` in `<game>\wololoader\scriptorium-settings.json` (editable in the Scriptorium Mod Hub settings dialog).
  3. Built-in default URL above.

Relative `download` and `icon` URLs inside `index.json` are resolved relative to the directory of the fetched index URL.

---

## 2. Index JSON Format (`index.json`)

The index is a UTF-8 JSON object:

```json
{
  "schema_version": 1,
  "updated": "2026-11-24",
  "mods": [
    {
      "id": "Monklanians",
      "name": "Monklanians",
      "version": "1.1.0",
      "author": "ali",
      "description": "Adds the Monklanians civilization, tough fast-walking monks, and the Abbey unique building.",
      "tags": ["civilization", "balance"],
      "icon": "https://raw.githubusercontent.com/Vitrie32/wololoader-mods/main/mods/Monklanians/icon.png",
      "download": "https://github.com/Vitrie32/wololoader-mods/releases/download/monklanians-1.1.0/Monklanians.zip",
      "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
      "size": 14820,
      "requires_wololoader": "0.4.0",
      "depends": [],
      "updated": "2026-11-24"
    }
  ]
}
```

### Root Fields

| Key | Type | Required | Description |
|---|---|---|---|
| `schema_version` | integer | **Yes** | Must be `1`. Scriptorium rejects indices with unsupported higher versions. |
| `updated` | string | No | ISO-8601 date string for the last index update. |
| `mods` | array | **Yes** | List of mod entry objects. |

### Mod Entry Fields

| Key | Type | Required | Description |
|---|---|---|---|
| `id` | string | **Yes** | Folder name under `<game>\wololoader\mods\<id>\`. Must match `^[A-Za-z0-9_\- ]+$` (no `/`, `\`, `..`, `:`, and not `WololoaderBase`). |
| `name` | string | **Yes** | Human-readable display name. |
| `version` | string | **Yes** | Version string used for update comparison (dot-separated numbers with optional leading `v` and `-prerelease` suffix, e.g. `"1.2.0"`, `"0.4.1"`). |
| `author` | string | No | Author name or handle. |
| `description` | string | No | Full mod description shown in the hub detail view (short preview shown on cards). |
| `tags` | array of strings | No | Category/search tags (e.g. `["civilization", "scripts", "graphics"]`). |
| `icon` | string | No | URL (`https://`, `http://`, `file://`, or path relative to index) to a PNG/JPEG icon. |
| `download` | string | **Yes** | URL (`https://`, `http://`, `file://`, or path relative to index) to the mod `.zip` archive. |
| `sha256` | string | **Yes** | Expected 64-character hex SHA-256 digest of the `.zip` archive. |
| `size` | integer | **Yes** | Expected byte size of the `.zip` archive (`> 0`). |
| `requires_wololoader` | string | No | Minimum Wololoader version required (e.g. `"0.4.0"`). |
| `depends` | array of strings | No | List of mod `id`s that must be installed before this mod. |
| `updated` | string | No | ISO-8601 date string (`YYYY-MM-DD`) of this mod version. |

---

## 3. Zip Archive Rules & Safety

A mod `.zip` must use standard ZIP format (Stored method `0` or Deflate method `8`; Zip64 and encrypted archives are not supported) and follow one of two layouts:

1. **Flat root**: `mod.json` sits directly at the archive root (`mod.json`, `civs/...`, `scripts/...`).
2. **Single wrapper folder**: Every archive entry lives under a single top-level directory containing `mod.json` (`MyMod/mod.json`, `MyMod/civs/...`). Scriptorium strips that single top-level prefix during extraction so files land in `<game>\wololoader\mods\<id>\`.

For security and integrity, Scriptorium verifies before and during extraction:
- Exact byte `size` and `sha256` match against the index entry.
- CRC-32 checksum match for every extracted file.
- Rejects absolute paths, drive letters / colons (`:`), `..` traversal segments, symlinks (POSIX `0120000` or Windows reparse points), and special files.
- Rejects archives exceeding limits: **100 MB** download size, **2048** entries, **32 MB** per uncompressed entry, **200 MB** total uncompressed size.
- Extracts into `mods\<id>.tmp`, validates `mod.json`, writes `.hub.json`, and atomically renames into `mods\<id>`.

---

## 4. Local Tracking & Uninstall

- **`.hub.json`**: Written inside `<game>\wololoader\mods\<id>\.hub.json` for hub-installed mods:
  ```json
  {
    "id": "Monklanians",
    "source_url": "https://raw.githubusercontent.com/Vitrie32/wololoader-mods/main/index.json",
    "download": "https://.../Monklanians.zip",
    "version": "1.1.0",
    "sha256": "...",
    "installed_at": "2026-11-24T16:20:00Z"
  }
  ```
- **Preserving `enabled` state**: When updating (or overwriting) an existing mod, the user's `"enabled"` value from the previous `mod.json` is preserved.
- **Non-hub mods**: If `<game>\wololoader\mods\<id>` exists without `.hub.json`, Scriptorium refuses to overwrite it unless `overwrite: true` is explicitly passed, and refuses to uninstall it from the Hub UI.
- **Uninstall**: Moves `<game>\wololoader\mods\<id>` to `<game>\wololoader\trash\<id>-<YYYYMMDD-HHMMSS>` rather than permanently deleting files.

---

## 5. Scriptorium HTTP API

All `/api/hub/*` routes require the `X-Scriptorium-Token` header and valid `127.0.0.1:PORT` `Host` header.

| Method + Path | Body | Response |
|---|---|---|
| `GET /api/hub/index[?refresh=1]` | — | Cached (10 min TTL) index annotated with `installed`, `installed_version`, `installed_enabled`, `hub_managed`, `update_available`, `compatible`, plus `effective_url`, `configured_url`, `default_url`, `env_override`, `cached`. |
| `GET /api/hub/icon?id=<id>` | — | Fetches and serves the mod's remote/local `icon` image (`image/png`, `image/jpeg`, etc.). |
| `POST /api/hub/install` | `{"id": "...", "overwrite"?: bool}` | Installs dependencies first, downloads/verifies/extracts mod, writes `.hub.json`. Returns `{"ok": true, "id", "version", "installed_depends": [...], "updated": bool}`. |
| `POST /api/hub/uninstall` | `{"id": "..."}` | Hub-managed mods only. Moves mod folder to `wololoader/trash/<id>-<date>`. Returns `{"ok": true, "id", "trash_path"}`. |
